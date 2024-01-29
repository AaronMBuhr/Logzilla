/*
SyslogAgent: a syslog agent for Windows
Copyright © 2021 Logzilla Corp.
*/

#pragma once
#include <memory>
#include <thread>
#include "Configuration.h"
#include "EventLogSubscription.h"
#include "FileWatcher.h"
#include "MessageQueue.h"
#include "MessageQueueLogMessageSender.h"
#include "NetworkClient.h"
#include "WindowsEvent.h"

namespace Syslog_agent {

#define VERSION_MAJOR	    	"6"
#define VERSION_MINOR			"27"
#define VERSION_FIXVERSION      "0"
#define VERSION_MINORFIXVERSION "0"
#define APP_NAME    			"LZ Syslog Agent"
#define SERVICE_NAME			L"LZ Syslog Agent"


    class Service {
    public:
        static void run(bool running_as_console);
        static void shutdown();

        static const int MESSAGE_QUEUE_SIZE = 100000;
        static const int MESSAGE_BUFFERS_CHUNK_SIZE = 100;
        static const int MSEC_BETWEEN_CONNECTION_ATTEMPTS = 4000;
        static unique_ptr<thread> send_thread_;
        static MessageQueue message_queue_;
        static Configuration config_;
        static shared_ptr<NetworkClient> primary_network_client_;
        static shared_ptr<NetworkClient> secondary_network_client_;
        static shared_ptr<MessageQueueLogMessageSender> log_msg_sender_;
        static volatile bool shutdown_requested_;
        static volatile bool service_shutdown_requested_;
        static WindowsEvent shutdown_event_;
        static void loadConfiguration(bool running_from_console, bool override_log_level, Logger::LogLevel override_log_level_setting)
        {
            config_.loadFromRegistry(running_from_console, override_log_level, override_log_level_setting);
        }

    private:
        static shared_ptr<FileWatcher> filewatcher_;
        static bool restart(); // returns false for failure
        static bool setForRestart(); // returns false for failure
        static bool setForNoRestart(); // returns false for failure
        static vector<EventLogSubscription> subscriptions_;
    };
}
