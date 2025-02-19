/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include "SyslogSender.h"
#include "Configuration.h"
#include "Globals.h"
#include "HttpNetworkClient.h"
#include "INetworkClient.h"
#include "Logger.h"
#include "SlidingWindowMetrics.h"
#include "Util.h"
#include "EventLogger.h"

namespace Syslog_agent {

// Define static members
std::atomic<bool> SyslogSender::stop_requested_ = false;

const char SyslogSender::event_header_[] = "{\"events\":[";
const char SyslogSender::message_separator_[] = ",";
const char SyslogSender::message_trailer_[] = "]}";

SyslogSender::SyslogSender(
    std::shared_ptr<MessageQueue> primary_queue,
    std::shared_ptr<MessageQueue> secondary_queue,
    std::shared_ptr<INetworkClient> primary_network_client,
    std::shared_ptr<INetworkClient> secondary_network_client,
    std::shared_ptr<MessageBatcher> primary_batcher,
    std::shared_ptr<MessageBatcher> secondary_batcher,
    uint32_t max_batch_size,
    uint32_t max_batch_age)
    : max_batch_size_(max_batch_size)
    , max_batch_age_(max_batch_age)
    , primary_queue_(std::move(primary_queue))
    , secondary_queue_(std::move(secondary_queue))
    , primary_network_client_(std::move(primary_network_client))
    , secondary_network_client_(std::move(secondary_network_client))
    , primary_batcher_(std::move(primary_batcher))
    , secondary_batcher_(std::move(secondary_batcher))
    , send_buffer_(new char[SEND_BUFFER_SIZE])
{
    auto logger = LOG_THIS;
    logger->debug2("SyslogSender constructor\n");
    using namespace std::placeholders;
    std::function<bool(size_t, MessageQueue::Message*, bool)> hook = 
        std::bind(&SyslogSender::enqueueHook, this, _1, _2, _3);
    primary_queue_->setEnqueueHook(hook);
    if (secondary_queue_) {
        secondary_queue_->setEnqueueHook(hook);
    }
}

uint64_t SyslogSender::next_wait_time_ms(uint64_t longest_wait_time_ms) const {
    auto primary_oldest_message_time = primary_queue_->getOldestMessageTimestamp();
    auto secondary_oldest_message_time = secondary_queue_ ? secondary_queue_->getOldestMessageTimestamp() : 0;
    
    // If primary queue has no messages, use secondary queue time (which may be 0)
    // If primary queue has messages but secondary is null or empty, use primary time
    // If both have messages, use the older one
    auto oldest_message_time = primary_oldest_message_time == 0 ? secondary_oldest_message_time :
        secondary_oldest_message_time == 0 ? primary_oldest_message_time :
        (std::min)(primary_oldest_message_time, secondary_oldest_message_time);
    
    uint64_t time_to_wait = longest_wait_time_ms;
    if (oldest_message_time > 0) {
        auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        // oldest_message_time is already in milliseconds
        auto elapsed_ms = current_time - oldest_message_time;
        time_to_wait = elapsed_ms >= longest_wait_time_ms ? 0 : longest_wait_time_ms - elapsed_ms;
    }
    return time_to_wait;
}

bool SyslogSender::waitForBatch(MessageQueue* first_queue, MessageQueue* second_queue) const {
    auto logger = LOG_THIS;
    std::unique_lock<std::mutex> lock(batch_mutex_);
    auto size_check = [this, first_queue, second_queue]() { 
        return (first_queue && first_queue->length() >= max_batch_size_) 
            || (second_queue && second_queue->length() >= max_batch_size_); 
    };
    if (size_check()) {
        return false;
    }
    auto wait_time_ms = next_wait_time_ms(max_batch_age_);
	if (wait_time_ms == 0) {
		return false;
	}
    logger->debug3("-------------------------------------------------------------------> SyslogSender::waitForBatch()> queue sizes: %d %d, wait time %lu\n", 
        (first_queue ? first_queue->length() : 0),
        (second_queue ? second_queue->length() : 0), 
        wait_time_ms);
    batch_cv_.wait_for(lock, std::chrono::milliseconds(wait_time_ms), size_check);
    return true;
}

void SyslogSender::run() const {
    auto logger = LOG_THIS;
    logger->debug2("SyslogSender::run()> Starting sender thread\n");
    
    MessageQueue* primary_queue = (primary_network_client_ && primary_batcher_) ? primary_queue_.get() : nullptr;
    MessageQueue* secondary_queue = (secondary_network_client_ && secondary_batcher_) ? secondary_queue_.get() : nullptr;
    bool primary_has_messages = true;
    bool secondary_has_messages = true;

    // Allocate send buffer from global pool
    std::unique_ptr<char[]> batch_buffer(new char[MAX_MESSAGE_SIZE]);
    uint32_t batch_buffer_length = 0;

    while (!isStopRequested()) {
        try {
            logger->debug3("SyslogSender::run()> Queue lengths - Primary: %d, Secondary: %d\n",
                primary_queue ? primary_queue->length() : 0,
                secondary_queue ? secondary_queue->length() : 0);
                
            while (waitForBatch(
                primary_has_messages ? primary_queue : nullptr, 
                secondary_has_messages ? secondary_queue : nullptr));

            if (isStopRequested()) {
                break;
            }

            // Process primary queue if messages are available
            if (primary_queue) {
                logger->debug3("SyslogSender::run()> Attempting to batch primary queue messages\n");
                size_t initial_queue_size = primary_queue->length();
                
                for (size_t messages_processed = 0; messages_processed < initial_queue_size;) {
                    auto batch_result = primary_batcher_->BatchEvents(primary_queue_, batch_buffer.get(), MAX_MESSAGE_SIZE);
                    logger->debug3("SyslogSender::run()> Primary batch result status: %d, messages: %d, bytes: %d\n",
                        (int)batch_result.status, batch_result.messages_batched, batch_result.bytes_written);
                    if (batch_result.status == MessageBatcher::BatchResult::Status::Success) {
                        batch_buffer_length = static_cast<uint32_t>(batch_result.bytes_written);
                        sendMessageBatch(primary_queue_, primary_network_client_, batch_result.messages_batched,
                            batch_buffer.get(), batch_buffer_length);
                        messages_processed += batch_result.messages_batched;
                    }
                    else {
                        break;
                    }
                }
                primary_has_messages = (primary_queue->length() > 0);
            }

            // Process secondary queue if messages are available
            if (secondary_queue) {
                logger->debug3("SyslogSender::run()> Attempting to batch secondary queue messages\n");
                size_t initial_queue_size = secondary_queue->length();
                
                for (size_t messages_processed = 0; messages_processed < initial_queue_size;) {
                    auto batch_result = secondary_batcher_->BatchEvents(secondary_queue_, batch_buffer.get(), MAX_MESSAGE_SIZE);
                    logger->debug3("SyslogSender::run()> Secondary batch result status: %d, messages: %d, bytes: %d\n",
                        (int)batch_result.status, batch_result.messages_batched, batch_result.bytes_written);
                    if (batch_result.status == MessageBatcher::BatchResult::Status::Success) {
                        batch_buffer_length = static_cast<uint32_t>(batch_result.bytes_written);
                        sendMessageBatch(secondary_queue_, secondary_network_client_, batch_result.messages_batched,
                            batch_buffer.get(), batch_buffer_length);
                        messages_processed += batch_result.messages_batched;
                    }
                    else {
                        break;
                    }
                }
                secondary_has_messages = (secondary_queue->length() > 0);
            }
        }
        catch (const std::exception& e) {
            logger->recoverable_error("SyslogSender::run()> Exception: %s\n", e.what());
            Sleep(max_batch_age_); // Wait a bit before retrying
        }
    }

    logger->debug2("SyslogSender::run()> Sender thread stopping\n");
}

int SyslogSender::sendMessageBatch(
    shared_ptr<MessageQueue> msg_queue,
    shared_ptr<INetworkClient> network_client,
    uint32_t batch_count,
    char* batch_buf,
    uint32_t batch_buf_length) const
{
    auto logger = LOG_THIS;
    if (!network_client || !msg_queue || !batch_buf || batch_count == 0) {
        logger->critical("SyslogSender::sendMessageBatch()> Invalid parameters\n");
        return 0;
    }

    // Check if we're shutting down
    if (isShuttingDown() || msg_queue->isShuttingDown()) {
        logger->debug2("SyslogSender::sendMessageBatch()> Shutdown in progress, skipping batch send\n");
        return 0;
    }

    try {
        logger->debug2("SyslogSender::sendMessageBatch()> Attempting to send batch of %u messages (%u bytes)\n",
            batch_count, batch_buf_length);

        if (logger->getLogLevel() == Logger::DEBUG3) {
            EventLogger::logNetworkSend(batch_buf, batch_buf_length);
        }

        // Attempt to send the batch
        INetworkClient::RESULT_TYPE result = network_client->post(batch_buf, batch_buf_length);

        EventLogger::logNetworkReceive(result.getMessage(), strlen(result.getMessage()));

        if (result != INetworkClient::RESULT_SUCCESS) {
            // Don't log as critical during shutdown - it's expected to fail
            if (isShuttingDown() || msg_queue->isShuttingDown()) {
                logger->debug2("SyslogSender::sendMessageBatch()> Network send failed during shutdown\n");
            }
            else {
                logger->critical("SyslogSender::sendMessageBatch()> Failed to send batch, network error: %d\n", result);
            }
            return 0;
        }

        logger->debug3("SyslogSender::sendMessageBatch()> Network send successful\n");

        // Only remove messages after successful send
        uint32_t messages_removed = 0;
        for (uint32_t i = 0; i < batch_count; i++) {
            if (!msg_queue->removeFront()) {
                logger->critical("SyslogSender::sendMessageBatch()> Failed to remove message %u of %u\n",
                    i + 1, batch_count);
                break;
            }
            SlidingWindowMetrics::instance().recordOutgoing();
            string eventJson = EventLogger::queuePopFront();
            EventLogger::log(EventLogger::LogDestination::SentEvents,
                "Event sent: %s\n", eventJson.c_str());
            messages_removed++;
        }

        logger->debug2("SyslogSender::sendMessageBatch()> Successfully sent and removed %u messages\n",
            messages_removed);

        return static_cast<int>(messages_removed);
    }
    catch (const std::exception& e) {
        logger->critical("SyslogSender::sendMessageBatch()> Exception: %s\n", e.what());
        return 0; // Return 0 to indicate no messages were processed
    }
}

bool SyslogSender::enqueueHook(size_t queue_length, MessageQueue::Message* message, bool is_pre_enqueue) const {
    if (is_pre_enqueue) {
        // This is called before enqueue, we can do validation here
        return true;
    }
    else {
        // This is called after successful enqueue
        if (queue_length >= max_batch_size_) {
            batch_cv_.notify_one();
        }
        return true;
    }
}

} // namespace Syslog_agent