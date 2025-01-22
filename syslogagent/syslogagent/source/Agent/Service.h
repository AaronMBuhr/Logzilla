/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include "WindowsEvent.h"
#include "Configuration.h"
#include "LogConfiguration.h"
#include "MessageQueue.h"
#include "INetworkClient.h"
#include "EventLogSubscription.h"
#include "FileWatcher.h"

namespace Syslog_agent {

#define VERSION_MAJOR	    	"6"
#define VERSION_MINOR			"31"
#define VERSION_FIXVERSION      "0"
#define VERSION_MINORFIXVERSION "0"
#define APP_NAME    			"LZ Syslog Agent"
#define SERVICE_NAME			L"LZ Syslog Agent"
    
class Service {
public:

    static constexpr size_t MESSAGE_QUEUE_SIZE = 1000000;
    static constexpr size_t MESSAGE_BUFFERS_CHUNK_SIZE = 1000;
    static constexpr int DEFAULT_EVENT_LOG_POLL_INTERVAL = 1;

    // Static member variables, visible for use by sendMessagesThread
    static Configuration config_;
    static shared_ptr<MessageQueue> primary_message_queue_;
    static shared_ptr<MessageQueue> secondary_message_queue_;
    static shared_ptr<INetworkClient> primary_network_client_;
    static shared_ptr<INetworkClient> secondary_network_client_;

    // Public interface
    static void run(bool running_as_console);
    static void shutdown();
    static void fatalErrorHandler(const char* msg);
    static void loadConfiguration(bool running_from_console, bool override_log_level, Logger::LogLevel override_log_level_setting);

protected:
    // Initialization methods
    static bool initializeNetworkComponents();
    static bool initializePrimaryCertificate();
    static bool initializeSecondaryComponents();
    static bool getAndSetLogZillaVersion(const shared_ptr<INetworkClient>& client, bool isPrimary);
    static void initializeEventLogSubscriptions(const vector<LogConfiguration>& logs);

    // Main loop control methods
    static void mainLoop(bool running_as_console, bool& first_loop, int& restart_needed);
    static bool checkForShutdown(bool running_as_console, int& restart_needed);
    static void handleQueueStatusAndConfig();
    static void cleanupAndShutdown(bool running_as_console, int restart_needed);

private:
    // Internal state
    static std::atomic<bool> fatal_shutdown_in_progress;
    static unique_ptr<thread> send_thread_;
    static volatile bool shutdown_requested_;
    static volatile bool service_shutdown_requested_;
    static WindowsEvent shutdown_event_;
    static shared_ptr<FileWatcher> filewatcher_;
    static vector<EventLogSubscription> subscriptions_;

    // Prevent instantiation
    Service() = delete;
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
};

} // namespace Syslog_agent