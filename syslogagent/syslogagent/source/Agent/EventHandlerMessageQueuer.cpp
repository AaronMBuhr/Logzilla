#include "stdafx.h"
#include <iomanip>
#include <locale>
#include <sstream>
#include "Debug.h"
#include "EventHandlerMessageQueuer.h"
#include "Globals.h"
#include "OStreamBuf.h"
#include "pugixml.hpp"
#include "SyslogSender.h"
#include "Util.h"

using namespace std;

namespace Syslog_agent {

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
		chars_written = WideCharToMultiByte(CP_UTF8, 0, log_name, wcslen(log_name), log_name_utf8_, 999, NULL, NULL);
		log_name_utf8_[chars_written] = 0;
		if (configuration_.suffix_.length() > 0) {
			if (configuration_.suffix_.length() > SYSLOGAGENT_MAX_SUFFIX_LENGTH) {
				strcpy_s(suffix_utf8_, SYSLOGAGENT_MAX_SUFFIX_LENGTH, "\"error_suffix\": \"too long\"");
			}
			else {
				chars_written = WideCharToMultiByte(CP_UTF8, 0, configuration_.suffix_.c_str(), configuration_.suffix_.length(), suffix_utf8_, 1000, NULL, NULL);
				suffix_utf8_[chars_written] = 0;
			}
		}
		else {
			suffix_utf8_[0] = 0;
		}
	}

	Result EventHandlerMessageQueuer::handleEvent(const wchar_t* subscription_name, EventLogEvent& event) {
		// TODO maybe different return results?
		event.renderEvent();
		char* json_buffer = Globals::instance()->getMessageBuffer("json_buffer");
		if (generateLogMessage(event, json_buffer, Globals::MESSAGE_BUFFER_SIZE)) {
#if !DEBUG_SETTINGS_SKIP_MESSAGEQUEUE
			primary_message_queue_->lock();
			if (primary_message_queue_->isFull()) {
				primary_message_queue_->removeFront();
			}
			primary_message_queue_->enqueue(json_buffer, (const int)strlen(json_buffer));
			primary_message_queue_->unlock();
			if (secondary_message_queue_ != nullptr) {
                secondary_message_queue_->lock();
				if (secondary_message_queue_->isFull()) {
                    secondary_message_queue_->removeFront();
                }
                secondary_message_queue_->enqueue(json_buffer, (const int)strlen(json_buffer));
                secondary_message_queue_->unlock();
            }
			SyslogSender::enqueue_event_.signal();
#endif
		}
		Globals::instance()->releaseMessageBuffer("json_buffer", json_buffer);
		return Result((DWORD)ERROR_SUCCESS);
	}

	unsigned char EventHandlerMessageQueuer::unixSeverityFromWindowsSeverity(char windows_severity_num) {
		unsigned char result = 0;
		switch (windows_severity_num) {
		case '0':
			// "LogAlways"
			result = SYSLOGAGENT_SEVERITY_NOTICE;
			break;
		case '1':
			// "Critical"
			result = SYSLOGAGENT_SEVERITY_CRITICAL;
			break;
		case '2':
			// "Error"
			result = SYSLOGAGENT_SEVERITY_ERROR;
			break;
		case '3':
			// "Warning":
			result = SYSLOGAGENT_SEVERITY_WARNING;
			break;
		case '4':
			// "Informational"
			result = SYSLOGAGENT_SEVERITY_INFORMATIONAL;
			break;
		case '5':
			// "Verbose"
			result = SYSLOGAGENT_SEVERITY_DEBUG;
			break;
		}
		return result;
	}


	bool EventHandlerMessageQueuer::generateLogMessage(EventLogEvent& event, char* json_buffer, size_t buflen) {

		// figure out some message details
		auto event_id_str = event.getXmlDoc().child("Event").child("System").child("EventID").child_value();
		DWORD event_id;
		sscanf_s(event_id_str, "%u", &event_id);
		if (configuration_.include_vs_ignore_eventids_) {
			if (configuration_.event_id_filter_.find(event_id) == configuration_.event_id_filter_.end()) {
                return false;
            }
		}
		else
		{
			if (configuration_.event_id_filter_.find(event_id) != configuration_.event_id_filter_.end()) {
				return false;
			}
		}
		char severity;
		if (configuration_.severity_ == SYSLOGAGENT_SEVERITY_DYNAMIC) {
			auto level = event.getXmlDoc().child("Event").child("System").child("Level").child_value();
			severity = unixSeverityFromWindowsSeverity(level[0]);
			// Logger::always("%s > %d : %s\n", time_field, event_id, level);
		}
		else {
			severity = configuration_.severity_;
		}

		auto time_field = event.getXmlDoc().child("Event").child("System").child("TimeCreated").attribute("SystemTime").value();
		time_t timestamp = 0;
		unsigned int decimal_time = 0;
#if !DEBUG_SETTINGS_SKIP_TIMESTAMP
		std::tm t = {};
		std::istringstream ss(time_field);
		if (ss >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S"))
		{
			timestamp = std::mktime(&t) - (60 * configuration_.utc_offset_minutes_);
			// timestamp = std::mktime(&t);
			auto time_period_pos = strstr(time_field, ".");
			if (time_period_pos != NULL) {
				sscanf_s(time_period_pos, ".%uZ", &decimal_time);
			}
		}
#endif
		auto provider = event.getXmlDoc().child("Event").child("System").child("Provider").attribute("Name").value();
		// generate json
		// we're going to copy the message text into the json so we need
		// to escape certain characters for valid json
		auto escaped_buf = Globals::instance()->getMessageBuffer("escaped_buf");
		Util::jsonEscape(event.getEventText(), escaped_buf, Globals::MESSAGE_BUFFER_SIZE);
		OStreamBuf<char> ostream_buffer(json_buffer, buflen);
		ostream json_output(&ostream_buffer);
		json_output.fill('0');
		json_output << "{"
			<< " \"_source_type\": \"WindowsAgent\","
			<< " \"_log_type\": \"eventlog\",";
		if (timestamp != 0) {
			json_output << " \"ts\": " << timestamp << "." << decimal_time << ",";
		}
		json_output
			<< " \"host\": \"" << configuration_.host_name_ << "\","
			<< " \"program\": \"" << provider << "\","
			<< " \"event_id\": \"" << event_id_str << "\","
			<< " \"event_log\": \"" << log_name_utf8_ << "\","
			<< " \"severity\": " << ((char)(severity + '0')) << ","
			<< " \"facility\": " << configuration_.facility_ << ","
			<< " \"message\": \""
			<< "EventID=\\\"" << event_id_str << "\\\""
			<< " EventLog=\\\"" << log_name_utf8_ << "\\\"\\r\\n"
			<< escaped_buf << "\" ";
		pugi::xml_node event_data = event.getXmlDoc().child("Event").child("EventData");
		for (pugi::xml_node data_item = event_data.first_child(); data_item; data_item = data_item.next_sibling()) {
			auto data_name = data_item.attribute("Name").value();
			if (data_name[0] != 0) {
				auto value = data_item.child_value();
				// just in case there's any chars in value to escape:
				Util::jsonEscape((char*)value, escaped_buf, Globals::MESSAGE_BUFFER_SIZE - 1);
				json_output << ", \"" << data_name << "\": \"" << escaped_buf << "\"";
			}
		}
		Globals::instance()->releaseMessageBuffer("escaped_buf", escaped_buf);
		if (suffix_utf8_[0] != 0) {
			json_output << "," << suffix_utf8_;
		}
		json_output << " }" << (char)10 << (char)0;

		return true;
	}
}
