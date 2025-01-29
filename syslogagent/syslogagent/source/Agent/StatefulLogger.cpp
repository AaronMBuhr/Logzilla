#include "stdafx.h"
#include <cstring>
#include "StatefulLogger.h"
#include "Util.h"

using namespace Syslog_agent;

StatefulLogger* StatefulLogger::singleton() {
    static StatefulLogger instance;
    return &instance;
}

void StatefulLogger::setEventId(const char* eventId) {
    if (!eventId) return;
    strncpy_s(StatefulLogger::singleton()->current_event_id_, MAX_EVENT_ID_LENGTH, eventId, _TRUNCATE);
}

void StatefulLogger::setEventLog(const char* eventLog) {
    if (!eventLog) return;
    strncpy_s(StatefulLogger::singleton()->current_event_log_, MAX_EVENT_LOG_LENGTH, eventLog, _TRUNCATE);
}

void StatefulLogger::setEventDatetime(const char* eventDatetime) {
    if (!eventDatetime) return;
    strncpy_s(StatefulLogger::singleton()->current_event_datetime_, MAX_EVENT_DATETIME_LENGTH, eventDatetime, _TRUNCATE);
}

void StatefulLogger::logEvent() {
    char buf[256];
	Util::epochToDateTime(StatefulLogger::singleton()->current_event_datetime_, buf);
    Logger::log(Logger::DEBUG2, "Queuing Event ID: %s, Event Log: %s, Event Datetime: %s (%s)\n", 
        StatefulLogger::singleton()->current_event_id_, 
        StatefulLogger::singleton()->current_event_log_, 
        buf,  // Use the converted datetime string
        StatefulLogger::singleton()->current_event_datetime_);
}
