#include "stdafx.h"
#include "EventLogger.h"
#include "Globals.h"
#include "Logger.h"
#include "Util.h"
#include <chrono>
#include <ctime>
#include <string>

using std::string;

namespace Syslog_agent {

EventLogger* EventLogger::instance_ = nullptr;

EventLogger::~EventLogger() {
    // Clean up any remaining message buffers in the queue
    std::lock_guard<std::mutex> lock(logger_lock_);
    while (!_queued_events_to_log.empty()) {
        LoggedEvent& event = _queued_events_to_log.front();
        Globals::instance()->releaseMessageBuffer(event.message_buffer);
        _queued_events_to_log.pop();
    }
}

EventLogger* EventLogger::singleton() {
    if (instance_ == nullptr) {
        instance_ = new EventLogger();
    }
    return instance_;
}

const wstring& EventLogger::getFilenameForDestination(const LogDestination dest) const {
    switch (dest) {
        case LogDestination::SubscribedEvents:
            return SUBSCRIBED_EVENTS_FILENAME;
        case LogDestination::GeneratedEvents:
            return GENERATED_EVENTS_FILENAME;
        case LogDestination::SentEvents:
            return SENT_EVENTS_FILENAME;
        default:
            return SUBSCRIBED_EVENTS_FILENAME;
    }
}

void EventLogger::writeToFile(const LogDestination dest, const char* message) {
    try {
        const wstring& filename = getFilenameForDestination(dest);
        std::ofstream file(filename, std::ios::app);
        if (file.is_open()) {
            file << message;
            file.close();
        }
    }
    catch (const std::exception& e) {
        Logger::recoverable_error("EventLogger::writeToFile()> Exception: %s\n", e.what());
    }
}

bool EventLogger::log(const LogDestination dest, const char* format, ...) {
    try {
        char buffer[4096];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        auto instance = singleton();
        std::lock_guard<std::mutex> lock(instance->logger_lock_);
        instance->writeToFile(dest, buffer);
        return true;
    }
    catch (const std::exception& e) {
        Logger::recoverable_error("EventLogger::log()> Exception: %s\n", e.what());
        return false;
    }
}

void EventLogger::enqueueEventForLogging(const string& event) {
    auto instance = singleton();
    std::lock_guard<std::mutex> lock(instance->logger_lock_);
    
    LoggedEvent event_struct;
    event_struct.message_buffer = Globals::instance()->getMessageBuffer("EventLogger::enqueueEventForLogging()");
    if (!event_struct.message_buffer) {
        Logger::recoverable_error("EventLogger::enqueueEventForLogging()> Failed to get message buffer\n");
        return;
    }

    memcpy(event_struct.message_buffer, event.c_str(), event.length());
    event_struct.data_length = (uint32_t)event.length();
    auto now = std::chrono::system_clock::now();
    event_struct.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    instance->_queued_events_to_log.push(event_struct);
}

string EventLogger::queuePopFront() {
    auto instance = singleton();
    std::lock_guard<std::mutex> lock(instance->logger_lock_);
    
    if (instance->_queued_events_to_log.empty()) {
        return "";
    }

    LoggedEvent event_struct = instance->_queued_events_to_log.front();
    instance->_queued_events_to_log.pop();
    
    string result(event_struct.message_buffer, event_struct.data_length);
    Globals::instance()->releaseMessageBuffer(event_struct.message_buffer);
    return result;
}

bool EventLogger::queueEmpty() {
    auto instance = singleton();
    std::lock_guard<std::mutex> lock(instance->logger_lock_);
    return instance->_queued_events_to_log.empty();
}

} // namespace Syslog_agent
