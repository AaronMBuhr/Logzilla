#pragma once
#include "ChannelEventHandlerBase.h"
#include "Configuration.h"
#include "MessageQueue.h"
#include "Result.h"
#include "SyslogAgentSharedConstants.h"

namespace Syslog_agent {
	class EventHandlerMessageQueuer : public ChannelEventHandlerBase {
	public:
		EventHandlerMessageQueuer(
			Configuration& configuration,
			MessageQueue& message_queue,
			const wchar_t* log_name);
		Result handleEvent(const wchar_t* subscription_name, EventLogEvent& event) override;
	private:
		bool generateLogMessage(EventLogEvent& event, char* json_buffer, size_t buflen); // returns true if generated
		unsigned char unixSeverityFromWindowsSeverity(char windows_severity_num);
		Configuration& configuration_;
		MessageQueue& message_queue_;
		char log_name_utf8_[1000];
		char suffix_utf8_[SYSLOGAGENT_MAX_SUFFIX_LENGTH];
	};
}