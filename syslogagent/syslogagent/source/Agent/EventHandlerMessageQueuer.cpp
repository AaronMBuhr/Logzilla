/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include <ctime>
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

#include "XMLToJsonConverter.h"

#if ONLY_FOR_DEBUGGING_CURRENTLY_DISABLED
#include "EventLogger.h"
#endif

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
        estimated_size += strlen(data.event_id) + log_name_utf8_.length() + 30;

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
            estimated_size += suffix_utf8_.length();
        }

        return estimated_size;
    }

    void epoch_to_datetime(std::time_t epoch, char* buffer, size_t bufsize) {
        if (bufsize < 20) return;  // Need 19 chars + null terminator
        
        // Convert to time struct
        std::tm* timeinfo = std::localtime(&epoch);
        
        // Format into buffer
        std::strftime(buffer, bufsize, "%Y-%m-%d %H:%M:%S", timeinfo);
    }

    Result EventHandlerMessageQueuer::generateLogMessage(
        EventLogEvent& event, const int logformat, char* json_buffer, size_t buflen)
    {
        auto logger = LOG_THIS;
        EventHandlerMessageQueuer::EventData data;
        data.parseFrom(event, this->configuration_);

        char* end;
        long event_timestamp_value = std::strtol(data.timestamp, &end, 10);

        if (*end == '\0') {
            // Successful conversion
            // Get seconds since epoch
            std::time_t now = std::time(nullptr);
            long earliest_allowed_timestamp = now - (SharedConstants::MAX_CATCHUP_DAYS * 24 * 60 * 60);
            char buffer[20];  // YYYY-MM-DD HH:MM:SS\0
            epoch_to_datetime(event_timestamp_value, buffer, sizeof(buffer));    
            if (event_timestamp_value < earliest_allowed_timestamp) {
                if (!skipping_dates_) {
                    skipping_dates_ = true;
                    logger->warning("Skipping events starting from %s\n", buffer);
                }
                return Result(ERROR_CANCELLED, "generateLogMessage", "Event too old, skipped.");
            }
            else {
                if (skipping_dates_) {
                    skipping_dates_ = false;
                    logger->info("End skipping dates starting at %s\n", buffer);
                }
            }
        } 

        if (this->generateJson(data, logformat, json_buffer, buflen)) {
			return Result(ERROR_SUCCESS, "generateLogMessage", "Successfully generated JSON message");
		}
        else {
            return Result(ERROR_INVALID_DATA, "generateLogMessage", "Failed to generate JSON message");
        }
    }

    bool EventHandlerMessageQueuer::generateJson(
        const EventHandlerMessageQueuer::EventData& data, int logformat, char* json_buffer,
        size_t buflen)
    {
        auto logger = LOG_THIS;
        // Use stack-based buffer for stream
        OStreamBuf ostream_buffer(json_buffer, buflen);
        std::ostream json_output(&ostream_buffer);

        auto checkBufferSpace = [&](const char* field_name, size_t needed_space) -> bool {
            std::streamoff current_len = ostream_buffer.current_length();
            if (static_cast<size_t>(current_len) + needed_space >= buflen) {
                logger->warning("Buffer overflow prevented: current %zu + needed %zu would exceed buffer size %zu while adding %s",
                    static_cast<size_t>(current_len), needed_space, buflen, field_name);
                return false;
            }
            return true;
            };

        // Start JSON object
        json_output << "{";

        // Add root level fields
        const string& hostname = configuration_.getHostName();

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
        if (data.timestamp[0] != '\0') {
           if (!checkBufferSpace("timestamp", strlen(data.timestamp) + strlen(data.microsec) + 40)) {
               return false;
           }
           json_output << ", \"ts\": \"" << data.timestamp << "." << data.microsec << "\"";
        }

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

#if ONLY_FOR_DEBUGGING_CURRENTLY_DISABLED
        StatefulLogger::setEventId(data.event_id);
        StatefulLogger::setEventLog(log_name_utf8_.c_str());
        StatefulLogger::setEventDatetime(data.timestamp);
#endif

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
        if (!suffix_utf8_.empty()) {
            json_output << ", " << suffix_utf8_;  // Already in "key":"value" format
        }

        // Add message field
        size_t msg_len = strlen(data.message);
        char* msg_buf = Globals::instance()->getMessageBuffer("jsonEscapeMessage");

        size_t current_pos = static_cast<size_t>(ostream_buffer.pubseekoff(0, ios_base::cur));
        size_t remaining_space = buflen - current_pos;

        size_t overhead = strlen(", \"message\":\"") + 2;
        if (remaining_space <= overhead) {
            logger->recoverable_error("No space left for message field - buffer position %zu/%zu\n",
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

                logger->warning("Message truncated from %zu to %zu characters\n", msg_len, max_msg_len);
            }
            else {
                logger->recoverable_error("No space left for message content - buffer position %zu/%zu\n",
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
        auto logger = LOG_THIS;
        DWORD chars_written;
        size_t length = wcslen(log_name);
        if (length > INT_MAX) {
            throw std::runtime_error("Log name too long");
        }

        log_name_utf8_.resize(SharedConstants::MAX_LOG_NAME_LENGTH + 1);
        chars_written = WideCharToMultiByte(CP_UTF8, 0, log_name,
            static_cast<int>(length), log_name_utf8_.data(), log_name_utf8_.size() - 1,
            NULL, NULL);

        if (chars_written == 0) {
            throw std::runtime_error("Failed to convert log name to UTF-8");
        }
        log_name_utf8_.resize(chars_written);

        if (!configuration_.getSuffix().empty()) {
            auto suffix = configuration_.getSuffix();
            if (suffix.length() > SharedConstants::MAX_SUFFIX_LENGTH - 1) {
                suffix_utf8_ = string("\"error_suffix\": \"too long\"");
            }
            else {
                suffix_utf8_.resize(SharedConstants::MAX_SUFFIX_LENGTH + 1);
                chars_written = Util::wstr2str_truncate(const_cast<char*>(suffix_utf8_.c_str()), suffix_utf8_.size(), suffix.c_str());
                if (chars_written == 0) {
                    suffix_utf8_ = string("\"error_suffix\": \"conversion failed\"");
                }
                else {
                    suffix_utf8_.resize(chars_written);
                }
            }
        }
    }

    Result EventHandlerMessageQueuer::handleEvent(
        const wchar_t* subscription_name, EventLogEvent& event)
    {
        auto logger = LOG_THIS;
        char* json_buffer = Globals::instance()->getMessageBuffer("eventHandlerMessageQueuer");

        try {
            // Estimate message size and check buffer capacity
            EventData data;
            event.renderEvent();
            data.parseFrom(event, configuration_);
            size_t estimated_size = estimateMessageSize(data, SharedConstants::LOGFORMAT_HTTPPORT);

            if (estimated_size > Globals::MESSAGE_BUFFER_SIZE) {
                logger->recoverable_error("Estimated message size %zu exceeds buffer size %zu\n",
                    estimated_size, Globals::MESSAGE_BUFFER_SIZE);
                Globals::instance()->releaseMessageBuffer(json_buffer);
                return Result(ERROR_INSUFFICIENT_BUFFER, "handleEvent", "Buffer too small");
            }

            // Generate JSON for primary queue
			Result generate_result = generateLogMessage(event, configuration_.getPrimaryLogformat(), json_buffer, Globals::MESSAGE_BUFFER_SIZE);
            if (generate_result.statusCode() != ERROR_SUCCESS) {
                Globals::instance()->releaseMessageBuffer(json_buffer);
                if (generate_result.statusCode() != ERROR_CANCELLED) {
					logger->recoverable_error("Failed to generate JSON for primary queue\n");
                }
                return generate_result;
            }

            // Queue message for primary server
            primary_message_queue_->enqueue(json_buffer, strlen(json_buffer));

            // Handle secondary server if configured
            if (configuration_.hasSecondaryHost()) {
                // Generate JSON for secondary queue
                generate_result = generateLogMessage(event, configuration_.getSecondaryLogformat(), json_buffer, Globals::MESSAGE_BUFFER_SIZE);
                if (generate_result.statusCode() != ERROR_SUCCESS) {
                    Globals::instance()->releaseMessageBuffer(json_buffer);
					if (generate_result.statusCode() != ERROR_CANCELLED) {
						logger->recoverable_error("Failed to generate JSON for secondary queue\n");
					}
                    return generate_result;
                }

                // Queue message for secondary server
                secondary_message_queue_->enqueue(json_buffer, strlen(json_buffer));
            }

            Globals::instance()->releaseMessageBuffer(json_buffer);
            return Result();
        }
        catch (const std::exception& e) {
            logger->recoverable_error("Exception in handleEvent: %s\n", e.what());
            Globals::instance()->releaseMessageBuffer(json_buffer);
            return Result(ERROR_INVALID_DATA, "handleEvent", e.what());
        }
    }

    unsigned char EventHandlerMessageQueuer::unixSeverityFromWindowsSeverity(
        char windows_severity_num)
    {
        auto logger = LOG_THIS;
        unsigned char severity;

        switch (windows_severity_num) {
		case '0':
			severity = SharedConstants::Severities::ALERT;
			break;
        case '1':
            severity = SharedConstants::Severities::CRITICAL;
            break;
        case '2':
            severity = SharedConstants::Severities::ERR;
            break;
        case '3':
            severity = SharedConstants::Severities::WARNING;
            break;
        case '4':
            severity = SharedConstants::Severities::NOTICE;
            break;
        case '5':
            severity = SharedConstants::Severities::DEBUG;
            break;
        default:
            logger->warning("Unknown Windows severity level: %c, defaulting to NOTICE\n", windows_severity_num);
            severity = SharedConstants::Severities::NOTICE;
        }

        return severity;
    }

} // namespace Syslog_agent