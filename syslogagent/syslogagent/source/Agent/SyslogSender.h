/*
SyslogAgent: a syslog agent for Windows
Copyright © 2021 Logzilla Corp.
*/

#pragma once

#include <memory>
#include "Configuration.h"
#include "MessageQueue.h"
#include "NetworkClient.h"
#include "WindowsEvent.h"

namespace Syslog_agent {

    class SyslogSender {
    public:
        static const int MAX_MESSAGE_SIZE = 65535;
        SyslogSender(
            MessageQueue& queue, 
            Configuration& config, 
            shared_ptr<NetworkClient> primary_network_client,
            shared_ptr<NetworkClient> secondary_network_client
        );
        static WindowsEvent enqueue_event_; // TODO : this probably shouldn't be static
        void run() const;
        static void stop() { 
            Logger::log(Logger::DEBUG2, "SyslogSender::stop() stop requested\n");
            SyslogSender::stop_requested_ = true;
        }

    private:
        shared_ptr<NetworkClient> primary_network_client_;
        shared_ptr<NetworkClient> secondary_network_client_;
        MessageQueue& queue_;
        Configuration& config_;
        volatile static bool stop_requested_;
    };
}
