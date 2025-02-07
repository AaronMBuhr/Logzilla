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
        SentEvents
    };

    static bool log(const LogDestination dest, const char* format, ...);
    static void enqueueEventForLogging(const string& event);
    static string queuePopFront();
    static bool queueEmpty();

    // Delete copy constructor and assignment operator
    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;

private:
    struct LoggedEvent {
        char* message_buffer;
        uint32_t data_length;
        uint64_t timestamp;
    };

    static EventLogger* singleton();
    static EventLogger* instance_;
    std::mutex logger_lock_;
    std::queue<LoggedEvent> _queued_events_to_log;

    const wstring SUBSCRIBED_EVENTS_FILENAME = L"subscribed_events.txt";
    const wstring GENERATED_EVENTS_FILENAME = L"generated_events.txt";
    const wstring SENT_EVENTS_FILENAME = L"sent_events.txt";

    // Private constructor for singleton
    EventLogger() = default;
    ~EventLogger();

    const wstring& getFilenameForDestination(const LogDestination dest) const;
    void writeToFile(const LogDestination dest, const char* message);
};

} // namespace Syslog_agent