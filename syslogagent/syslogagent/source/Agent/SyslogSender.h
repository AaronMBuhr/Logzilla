#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <mutex>
#include <condition_variable>
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
    static constexpr uint32_t MAX_BATCH_SIZE = 100;            // Maximum messages in a single batch
    static constexpr uint32_t MAX_MESSAGE_AGE_MS = 1000;       // Maximum age of oldest message before forcing a batch

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
        uint32_t batch_buf_length) const;

    uint64_t next_wait_time_ms(uint64_t longest_wait_time_ms) const;
    void waitForBatch(MessageQueue* first_queue, MessageQueue* second_queue) const;

    // Check if a batch should be sent based on queue length or message age
    bool shouldSendBatch(shared_ptr<MessageQueue> queue) const {
        if (!queue) return false;
        
        // Check queue length
        if (queue->length() >= MAX_BATCH_SIZE) {
            Logger::debug2("SyslogSender::shouldSendBatch()> Queue length %d >= threshold %d\n",
                queue->length(), MAX_BATCH_SIZE);
            return true;
        }
        
        // Check message age
        int64_t oldest_time = queue->getOldestMessageTimestamp();
        if (oldest_time > 0) {
            int64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t age = current_time - oldest_time;
            
            if (age >= MAX_MESSAGE_AGE_MS) {
                Logger::debug2("SyslogSender::shouldSendBatch()> Oldest message age %lld ms >= max age %d ms\n",
                    age, MAX_MESSAGE_AGE_MS);
                return true;
            }
        }
        
        return false;
    }

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
    mutable std::mutex batch_mutex_;
    mutable std::condition_variable batch_cv_;
    unique_ptr<char[]> message_buffer_;

};

} // namespace Syslog_agent