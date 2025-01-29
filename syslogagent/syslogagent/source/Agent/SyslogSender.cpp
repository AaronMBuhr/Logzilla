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

    WindowsTimer batch_timer;
    batch_timer.set(MIN_BATCH_INTERVAL);

    while (!isStopRequested()) {
        try {
            // Process primary queue
            if (primary_queue_ && primary_network_client_ && primary_batcher_) {
                uint32_t batch_count = primary_batcher_->BatchEvents(primary_queue_, batch_buffer.get());
                if (batch_count > 0) {
                    batch_buffer_length = static_cast<uint32_t>(strlen(batch_buffer.get()));
                    sendMessageBatch(primary_queue_, primary_network_client_, batch_count, 
                        batch_buffer.get(), batch_buffer_length, primary_oldest_message_time_);
                }
            }

            // Process secondary queue
            if (secondary_queue_ && secondary_network_client_ && secondary_batcher_) {
                uint32_t batch_count = secondary_batcher_->BatchEvents(secondary_queue_, batch_buffer.get());
                if (batch_count > 0) {
                    batch_buffer_length = static_cast<uint32_t>(strlen(batch_buffer.get()));
                    sendMessageBatch(secondary_queue_, secondary_network_client_, batch_count, 
                        batch_buffer.get(), batch_buffer_length, secondary_oldest_message_time_);
                }
            }

            // Sleep for a short interval if no messages were processed
            if (primary_queue_->isEmpty() && (!secondary_queue_ || secondary_queue_->isEmpty())) {
                batch_timer.wait(MIN_BATCH_INTERVAL);
            }

            // Reset and set timer for next batch
            batch_timer.reset();
            batch_timer.set(MIN_BATCH_INTERVAL);
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
    if (!network_client || !msg_queue || !batch_buf) {
        Logger::critical("SyslogSender::sendMessageBatch() Invalid parameters\n");
        return 0;
    }

    try {
        Logger::debug2("sendMessageBatch() Attempting to send batch of %u messages\n", batch_count);

        INetworkClient::RESULT_TYPE result = network_client->post(
            batch_buf, batch_buf_length);

        Logger::debug2("sendMessageBatch() Network post result: %d\n", result);

        if (result == INetworkClient::RESULT_SUCCESS) {
            Logger::debug2("sendMessageBatch() Successfully sent batch of %u messages\n", batch_count);
            // Only remove messages after successful send
            bool remove_error = false;
            for (uint32_t i = 0; i < batch_count && !remove_error; i++) {
                if (!msg_queue->removeFront()) {
                    Logger::critical("SyslogSender::sendMessageBatch() Failed to remove sent message %u\n", i + 1);
                    remove_error = true;
                }
            }
            if (!remove_error) {
                oldest_message_time = 0;  // Reset timer after successful send
                Logger::debug2("sendMessageBatch() Successfully removed all sent messages from queue\n");
            } else {
                Logger::critical("SyslogSender::sendMessageBatch() Failed to remove some messages from queue\n");
                return -1;
            }
            return static_cast<int>(batch_count);
        } else {
            Logger::critical("SyslogSender::sendMessageBatch() Failed to send batch, network error: %d\n", result);
            return -1;
        }
    }
    catch (const std::exception& e) {
        Logger::critical("SyslogSender::sendMessageBatch() Exception: %s\n", e.what());
        return -1;
    }
}

} // namespace Syslog_agent