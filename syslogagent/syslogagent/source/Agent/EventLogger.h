#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <queue>
#include <cstdint>

using std::wstring;
using std::string;
using std::queue;

namespace Syslog_agent {

class EventLogger {
public:
    enum class LogDestination {
        SubscribedEvents,
        GeneratedEvents,
        SentEvents,
        SentData
    };

    static bool log(const LogDestination dest, const char* format, ...);
    static bool logNetworkSend(const char* buf, size_t length);
    static bool logNetworkReceive(const char* result, size_t result_length);
    static bool isQueueEmpty();
    static void enqueueEventForLogging(const string& event);
    static string queuePopFront();

    // Delete copy constructor and assignment operator
    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;
    // Delete move constructor and assignment operator
    EventLogger(EventLogger&&) = delete;
    EventLogger& operator=(EventLogger&&) = delete;
    
    ~EventLogger();

private:
    struct LoggedEvent {
        char* message_buffer;
        uint32_t data_length;
        uint64_t timestamp;
    };

    static EventLogger& singleton();
    static std::mutex logger_lock_;
    static std::unique_ptr<EventLogger, std::default_delete<EventLogger>> instance_;
    std::queue<LoggedEvent> _queued_events_to_log;

    // File names
    static const wstring SUBSCRIBED_EVENTS_FILENAME;
    static const wstring GENERATED_EVENTS_FILENAME;
    static const wstring SENT_EVENTS_FILENAME;
    static const wstring SENT_DATA_FILENAME;

    // Private constructor for singleton
    EventLogger() = default;

    // Helper methods
    static const wstring& getFilenameForDestination(const LogDestination dest);
    void writeToFile(const LogDestination dest, const char* message);
    void writeToFile(const LogDestination dest, const char* message, size_t length);
    void writeSentData(const char* data, size_t length);
};

} // namespace Syslog_agent