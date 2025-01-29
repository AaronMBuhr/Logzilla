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
        severity = level[0] ? unixSeverityFromWindowsSeverity(level[0]) 
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
    
    FormatLevel initial_level;
    const size_t estimated_size = this->estimateMessageSize(data, logformat);
    
    if (estimated_size >= buflen) {
        Logger::warn("Message size estimate %zu exceeds buffer size %zu, using MINIMUM format",
            estimated_size, buflen);
        initial_level = FormatLevel::MINIMUM;
    }
    else {
        const double threshold = static_cast<double>(buflen) * BUFFER_WARNING_THRESHOLD * 100;
        if (static_cast<double>(estimated_size) >= threshold) {
            Logger::warn("Message size estimate %zu approaching buffer size %zu, using TRUNCATED format",
                estimated_size, buflen);
            initial_level = FormatLevel::TRUNCATED_MSG;
        }
        else {
            initial_level = FormatLevel::FULL;
        }
    }

    return this->tryGenerateJson(data, logformat, json_buffer, buflen, initial_level);
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

        if (generateLogMessage(event, primary_logformat, json_buffer, 
            Globals::MESSAGE_BUFFER_SIZE)) {
            
            Logger::debug3("EventHandlerMessageQueuer::handleEvent()> Generated message: %s\n", 
                json_buffer);
			StatefulLogger::logEvent();
            
            while (primary_message_queue_->isFull()) {
                Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Primary queue full, removing front message\n");
                primary_message_queue_->removeFront();
            }
            
            primary_message_queue_->enqueue(json_buffer, 
                static_cast<int>(strlen(json_buffer)));
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
                    while (secondary_message_queue_->isFull()) {
                        Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Secondary queue full, removing front message\n");
                        secondary_message_queue_->removeFront();
                    }
                    
                    secondary_message_queue_->enqueue(json_buffer, 
                        static_cast<int>(strlen(json_buffer)));
                    Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Message enqueued to secondary queue\n");
                }
                else {
                    Logger::warn("EventHandlerMessageQueuer::handleEvent()> Secondary message generation failed for event from %S\n", 
                        subscription_name);
                }
            }
        }
        else {
            Logger::warn("EventHandlerMessageQueuer::handleEvent()> Primary message generation failed for event from %S\n", 
                subscription_name);
        }
    }
    catch (const std::exception& e) {
        Logger::recoverable_error("Exception in handleEvent for %S: %s\n", subscription_name, e.what());
        Globals::instance()->releaseMessageBuffer(json_buffer);
        return Result(ERROR_INVALID_DATA);
    }

    Globals::instance()->releaseMessageBuffer(json_buffer);
    Logger::debug2("EventHandlerMessageQueuer::handleEvent()> Successfully processed event from %S\n", 
        subscription_name);
    return Result(static_cast<DWORD>(ERROR_SUCCESS));
}

bool EventHandlerMessageQueuer::tryGenerateJson(
    const EventHandlerMessageQueuer::EventData& data, int logformat, char* json_buffer, 
    size_t buflen, FormatLevel level)
{
    // Use stack-based buffer for stream
    OStreamBuf ostream_buffer(json_buffer, buflen);
    std::ostream json_output(&ostream_buffer);

    // Function to check buffer space and log warning if needed
    auto checkBufferSpace = [&](const char* field_name, size_t needed_space) -> bool {
        size_t current_len = strlen(json_buffer);
        if (current_len + needed_space >= buflen) {
            Logger::recoverable_error("Buffer overflow prevented: current %zu + needed %zu would exceed buffer size %zu while adding %s", 
                current_len, needed_space, buflen, field_name);
            return false;
        }
        return true;
    };



    // Start JSON object
    json_output << "{";

    // Add host and program to main JSON structure
    string hostname = configuration_.getHostName();

 //   // DEBUGGING
 //   json_output << "\"_source_type\": \"WindowsAgent\","
	//	<< "\"_source_tag\": \"windows_agent\","
 //       << "\"_log_type\": \"eventlog\",";

 //   json_output
 //       << "\"host\": \"TESTHOST04\","
 //       << "\"program\": \"testprogram04\","
 //       << "\"event_id\": \"11111\","
 //       << "\"event_log\": \"testlog04\","
 //       //<< "\"severity\": \"1" << (data.severity + '0') << "\","
 //       //<< "\"facility\": \"1" << configuration_.getFacility() << "\","
 //       << "\"message\": \"EventID=444\"";

 //   // json_output << ",\"extra_fields\": { \"host\": \"host01\", \"program\": \"program01\" , \"_source_type\": \"WindowsAgent\", \"_source_tag\": \"windows_agent\" } }" << (char)10;
 //   // json_output << ",\"extra_fields\": { \"host\": \"host01\", \"program\": \"program01\" } }" << (char)10;
	//json_output << "}" << (char)10;


 //   goto skip;

    if (!hostname.empty()) {
        if (!checkBufferSpace("hostname", hostname.length() + 10)) {
            return false;
        }
        json_output << "\"host\":\"" << hostname << "\",";
    }

    // Add program
    if (!checkBufferSpace("program", strlen(data.provider) + 20)) {
        return false;
    }
    json_output << "\"program\":\"" << data.provider << "\"";

    json_output 
        << ", \"severity\":\"" << static_cast<unsigned int>(data.severity) << "\""
        << ", \"facility\":\"" << configuration_.getFacility() << "\""
        << ", \"_source_type\": \"WindowsAgent\""
        << ", \"_source_tag\":\"windows_agent\""
        << ", \"log_type\":\"eventlog\""
        << ", \"event_id\":\"" << data.event_id << "\""
        << ", \"event_log\":\"" << log_name_utf8_ << "\"";
    //json_output
    //    << ", \"_source_type\": \"WindowsAgent\""
    //    << ", \"_source_tag\":\"windows_agent\""
    //    << ", \"log_type\":\"eventlog\""
    //    << ", \"event_id\":\"" << data.event_id << "\""
    //    << ", \"event_log\":\"" << log_name_utf8_ << "\"";






    size_t msg_len = strlen(data.message);
    char* msg_buf = nullptr;
        
    // Calculate space needed for remaining fields after message
    size_t remaining_size = 200;  // Basic JSON structure and formatting
    remaining_size += strlen(data.event_id);
    remaining_size += strlen(log_name_utf8_);
        
    // Account for timestamp if present
    if (data.timestamp[0] != '\0') {
        remaining_size += strlen(data.timestamp) + strlen(data.microsec) + 30;
    }
        
    // Account for suffix if present
    if (!configuration_.getSuffix().empty()) {
        remaining_size += strlen(suffix_utf8_) + 10;
    }
        
    // Account for event data fields if not in MINIMUM mode
    if (level != FormatLevel::MINIMUM && data.event_data_count > 0) {
        for (size_t i = 0; i < data.event_data_count; i++) {
            if (data.event_data[i].used) {
                remaining_size += strlen(data.event_data[i].key) + strlen(data.event_data[i].value) + ESTIMATED_FIELD_OVERHEAD;
            }
        }
    }
        
    // Add 20% safety margin for JSON escaping and formatting
    remaining_size = static_cast<size_t>(remaining_size * 1.2);
        
    // Get current buffer position
    auto current_pos = static_cast<size_t>(ostream_buffer.pubseekoff(0, ios_base::cur));
        
    // Calculate maximum safe message length
    size_t max_safe_msg_len = 0;
    if (current_pos + remaining_size < buflen) {
        max_safe_msg_len = buflen - (current_pos + remaining_size);
            
        // For TRUNCATED_MSG level, add truncation notice
        if (level == FormatLevel::TRUNCATED_MSG && msg_len > max_safe_msg_len) {
            char truncate_prefix[100];
            snprintf(truncate_prefix, sizeof(truncate_prefix), 
                "(message truncated: %zu bytes requested, %zu bytes available) ", 
                msg_len, max_safe_msg_len);
            size_t prefix_len = strlen(truncate_prefix);
                
            // Calculate what percentage of message we can keep
            double retention_percent = (max_safe_msg_len * 100.0) / msg_len;
                
            Logger::verbose("Message truncation: %zu bytes requested, %zu bytes available (%.1f%% retained) "
                "(buffer position: %zu/%zu, estimated remaining fields: %zu bytes)", 
                msg_len, max_safe_msg_len, retention_percent, current_pos, buflen, remaining_size);
                
            // Ensure we have space for prefix + at least some message content
            if (max_safe_msg_len > prefix_len + 50) {  // At least 50 chars of message
                max_safe_msg_len -= prefix_len;  // Reserve space for prefix
                msg_buf = Globals::instance()->getMessageBuffer("jsonEscapeMessage");
                    
                // Copy prefix with size information
                strcpy_s(msg_buf, Globals::MESSAGE_BUFFER_SIZE, truncate_prefix);
                    
                // Copy truncated portion of message
                size_t msg_content_len = min(msg_len, max_safe_msg_len - 1);
                strncpy_s(msg_buf + prefix_len, Globals::MESSAGE_BUFFER_SIZE - prefix_len, data.message, msg_content_len);
                msg_buf[prefix_len + msg_content_len] = '\0';
                    
                Logger::debug2("Message truncated from %zu to %zu bytes (including %zu byte prefix)", 
                    msg_len, prefix_len + msg_content_len, prefix_len);
            } else {
                // Not enough space for prefix + message, just use original truncation
                msg_buf = Globals::instance()->getMessageBuffer("jsonEscapeMessage");
                strncpy_s(msg_buf, Globals::MESSAGE_BUFFER_SIZE, data.message, max_safe_msg_len - 1);
                msg_buf[max_safe_msg_len - 1] = '\0';
                Logger::warn("Severe truncation: insufficient space for prefix. Message truncated from %zu to %zu bytes (%.1f%% retained)", 
                    msg_len, max_safe_msg_len, retention_percent);
            }
        } else {
            // No truncation needed or not in TRUNCATED_MSG mode
            msg_buf = Globals::instance()->getMessageBuffer("jsonEscapeMessage");
            Util::jsonEscapeString(data.message, msg_buf, Globals::MESSAGE_BUFFER_SIZE);
        }
    } else {
        // Buffer already too full, use minimal message
        msg_buf = Globals::instance()->getMessageBuffer("jsonEscapeMessage");
        strcpy_s(msg_buf, Globals::MESSAGE_BUFFER_SIZE, "(message omitted due to buffer constraints)");
        Logger::warn("Buffer constraints prevent including message content: "
            "buffer position %zu/%zu with %zu bytes needed for remaining fields", 
            current_pos, buflen, remaining_size);
    }
        
    if (!checkBufferSpace("message", strlen(msg_buf) + 50)) {  // Extra space for escaping
        Globals::instance()->releaseMessageBuffer(msg_buf);
        return false;
    }
        
    json_output << ", \"message\":\"" << msg_buf << "\"";
    Globals::instance()->releaseMessageBuffer(msg_buf);

    // Everything else goes in extra_fields
    if (!checkBufferSpace("extra_fields_start", 100)) {
        return false;
    }
    // json_output << ", \"_source_tag\":\"windows_agent\"";
    if (logformat == SharedConstants::LOGFORMAT_HTTPPORT) {
        json_output << ", \"extra_fields\":{ ";
    }
    //json_output << ", \"extra_fields\":{ "
    //    << "\"severity\":\"" << (data.severity + '0') << "\""
    //    << ", \"facility\":\"" << configuration_.getFacility() << "\""
    //    << ", \"_source_tag\":\"windows_agent\""
    //    << ", \"log_type\":\"eventlog\""
    //    << ", \"event_id\":\"" << data.event_id << "\""
    //    << ", \"event_log\":\"" << log_name_utf8_ << "\""
    //    << ", \"host\":\"" << hostname << "\""
    //    << ", \"program\":\"" << data.provider << "\"";


	StatefulLogger::setEventId(data.event_id);
	StatefulLogger::setEventLog(log_name_utf8_);
	StatefulLogger::setEventDatetime(data.timestamp);

    // Add event data fields to extra_fields
    if (level != FormatLevel::MINIMUM && data.event_data_count > 0) {
        for (size_t i = 0; i < data.event_data_count; i++) {
            if (!data.event_data[i].used) continue;
            
            const size_t field_size = strlen(data.event_data[i].key) + strlen(data.event_data[i].value) + ESTIMATED_FIELD_OVERHEAD;
            if (!checkBufferSpace(data.event_data[i].key, field_size)) {
                break;
            }

            // Use fixed buffer from pool for JSON escaping
            char* escaped_name = Globals::instance()->getMessageBuffer("jsonEscapeName");
            char* escaped_value = Globals::instance()->getMessageBuffer("jsonEscapeValue");
            
            Util::jsonEscapeString(data.event_data[i].key, escaped_name, Globals::MESSAGE_BUFFER_SIZE);
            Util::jsonEscapeString(data.event_data[i].value, escaped_value, Globals::MESSAGE_BUFFER_SIZE);
            
            json_output << ", \"" << escaped_name << "\":\"" << escaped_value << "\"";
            
            // Return buffers to pool
            Globals::instance()->releaseMessageBuffer(escaped_name);
            Globals::instance()->releaseMessageBuffer(escaped_value);
        }
    }

    // DEBUGGING
    //if (logformat == SharedConstants::LOGFORMAT_HTTPPORT && data.timestamp[0] != '\0') {
    //    if (!checkBufferSpace("timestamp", strlen(data.timestamp) + strlen(data.microsec) + 40)) {
    //        return false;
    //    }
    //    json_output << ", \"first_occurrence\":\"" << data.timestamp << "." << data.microsec << "\"";
    //}

    // For JSON port format, duplicate fields inside extra_fields
    if (logformat == SharedConstants::LOGFORMAT_JSONPORT) {
        if (!checkBufferSpace("json_extra_fields", 200)) {
            return false;
        }
        //json_output << ", \"program\":\"" << data.provider << "\"";
        //json_output << ", \"facility\":\"" << configuration_.getFacility() << "\"";
        //json_output << ", \"severity\":\"" << (data.severity + '0') << "\"";
        ////json_output << ", \"_source_type\":\"WindowsAgent\"";

        //if (data.timestamp[0] != '\0') {
        //    json_output << ", \"ts\":\"" << data.timestamp << "." << data.microsec << "\"";
        //}
        //if (!hostname.empty()) {
        //    json_output << ", \"host\":\"" << hostname << "\"";
        //}
        // json_output << ", \"message\":\"" << data.message << "\"";
    }

    // For HTTP format, suffix fields go inside extra_fields
    if (logformat != SharedConstants::LOGFORMAT_JSONPORT && !configuration_.getSuffix().empty()) {
        if (!checkBufferSpace("suffix", strlen(suffix_utf8_) + 10)) {
            return false;
        }
        json_output << ", " << suffix_utf8_;
    }

    if (logformat == SharedConstants::LOGFORMAT_HTTPPORT) {
        // Close extra_fields object
        json_output << " }";
    }

    // For JSON port format, suffix fields go at root level
    if (logformat == SharedConstants::LOGFORMAT_JSONPORT && !configuration_.getSuffix().empty()) {
        if (!checkBufferSpace("json_suffix", strlen(suffix_utf8_) + 10)) {
            return false;
        }
        json_output << ", " << suffix_utf8_;
    }

    // Close the main object
    json_output << " }";

    skip:

    json_output << (char)10 << (char)0;  // Add newline and null terminator
        
    // Check final buffer usage
    size_t final_pos = strlen(json_buffer);
    double usage_percent = (static_cast<double>(final_pos) * 100.0) / buflen;
    Logger::debug3("Final buffer usage: %zu/%zu bytes (%.1f%%)", 
        final_pos, buflen, usage_percent);
            
    // FATAL check - if we're within 1% of buffer size, something's wrong
    // (this was for debugging, taking it out for now)
    //if (static_cast<double>(final_pos) / buflen > BUFFER_WARNING_THRESHOLD) {
    //    Logger::fatal("CRITICAL: JSON buffer usage at %zu/%zu bytes (%.1f%%) - buffer nearly full! "
    //        "This indicates a serious issue with message sizing. Original message length: %zu, "
    //        "Format level: %d, Current message: %.200s", 
    //        final_pos, buflen, usage_percent,
    //        strlen(data.message), static_cast<int>(level), json_buffer);
    //    throw std::runtime_error("JSON buffer critically full");
    //}
            
    return static_cast<size_t>(final_pos) < buflen;
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