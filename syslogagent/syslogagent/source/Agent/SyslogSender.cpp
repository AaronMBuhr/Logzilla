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
volatile bool SyslogSender::stop_requested_ = false;
WindowsTimer SyslogSender::enqueue_timer_;

const char SyslogSender::event_header_[] = "{\"events\":[";
const char SyslogSender::message_separator_[] = ",";
const char SyslogSender::message_trailer_[] = "]}";

SyslogSender::SyslogSender(
    Configuration& config,
    shared_ptr<MessageQueue> primary_queue,
    shared_ptr<MessageQueue> secondary_queue,
    shared_ptr<INetworkClient> primary_network_client,
    shared_ptr<INetworkClient> secondary_network_client)
    : config_(config),
    primary_queue_(primary_queue),
    secondary_queue_(secondary_queue),
    primary_network_client_(primary_network_client),
    secondary_network_client_(secondary_network_client),
    message_buffer_(new char[MAX_MESSAGE_SIZE]),
    primary_oldest_message_time_(0),
    secondary_oldest_message_time_(0)
{
    Logger::debug2("SyslogSender constructor\n");
}

void SyslogSender::run() const {
    Logger::debug2("Syslog_sender::run() starting\n");
    char* buf = Globals::instance()->getMessageBuffer("SyslogSender::run()");
    int64_t last_start_loop_time = Util::getUnixTimeMilliseconds();

    while (!SyslogSender::stop_requested_) {
        Logger::debug2("SyslogSender::run() Starting main loop iteration\n");
        int64_t current_time = Util::getUnixTimeMilliseconds();
        
        // Manage timing of the run loop
        if (current_time - last_start_loop_time < SharedConstants::SENDER_MAINLOOP_DURATION) {
            Logger::debug2("SyslogSender::run() Sleeping for %d ms\n", 
                static_cast<int>(SharedConstants::SENDER_MAINLOOP_DURATION - (current_time - last_start_loop_time)));
            Sleep(static_cast<DWORD>(SharedConstants::SENDER_MAINLOOP_DURATION - 
                (current_time - last_start_loop_time)));
            current_time = Util::getUnixTimeMilliseconds();
        }
        last_start_loop_time = current_time;

        // Handle primary queue
        if (!primary_queue_->isEmpty()) {
            Logger::debug2("Primary queue processing - Length=%d, Oldest message time=%lld\n", 
                primary_queue_->length(), primary_oldest_message_time_);
            // If this is the first message in a new batch, record the time
            if (primary_oldest_message_time_ == 0) {
                primary_oldest_message_time_ = current_time;
            }
            
            // Check if we need to force send due to timeout
            bool should_force_send = (current_time - primary_oldest_message_time_) >= 
                                   config_.getBatchInterval();
            
            if (should_force_send || primary_queue_->length() >= BATCH_SIZE_THRESHOLD) {
                Logger::debug2("Attempting to send primary queue batch - Force send: %s, Queue length: %d\n",
                    should_force_send ? "yes" : "no", primary_queue_->length());
                
                primary_queue_->runInsideLock([&]() -> int {
                    return sendMessageBatch(primary_queue_, primary_network_client_, 
                        buf, primary_oldest_message_time_);
                });
                
                Logger::debug2("Primary queue after send attempt - Length: %d\n", primary_queue_->length());
            } else {
                Logger::debug2("Not sending primary queue - Below threshold or timeout (Length: %d, Time since oldest: %lld ms)\n",
                    primary_queue_->length(), current_time - primary_oldest_message_time_);
            }
        } else {
            Logger::debug2("Primary queue is empty\n");
        }

        // Handle secondary queue
        if (secondary_queue_ && !secondary_queue_->isEmpty()) {
            Logger::debug2("Secondary queue processing - Length=%d, Oldest message time=%lld\n", 
                secondary_queue_->length(), secondary_oldest_message_time_);
            // If this is the first message in a new batch, record the time
            if (secondary_oldest_message_time_ == 0) {
                secondary_oldest_message_time_ = current_time;
            }
            
            // Check if we need to force send due to timeout
            bool should_force_send = (current_time - secondary_oldest_message_time_) >= 
                                   config_.getBatchInterval();
            
            if (should_force_send || secondary_queue_->length() >= BATCH_SIZE_THRESHOLD) {
                Logger::debug2("Attempting to send secondary queue batch - Force send: %s, Queue length: %d\n",
                    should_force_send ? "yes" : "no", secondary_queue_->length());
                
                secondary_queue_->runInsideLock([&]() -> int {
                    return sendMessageBatch(secondary_queue_, secondary_network_client_, 
                        buf, secondary_oldest_message_time_);
                });
                
                Logger::debug2("Secondary queue after send attempt - Length: %d\n", secondary_queue_->length());
            } else {
                Logger::debug2("Not sending secondary queue - Below threshold or timeout (Length: %d, Time since oldest: %lld ms)\n",
                    secondary_queue_->length(), current_time - secondary_oldest_message_time_);
            }
        } else {
            Logger::debug2("Secondary queue is empty\n");
        }
    }
    
    Globals::instance()->releaseMessageBuffer(buf);
    Logger::debug2("Syslog_sender::run() ending\n");
}

int SyslogSender::sendMessageBatch(
    shared_ptr<MessageQueue> msg_queue, 
    shared_ptr<INetworkClient> network_client,
    char* buf,
    int64_t& oldest_message_time) const {
    
    if (!msg_queue || !network_client || !buf) {
        Logger::recoverable_error("SyslogSender::sendMessageBatch() Invalid parameters\n");
        return 0;
    }

    Logger::debug2("sendMessageBatch() Starting batch processing\n");
    unique_ptr<char[]> message_buffer_local = make_unique<char[]>(MAX_MESSAGE_SIZE);
    int batch_count = 0;
    const int queue_length = msg_queue->length();
    Logger::debug2("sendMessageBatch() Initial queue length: %d\n", queue_length);

    // Pre-calculate constant sizes to avoid repeated calculations
    const size_t header_size = strlen(event_header_);
    const size_t separator_size = strlen(message_separator_);
    const size_t trailer_size = strlen(message_trailer_);

    try {
        // Copy header to local buffer
        if (header_size >= MAX_MESSAGE_SIZE) {
            Logger::recoverable_error("SyslogSender::sendMessageBatch() Header too large\n");
            return 0;
        }
        memcpy(message_buffer_local.get(), event_header_, header_size);
        uint32_t message_buffer_length = static_cast<uint32_t>(header_size);
        const char* sep = "";

        Logger::debug2("sendMessageBatch() JSON Header: '%.*s'\n", header_size, event_header_);

        // Limit batch size to BATCH_SIZE_THRESHOLD
        int max_batch = std::min<int>(queue_length, static_cast<int>(BATCH_SIZE_THRESHOLD));
        Logger::debug2("sendMessageBatch() Will process up to %d messages\n", max_batch);

        while (batch_count < max_batch) {
            Logger::debug2("sendMessageBatch() Processing message %d of %d\n", batch_count + 1, max_batch);
            
            // Get the actual message first
            int msg_size = msg_queue->peek(buf, Globals::MESSAGE_BUFFER_SIZE, batch_count);
            if (msg_size < 0) {
                Logger::recoverable_error("SyslogSender::sendMessageBatch() Failed to peek message at index %d\n", 
                    batch_count);
                break;
            }
            
            if (msg_size > Globals::MESSAGE_BUFFER_SIZE) {
                Logger::recoverable_error("SyslogSender::sendMessageBatch() Message size %d exceeds buffer size %d\n", 
                    msg_size, Globals::MESSAGE_BUFFER_SIZE);
                break;
            }

            Logger::debug2("sendMessageBatch() Message %d size: %d bytes\n", batch_count + 1, msg_size);
            Logger::debug2("sendMessageBatch() Message content: '%.*s'\n", msg_size, buf);

            // Now check if we have room for this specific message plus separators and trailer
            // Add extra buffer for event data fields that might be present
            constexpr size_t EXTRA_FIELD_BUFFER = 2048;  // Conservative estimate for field overhead
            if (msg_size + message_buffer_length + strlen(sep) + trailer_size + EXTRA_FIELD_BUFFER >= MAX_MESSAGE_SIZE) {
                Logger::debug2("SyslogSender::sendMessageBatch() Message would exceed max size, stopping at %d messages\n", 
                    batch_count);
                break;
            }

            // Copy separator if not first message
            if (sep[0] != '\0') {
                Logger::debug2("sendMessageBatch() Adding separator: '%s'\n", sep);
                memcpy(message_buffer_local.get() + message_buffer_length, sep, separator_size);
                message_buffer_length += static_cast<uint32_t>(separator_size);
            }

            // Copy message
            memcpy(message_buffer_local.get() + message_buffer_length, buf, msg_size);
            message_buffer_length += msg_size;
            batch_count++;
            sep = message_separator_;
        }

        if (message_buffer_length > 0) {
            // Add the closing bracket
            memcpy(message_buffer_local.get() + message_buffer_length, message_trailer_, trailer_size);
            message_buffer_length += static_cast<uint32_t>(trailer_size);

            // Log the final message in chunks to avoid buffer issues
            const uint32_t CHUNK_SIZE = 1024;
            Logger::debug2("sendMessageBatch() Final JSON message (length=%d):\n", message_buffer_length);
            for (uint32_t i = 0; i < message_buffer_length; i += CHUNK_SIZE) {
                const uint32_t remaining = message_buffer_length - i;
                const uint32_t chunk_size = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
                Logger::debug2("JSON chunk %d: '%.*s'\n", i/CHUNK_SIZE, chunk_size, message_buffer_local.get() + i);
            }

            // HTTP-specific settings like compression and HTTP2 are handled internally
            // by the HttpNetworkClient during initialization and connection setup

            INetworkClient::RESULT_TYPE result = network_client->post(
                message_buffer_local.get(), message_buffer_length);

            Logger::debug2("sendMessageBatch() Network post result: %d\n", result);

            if (result == INetworkClient::RESULT_SUCCESS) {
                Logger::debug2("sendMessageBatch() Successfully sent batch of %d messages\n", batch_count);
                // Only remove messages after successful send
                bool remove_error = false;
                for (int i = 0; i < batch_count && !remove_error; i++) {
                    if (!msg_queue->removeFront()) {
                        Logger::critical("SyslogSender::sendMessageBatch() Failed to remove sent message %d\n", i + 1);
                        remove_error = true;
                    }
                }
                if (!remove_error) {
                    oldest_message_time = 0;  // Reset timer after successful send
                    Logger::debug2("sendMessageBatch() Successfully removed all sent messages from queue\n");
                } else {
                    batch_count = 0;  // If we couldn't remove messages, don't count them
                    Logger::critical("sendMessageBatch() Failed to remove some messages, batch considered failed\n");
                }
            } else {
                Logger::recoverable_error("SyslogSender::sendMessageBatch() Failed to send batch, network error: %d\n", result);
                batch_count = 0;
            }
        }
    }
    catch (const std::exception& e) {
        Logger::recoverable_error("SyslogSender::sendMessageBatch() Exception: %s\n", e.what());
        batch_count = 0;
    }

    return batch_count;
}

} // namespace Syslog_agent