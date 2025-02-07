/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include <iomanip>
#include <locale>
#include "pugixml.hpp"
#include <sstream>
#include "SyslogAgentSharedConstants.h"
#include "EventHandlerMessageQueuer.h"
#include "Logger.h"
#include "Globals.h"
#include "Util.h"
#include "OStreamBuf.h"
#include "SyslogSender.h"
#include "StatefulLogger.h"

#include "EventLogger.h"
#include "XMLToJsonConverter.h"

using namespace std;
using namespace Syslog_agent;

namespace Syslog_agent {

    void EventHandlerMessageQueuer::EventData::parseFrom(EventLogEvent& event, const Configuration& config) {

        // Get provider
        auto provider_value = event.getXmlDoc().child("Event").child("System")
            .child("Provider").attribute("Name").value();
        EventData::safeCopyString(provider, MAX_PROVIDER_LEN, provider_value);

        // Get event ID
        auto event_id_value = event.getXmlDoc().child("Event").child("System")
            .child("EventID").child_value();
        EventData::safeCopyString(event_id, MAX_EVENT_ID_LEN, event_id_value);

        // Get message
        auto message_value = event.getEventText();
        EventData::safeCopyString(message, MAX_MESSAGE_LEN, message_value);
        if (!message[0]) {
            EventData::safeCopyString(message, MAX_MESSAGE_LEN, "(no event message given)");
        }

        // Get and process timestamp
        auto time_field = event.getXmlDoc().child("Event").child("System")
            .child("TimeCreated").attribute("SystemTime").value();

        int year, month, day, hour, minute, second;
        int microsecs = 0;
        if (sscanf_s(time_field, "%d-%d-%dT%d:%d:%d.%dZ",
            &year, &month, &day, &hour, &minute, &second, &microsecs) >= 6) {

            struct tm tm = {};
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = minute;
            tm.tm_sec = second;
            time_t time = mktime(&tm);
            time -= config.getUtcOffsetMinutes() * 60;

            snprintf(timestamp, MAX_TIMESTAMP_LEN, "%ld", static_cast<long>(time));
            snprintf(microsec, MAX_MICROSEC_LEN, "%06d", microsecs);
        }

        // Get severity
        if (config.getSeverity() == SharedConstants::Severities::DYNAMIC) {
            auto level = event.getXmlDoc().child("Event").child("System")
                .child("Level").child_value();
            // Convert wide character to narrow using proper conversion
            char level_char = '\0';
            if (level && level[0]) {
                char temp[2] = {0};
                WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(level), 1, temp, sizeof(temp), nullptr, nullptr);
                level_char = temp[0];
            }
            severity = level_char ? unixSeverityFromWindowsSeverity(level_char)
                : SharedConstants::Severities::NOTICE;
        }
        else {
            severity = static_cast<unsigned char>(config.getSeverity());
        }

        // Parse event data fields
        event_data_count = 0;  // Reset counter
        pugi::xml_node event_data_node = event.getXmlDoc().child("Event").child("EventData");
        for (pugi::xml_node data_item = event_data_node.first_child(); data_item;
            data_item = data_item.next_sibling()) {
            auto data_name = data_item.attribute("Name").value();
            if (data_name && data_name[0]) {
                addEventData(data_name, data_item.child_value());
            }
        }
    }

    size_t EventHandlerMessageQueuer::estimateMessageSize(
        const EventHandlerMessageQueuer::EventData& data, int logformat) const
    {
        size_t estimated_size = 0;

        // Base JSON structure
        estimated_size += 2;  // Opening and closing braces

        // Root level fields
        if (!this->configuration_.getHostName().empty()) {
            estimated_size += 10 + this->configuration_.getHostName().length();  // "host": ""
        }
        estimated_size += 12 + strlen(data.provider);  // "program": ""
        estimated_size += 12 + strlen(data.message);   // "message": ""

        // extra_fields object
        estimated_size += 20;  // ", "extra_fields": {"
        estimated_size += 50;  // Basic fields (severity, facility, etc)
        estimated_size += strlen(data.event_id) + strlen(log_name_utf8_) + 30;

        if (data.timestamp[0] != '\0') {
            estimated_size += strlen(data.timestamp) + strlen(data.microsec) + 25;
        }

        // Event data fields
        for (size_t i = 0; i < data.event_data_count; i++) {
            if (data.event_data[i].used) {
                estimated_size += strlen(data.event_data[i].key) + strlen(data.event_data[i].value) + ESTIMATED_FIELD_OVERHEAD;
            }
        }

        // Suffix if present
        if (!configuration_.getSuffix().empty()) {
            estimated_size += strlen(suffix_utf8_);
        }

        return estimated_size;
    }

    bool EventHandlerMessageQueuer::generateLogMessage(
        EventLogEvent& event, const int logformat, char* json_buffer, size_t buflen)
    {
        EventHandlerMessageQueuer::EventData data;
        data.parseFrom(event, this->configuration_);
        return this->generateJson(data, logformat, json_buffer, buflen);
    }

    bool EventHandlerMessageQueuer::generateJson(
        const EventHandlerMessageQueuer::EventData& data, int logformat, char* json_buffer,
        size_t buflen)
    {
        // Use stack-based buffer for stream
        OStreamBuf ostream_buffer(json_buffer, buflen);
        std::ostream json_output(&ostream_buffer);

        // Function to check buffer space and log warning if needed
        auto checkBufferSpace = [&](const char* field_name, size_t needed_space) -> bool {
            size_t current_len = strlen(json_buffer);
            if (current_len + needed_space >= buflen) {
                Logger::warning("Buffer overflow prevented: current %zu + needed %zu would exceed buffer size %zu while adding %s",
                    current_len, needed_space, buflen, field_name);
                return false;
            }
            return true;
            };

        // Start JSON object
        json_output << "{";

        // Add root level fields
        string hostname = configuration_.getHostName();

        // http ingestion will accept these at root
        if (!hostname.empty()) {
            if (!checkBufferSpace("hostname", hostname.length() + 10)) {
                return false;
            }
            json_output << "\"host\":\"" << hostname << "\",";
        }

        // Add program and timestamp for HTTP format
        if (!checkBufferSpace("program", strlen(data.provider) + 20)) {
            return false;
        }
        json_output << "\"program\":\"" << data.provider << "\"";


        // don't know if http format will accept this directly, json won't, but current
        // rule takes ts from extra_fields anyway
        //if (logformat == SharedConstants::LOGFORMAT_HTTPPORT && data.timestamp[0] != '\0') {
        //    if (!checkBufferSpace("timestamp", strlen(data.timestamp) + strlen(data.microsec) + 40)) {
        //        return false;
        //    }
        //    json_output << ", \"first_occurrence\": " << data.timestamp << "." << data.microsec;
        //}

        json_output << ", ";
        // Start extra_fields for HTTP format
        if (logformat == SharedConstants::LOGFORMAT_HTTPPORT) {
            json_output << "\"extra_fields\": {";
        }
        json_output << "\"_source_type\": \"WindowsAgent\""
            << ", \"_source_tag\":\"windows_agent\""
            << ", \"_log_type\":\"eventlog\""
            << ", \"event_id\":\"" << data.event_id << "\""
            << ", \"event_log\":\"" << log_name_utf8_ << "\"";
        json_output << ", \"severity\":\"" << static_cast<unsigned int>(data.severity) << "\""
            << ", \"facility\":\"" << configuration_.getFacility() << "\"";
        //if (data.timestamp[0] != '\0') {
        //    if (!checkBufferSpace("timestamp", strlen(data.timestamp) + strlen(data.microsec) + 40)) {
        //        return false;
        //    }
        //    json_output << ", \"ts\": \"" << data.timestamp << "." << data.microsec << "\"";
        //}

        if (logformat == SharedConstants::LOGFORMAT_HTTPPORT) {
            // rule wants these two inside extra_fields
            if (!hostname.empty()) {
                if (!checkBufferSpace("hostname", hostname.length() + 10)) {
                    return false;
                }
                json_output << ", \"host\":\"" << hostname << "\",";
            }

            // Add program and timestamp for HTTP format
            if (!checkBufferSpace("program", strlen(data.provider) + 20)) {
                return false;
            }
            json_output << "\"program\":\"" << data.provider << "\"";
        }


        StatefulLogger::setEventId(data.event_id);
        StatefulLogger::setEventLog(log_name_utf8_);
        StatefulLogger::setEventDatetime(data.timestamp);

        // Add event data fields
        if (data.event_data_count > 0) {
            for (size_t i = 0; i < data.event_data_count; i++) {
                if (!data.event_data[i].used) continue;

                const size_t field_size = strlen(data.event_data[i].key) + strlen(data.event_data[i].value) + ESTIMATED_FIELD_OVERHEAD;
                if (!checkBufferSpace(data.event_data[i].key, field_size)) {
                    break;
                }

                char* escaped_name = Globals::instance()->getMessageBuffer("jsonEscapeName");
                char* escaped_value = Globals::instance()->getMessageBuffer("jsonEscapeValue");

                Util::jsonEscapeString(data.event_data[i].key, escaped_name, Globals::MESSAGE_BUFFER_SIZE);
                Util::jsonEscapeString(data.event_data[i].value, escaped_value, Globals::MESSAGE_BUFFER_SIZE);

                json_output << ", \"" << escaped_name << "\":\"" << escaped_value << "\"";

                Globals::instance()->releaseMessageBuffer(escaped_name);
                Globals::instance()->releaseMessageBuffer(escaped_value);
            }
        }

        // Add custom_suffix key-values to extra_fields if present
        if (!configuration_.getSuffix().empty()) {
            wstring suffix = configuration_.getSuffix();
            string suffix_utf8(suffix.begin(), suffix.end());
            json_output << ", " << suffix_utf8;  // Already in "key":"value" format
        }

        // Add message field
        size_t msg_len = strlen(data.message);
        char* msg_buf = Globals::instance()->getMessageBuffer("jsonEscapeMessage");

        size_t current_pos = static_cast<size_t>(ostream_buffer.pubseekoff(0, ios_base::cur));
        size_t remaining_space = buflen - current_pos;

        size_t overhead = strlen(", \"message\":\"") + 2;
        if (remaining_space <= overhead) {
            Logger::recoverable_error("No space left for message field - buffer position %zu/%zu\n",
                current_pos, buflen);
            Globals::instance()->releaseMessageBuffer(msg_buf);
            return false;
        }

        remaining_space -= overhead;

        if (msg_len <= remaining_space) {
            Util::jsonEscapeString(data.message, msg_buf, Globals::MESSAGE_BUFFER_SIZE);
        }
        else {
            const char* truncation_suffix = " *(message truncated)*";
            size_t suffix_len = strlen(truncation_suffix);
            size_t max_msg_len = remaining_space > suffix_len ? remaining_space - suffix_len : 0;

            if (max_msg_len > 0) {
                char* temp_buf = Globals::instance()->getMessageBuffer("tempMessage");
                strncpy_s(temp_buf, Globals::MESSAGE_BUFFER_SIZE, data.message, max_msg_len);
                temp_buf[max_msg_len] = '\0';
                strcat_s(temp_buf, Globals::MESSAGE_BUFFER_SIZE, truncation_suffix);

                Util::jsonEscapeString(temp_buf, msg_buf, Globals::MESSAGE_BUFFER_SIZE);
                Globals::instance()->releaseMessageBuffer(temp_buf);

                Logger::warning("Message truncated from %zu to %zu characters\n", msg_len, max_msg_len);
            }
            else {
                Logger::recoverable_error("No space left for message content - buffer position %zu/%zu\n",
                    current_pos, buflen);
                Globals::instance()->releaseMessageBuffer(msg_buf);
                return false;
            }
        }

        json_output << ", \"message\":\"" << msg_buf << "\"";

        if (logformat == SharedConstants::LOGFORMAT_HTTPPORT) {

        // now put message outside extra_fields as well...
        // i know, this sucks, it's just the way the lz appstore app is written
        json_output << "}, \"message\":\"" << msg_buf << "\""; 
        Globals::instance()->releaseMessageBuffer(msg_buf);
        }

        // Close the JSON object
        json_output << "}" << std::ends;

        return true;
    }

    EventHandlerMessageQueuer::EventHandlerMessageQueuer(
        Configuration& configuration,
        shared_ptr<MessageQueue> primary_message_queue,
        shared_ptr<MessageQueue> secondary_message_queue,
        const wchar_t* log_name)
        : configuration_(configuration),
        primary_message_queue_(primary_message_queue),
        secondary_message_queue_(secondary_message_queue)
    {
        DWORD chars_written;
        size_t length = wcslen(log_name);
        if (length > INT_MAX) {
            throw std::runtime_error("Log name too long");
        }

        chars_written = WideCharToMultiByte(CP_UTF8, 0, log_name,
            static_cast<int>(length), log_name_utf8_, sizeof(log_name_utf8_) - 1,
            NULL, NULL);

        if (chars_written == 0) {
            throw std::runtime_error("Failed to convert log name to UTF-8");
        }
        log_name_utf8_[chars_written] = 0;

        if (!configuration_.getSuffix().empty()) {
            auto suffix = configuration_.getSuffix();
            if (suffix.length() > SharedConstants::MAX_SUFFIX_LENGTH - 1) {
                strcpy_s(suffix_utf8_, sizeof(suffix_utf8_),
                    "\"error_suffix\": \"too long\"");
            }
            else {
                chars_written = WideCharToMultiByte(CP_UTF8, 0,
                    suffix.c_str(), static_cast<int>(suffix.length()),
                    suffix_utf8_, sizeof(suffix_utf8_) - 1, NULL, NULL);

                if (chars_written == 0) {
                    throw std::runtime_error("Failed to convert suffix to UTF-8");
                }
                suffix_utf8_[chars_written] = 0;
            }
        }
        else {
            suffix_utf8_[0] = 0;
        }
    }

    Result EventHandlerMessageQueuer::handleEvent(
        const wchar_t* subscription_name, EventLogEvent& event)
    {
        Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Processing event from subscription %S\n",
            subscription_name);

        event.renderEvent();
        Logger::debug3("EventHandlerMessageQueuer::handleEvent()> Event rendered\n");

        char* json_buffer = Globals::instance()->getMessageBuffer("EventHandlerMessageQueuer::handleEvent()");
        if (!json_buffer) {
            Logger::recoverable_error("Failed to allocate JSON buffer for event from %S\n", subscription_name);
            return Result(ERROR_OUTOFMEMORY);
        }

        Logger::debug3("EventHandlerMessageQueuer::handleEvent()> Buffer allocated\n");

        try {
            int primary_logformat = configuration_.getPrimaryLogformat();
            Logger::debug3("EventHandlerMessageQueuer::handleEvent()> Using primary format %d\n",
                primary_logformat);
            string eventJsonString = "Message: " + XMLToJSONConverter::escapeJSONString(string(event.getEventText())) + " (" + XMLToJSONConverter::convert(event.getEventXml()) + ")";
            EventLogger::log(EventLogger::LogDestination::SubscribedEvents,
                "Event from %S received: %s\n", subscription_name, eventJsonString.c_str());
            if (generateLogMessage(event, primary_logformat, json_buffer,
                Globals::MESSAGE_BUFFER_SIZE)) {
                EventLogger::log(EventLogger::LogDestination::GeneratedEvents,
                    "Event from %S generated: %s\n", subscription_name, eventJsonString.c_str());

                Logger::debug3("EventHandlerMessageQueuer::handleEvent()> Generated message: %s\n",
                    json_buffer);
                StatefulLogger::logEvent();

                while (!primary_message_queue_->enqueue(json_buffer, static_cast<int>(strlen(json_buffer)))) {
                    Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Primary queue full, removing front message\n");
                    primary_message_queue_->removeFront();
                }

                EventLogger::enqueueEventForLogging(eventJsonString);
                Globals::instance()->queued_count_++;
                Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Message enqueued to primary queue\n");

                if (secondary_message_queue_) {
                    bool have_secondary_message = true;
                    int secondary_logformat = configuration_.getSecondaryLogformat();
                    Logger::debug3("EventHandlerMessageQueuer::handleEvent()> Using secondary format %d\n",
                        secondary_logformat);

                    if (primary_logformat != secondary_logformat) {
                        have_secondary_message = generateLogMessage(event,
                            secondary_logformat, json_buffer,
                            Globals::MESSAGE_BUFFER_SIZE);
                    }

                    if (have_secondary_message) {
                        while (!secondary_message_queue_->enqueue(json_buffer, static_cast<int>(strlen(json_buffer)))) {
                            Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Secondary queue full, removing front message\n");
                            secondary_message_queue_->removeFront();
                        }
                        secondary_message_queue_->enqueue(json_buffer, static_cast<int>(strlen(json_buffer)));
                        Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Message enqueued to secondary queue\n");
                    }
                    else {
                        Logger::warning("EventHandlerMessageQueuer::handleEvent()> Secondary message generation failed for event from %S\n",
                            subscription_name);
                    }
                }
            }
            else {
                Logger::warning("EventHandlerMessageQueuer::handleEvent()> Primary message generation failed for event from %S\n",
                    subscription_name);
            }
        }
        catch (const std::exception& e) {
            Logger::critical("Exception in handleEvent for %S: %s\n", subscription_name, e.what());
            Globals::instance()->releaseMessageBuffer(json_buffer);
            return Result(ERROR_INVALID_DATA);
        }

        Globals::instance()->releaseMessageBuffer(json_buffer);
        Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Successfully processed event from %S\n",
            subscription_name);
        return Result(static_cast<DWORD>(ERROR_SUCCESS));
    }

    unsigned char EventHandlerMessageQueuer::unixSeverityFromWindowsSeverity(
        char windows_severity_num) {

        switch (windows_severity_num) {
        case '0': // "LogAlways"
            return SharedConstants::Severities::NOTICE;
        case '1': // "Critical"
            return SharedConstants::Severities::CRITICAL;
        case '2': // "Error"
            return SharedConstants::Severities::ERR;
        case '3': // "Warning"
            return SharedConstants::Severities::WARNING;
        case '4': // "Informational"
            return SharedConstants::Severities::INFORMATIONAL;
        case '5': // "Verbose"
            return SharedConstants::Severities::DEBUG;
        default:
            return SharedConstants::Severities::NOTICE;
        }
    }

} // namespace Syslog_agent