/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include <algorithm>
#include "stdafx.h"
#include "Configuration.h"
#include "Globals.h"
#include "HttpNetworkClient.h"
#include "INetworkClient.h"
#include "Logger.h"
#include "SlidingWindowMetrics.h"
#include "SyslogSender.h"
#include "Util.h"
#include "EventLogger.h"

namespace Syslog_agent {

// Define static members
atomic<bool> SyslogSender::stop_requested_ = false;

const char SyslogSender::event_header_[] = "{\"events\":[";
const char SyslogSender::message_separator_[] = ",";
const char SyslogSender::message_trailer_[] = "]}";

SyslogSender::SyslogSender(
    Configuration& config,
    shared_ptr<MessageQueue> primary_queue,
    shared_ptr<MessageQueue> secondary_queue,
    shared_ptr<INetworkClient> primary_network_client,
    shared_ptr<INetworkClient> secondary_network_client,
    shared_ptr<MessageBatcher> primary_batcher,
    shared_ptr<MessageBatcher> secondary_batcher)
    : config_(config)
    , primary_queue_(primary_queue)
    , secondary_queue_(secondary_queue)
    , primary_network_client_(primary_network_client)
    , secondary_network_client_(secondary_network_client)
    , primary_batcher_(primary_batcher)
    , secondary_batcher_(secondary_batcher)
    , message_buffer_(new char[MAX_MESSAGE_SIZE])
{
    Logger::debug2("SyslogSender constructor\n");
}

uint64_t SyslogSender::next_wait_time_ms(uint64_t longest_wait_time_ms) const {
    uint64_t primary_oldest_message_time_ = primary_queue_ ? primary_queue_->getOldestMessageTimestamp() : 0;
    uint64_t secondary_oldest_message_time_ = secondary_queue_ ? secondary_queue_->getOldestMessageTimestamp() : 0;
    uint64_t oldest_message_time = primary_oldest_message_time_ == 0 ? 
        secondary_oldest_message_time_ : (secondary_oldest_message_time_ == 0 ? primary_oldest_message_time_ : 
        (std::min)(primary_oldest_message_time_, secondary_oldest_message_time_));
    uint64_t time_to_wait = longest_wait_time_ms;
    if (oldest_message_time > 0) {
        auto current_time = std::chrono::system_clock::now().time_since_epoch().count();
        time_to_wait = longest_wait_time_ms - (current_time - oldest_message_time);
    }
    return (std::max)(time_to_wait, (uint64_t)0);
}

void SyslogSender::waitForBatch(MessageQueue* first_queue, MessageQueue* second_queue) const {
    std::unique_lock<std::mutex> lock(batch_mutex_);
    auto wait_time_ms = next_wait_time_ms(MAX_MESSAGE_AGE_MS);
    auto size_check = [first_queue, second_queue]() { 
        return (first_queue && first_queue->length() >= MAX_BATCH_SIZE) 
            || (second_queue && second_queue->length() >= MAX_BATCH_SIZE); 
    };
    if (size_check()) {
        return;
    }
    batch_cv_.wait_for(lock, std::chrono::milliseconds(wait_time_ms), size_check);
}

void SyslogSender::run() const
{
    Logger::debug2("SyslogSender::run()> Starting sender thread\n");
    
    unique_ptr<char[]> batch_buffer = make_unique<char[]>(MAX_MESSAGE_SIZE);
    uint32_t batch_buffer_length = 0;

    MessageQueue* primary_queue = (primary_network_client_ && primary_batcher_) ? primary_queue_.get() : nullptr;
    MessageQueue* secondary_queue = (secondary_network_client_ && secondary_batcher_) ? secondary_queue_.get() : nullptr;
    bool primary_has_messages = true;
    bool secondary_has_messages = true;

    while (!isStopRequested()) {
        try {
            Logger::debug3("SyslogSender::run()> Queue lengths - Primary: %d, Secondary: %d\n",
                primary_queue ? primary_queue->length() : 0,
                secondary_queue ? secondary_queue->length() : 0);
                
            waitForBatch(
                primary_has_messages ? primary_queue : nullptr, 
                secondary_has_messages ? secondary_queue : nullptr);

            // Process primary queue if messages are available
            if (primary_queue) {
                Logger::debug3("SyslogSender::run()> Attempting to batch primary queue messages\n");
                do {
                    auto batch_result = primary_batcher_->BatchEvents(primary_queue_, batch_buffer.get(), MAX_MESSAGE_SIZE);
                    Logger::debug3("SyslogSender::run()> Primary batch result status: %d, messages: %d, bytes: %d\n",
                        (int)batch_result.status, batch_result.messages_batched, batch_result.bytes_written);
                    if (batch_result.status == MessageBatcher::BatchResult::Status::Success) {
                        batch_buffer_length = static_cast<uint32_t>(batch_result.bytes_written);
                        sendMessageBatch(primary_queue_, primary_network_client_, batch_result.messages_batched, 
                            batch_buffer.get(), batch_buffer_length);
                        primary_has_messages = true;
                    } else {
                        primary_has_messages = (batch_result.status != MessageBatcher::BatchResult::Status::NoMessages);
                        if (!primary_has_messages && primary_queue->length() > 0) {
                            Logger::warning("SyslogSender::run()> Primary queue not sending, %d messages remaining\n", 
                                primary_queue->length());
                        }
                        break;
                    }
                } while (primary_queue->length() > 0);
            }

            // Process secondary queue if messages are available
            if (secondary_queue) {
                Logger::debug3("SyslogSender::run()> Attempting to batch secondary queue messages\n");
                do {
                    auto batch_result = secondary_batcher_->BatchEvents(secondary_queue_, batch_buffer.get(), MAX_MESSAGE_SIZE);
                    Logger::debug3("SyslogSender::run()> Secondary batch result status: %d, messages: %d, bytes: %d\n",
                        (int)batch_result.status, batch_result.messages_batched, batch_result.bytes_written);
                    if (batch_result.status == MessageBatcher::BatchResult::Status::Success) {
                        batch_buffer_length = static_cast<uint32_t>(batch_result.bytes_written);
                        sendMessageBatch(secondary_queue_, secondary_network_client_, batch_result.messages_batched, 
                            batch_buffer.get(), batch_buffer_length);
                        secondary_has_messages = true;
                    } else {
                        secondary_has_messages = (batch_result.status != MessageBatcher::BatchResult::Status::NoMessages);
                        if (!secondary_has_messages && secondary_queue->length() > 0) {
                            Logger::warning("SyslogSender::run()> Secondary queue not sending, %d messages remaining\n", 
                                secondary_queue->length());
                        }
                        break;
                    }
                } while (secondary_queue->length() > 0);
            }
        }
        catch (const std::exception& e) {
            Logger::recoverable_error("SyslogSender::run()> Exception: %s\n", e.what());
            Sleep(MAX_MESSAGE_AGE_MS); // Wait a bit before retrying
        }
    }

    Logger::debug2("SyslogSender::run()> Sender thread stopping\n");
}

int SyslogSender::sendMessageBatch(
    shared_ptr<MessageQueue> msg_queue,
    shared_ptr<INetworkClient> network_client,
    uint32_t batch_count,
    char* batch_buf,
    uint32_t batch_buf_length) const
{
    if (!network_client || !msg_queue || !batch_buf || batch_count == 0) {
        Logger::critical("SyslogSender::sendMessageBatch()> Invalid parameters\n");
        return 0;
    }

    try {
        Logger::debug2("SyslogSender::sendMessageBatch()> Attempting to send batch of %u messages (%u bytes)\n", 
            batch_count, batch_buf_length);
        // Logger::debug3("SyslogSender::sendMessageBatch()> Batch content: %.*s\n", 
        //     (std::min)(batch_buf_length, 1000u), batch_buf);

        // Attempt to send the batch
        INetworkClient::RESULT_TYPE result = network_client->post(batch_buf, batch_buf_length);

        if (result != INetworkClient::RESULT_SUCCESS) {
            Logger::critical("SyslogSender::sendMessageBatch()> Failed to send batch, network error: %d\n", result);
            return 0; // Return 0 to indicate no messages were processed
        }

        Logger::debug3("SyslogSender::sendMessageBatch()> Network send successful\n");

        // Only remove messages after successful send
        uint32_t messages_removed = 0;
        for (uint32_t i = 0; i < batch_count; i++) {
            if (!msg_queue->removeFront()) {
                Logger::critical("SyslogSender::sendMessageBatch()> Failed to remove message %u of %u\n", 
                    i + 1, batch_count);
                break;
            }
            SlidingWindowMetrics::instance().recordOutgoing();
            string eventJson = EventLogger::queuePopFront();
            EventLogger::log(EventLogger::LogDestination::SentEvents,
                "Event sent: %s\n", eventJson.c_str());
            messages_removed++;
        }

        Logger::debug2("SyslogSender::sendMessageBatch()> Successfully sent and removed %u messages\n", 
            messages_removed);

        return static_cast<int>(messages_removed);
    }
    catch (const std::exception& e) {
        Logger::critical("SyslogSender::sendMessageBatch()> Exception: %s\n", e.what());
        return 0; // Return 0 to indicate no messages were processed
    }
}

} // namespace Syslog_agent