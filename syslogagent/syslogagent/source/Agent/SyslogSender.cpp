/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include "SyslogSender.h"
#include "Configuration.h"
#include "Logger.h"
#include "INetworkClient.h"
#include "HttpNetworkClient.h"
#include "Util.h"
#include "Globals.h"
#include <algorithm>

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
    , primary_oldest_message_time_(0)
    , secondary_oldest_message_time_(0)
{
    Logger::debug2("SyslogSender constructor\n");
}

void SyslogSender::run() const
{
    Logger::debug2("SyslogSender::run()> Starting sender thread\n");
    
    unique_ptr<char[]> batch_buffer = make_unique<char[]>(MAX_MESSAGE_SIZE);
    uint32_t batch_buffer_length = 0;

    while (!isStopRequested()) {
        try {
            bool messages_processed = false;

            // Process primary queue if messages are available
            if (primary_queue_ && primary_network_client_ && primary_batcher_) {
                if (primary_queue_->waitForMessages(MIN_BATCH_INTERVAL)) {
                    uint32_t batch_count = primary_batcher_->BatchEvents(primary_queue_, batch_buffer.get());
                    if (batch_count > 0) {
                        messages_processed = true;
                        batch_buffer_length = static_cast<uint32_t>(strlen(batch_buffer.get()));
                        sendMessageBatch(primary_queue_, primary_network_client_, batch_count, 
                            batch_buffer.get(), batch_buffer_length, primary_oldest_message_time_);
                    }
                }
            }

            // Process secondary queue if messages are available
            if (secondary_queue_ && secondary_network_client_ && secondary_batcher_) {
                if (secondary_queue_->waitForMessages(MIN_BATCH_INTERVAL)) {
                    uint32_t batch_count = secondary_batcher_->BatchEvents(secondary_queue_, batch_buffer.get());
                    if (batch_count > 0) {
                        messages_processed = true;
                        batch_buffer_length = static_cast<uint32_t>(strlen(batch_buffer.get()));
                        sendMessageBatch(secondary_queue_, secondary_network_client_, batch_count, 
                            batch_buffer.get(), batch_buffer_length, secondary_oldest_message_time_);
                    }
                }
            }

            // If no messages were processed, do a longer wait
            if (!messages_processed) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MIN_BATCH_INTERVAL));
            }
        }
        catch (const std::exception& e) {
            Logger::recoverable_error("SyslogSender::run()> Exception: %s\n", e.what());
            Sleep(1000); // Wait a bit before retrying
        }
    }

    Logger::debug2("SyslogSender::run()> Sender thread stopping\n");
}

int SyslogSender::sendMessageBatch(
    shared_ptr<MessageQueue> msg_queue,
    shared_ptr<INetworkClient> network_client,
    uint32_t batch_count,
    char* batch_buf,
    uint32_t& batch_buf_length,
    int64_t& oldest_message_time) const
{
    if (!network_client || !msg_queue || !batch_buf || batch_count == 0) {
        Logger::critical("SyslogSender::sendMessageBatch()> Invalid parameters\n");
        return 0;
    }

    try {
        Logger::debug3("SyslogSender::sendMessageBatch()> Attempting to send batch of %u messages\n", batch_count);

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
            messages_removed++;
        }

        // Update oldest message time if we removed all messages
        if (messages_removed == batch_count) {
            oldest_message_time = 0;  // Reset timer after successful send
            Logger::debug2("SyslogSender::sendMessageBatch()> Successfully sent and removed %u messages\n", 
                messages_removed);
        } else {
            Logger::critical("SyslogSender::sendMessageBatch()> Only removed %u of %u messages\n", 
                messages_removed, batch_count);
        }

        return static_cast<int>(messages_removed);
    }
    catch (const std::exception& e) {
        Logger::critical("SyslogSender::sendMessageBatch()> Exception: %s\n", e.what());
        return 0; // Return 0 to indicate no messages were processed
    }
}

} // namespace Syslog_agent