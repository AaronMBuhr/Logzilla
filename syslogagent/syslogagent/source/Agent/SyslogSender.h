/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <Windows.h>
#include <WinSvc.h>
#include "Configuration.h"
#include "INetworkClient.h"
#include "MessageQueue.h"
#include "MessageBatcher.h"
#include "WindowsTimer.h"

using namespace std;

namespace Syslog_agent {

class SyslogSender {
public:
    static constexpr uint32_t MAX_MESSAGE_SIZE = 65536;         // Maximum size of a message batch in bytes
    static constexpr uint32_t BATCH_SIZE_THRESHOLD = 100;       // Messages per batch
    static constexpr uint32_t MIN_BATCH_INTERVAL = 100;         // Minimum ms between batches
    static constexpr uint32_t MAX_BATCH_SIZE = 100;            // Maximum messages in a single batch

    SyslogSender(Configuration& config,
        shared_ptr<MessageQueue> primary_queue,
        shared_ptr<MessageQueue> secondary_queue,
        shared_ptr<INetworkClient> primary_network_client,
        shared_ptr<INetworkClient> secondary_network_client,
        shared_ptr<MessageBatcher> primary_batcher,
        shared_ptr<MessageBatcher> secondary_batcher);

    ~SyslogSender() = default;

    // Prevent copying
    SyslogSender(const SyslogSender&) = delete;
    SyslogSender& operator=(const SyslogSender&) = delete;

    void run() const;

    static void requestStop() { stop_requested_ = true; }
    static bool isStopRequested() { return stop_requested_; }

protected:
    int sendMessageBatch(
        shared_ptr<MessageQueue> msg_queue,
        shared_ptr<INetworkClient> network_client,
        uint32_t batch_count,
        char* batch_buf,
        uint32_t& batch_buf_length,
        int64_t& oldest_message_time) const;

private:
    static atomic<bool> stop_requested_;

    static const char event_header_[];
    static const char message_separator_[];
    static const char message_trailer_[];

    Configuration& config_;
    shared_ptr<MessageQueue> primary_queue_;
    shared_ptr<MessageQueue> secondary_queue_;
    shared_ptr<INetworkClient> primary_network_client_;
    shared_ptr<INetworkClient> secondary_network_client_;
    shared_ptr<MessageBatcher> primary_batcher_;
    shared_ptr<MessageBatcher> secondary_batcher_;
    unique_ptr<char[]> message_buffer_;
    mutable int64_t primary_oldest_message_time_;
    mutable int64_t secondary_oldest_message_time_;
};

} // namespace Syslog_agent