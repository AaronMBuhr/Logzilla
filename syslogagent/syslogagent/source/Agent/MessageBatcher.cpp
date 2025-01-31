#include "stdafx.h"
#include "Globals.h"
#include "Logger.h"
#include "MessageBatcher.h"

using namespace Syslog_agent;

MessageBatcher::MessageBatcher()
{
}

MessageBatcher::~MessageBatcher()
{
}

int MessageBatcher::BatchEvents(shared_ptr<MessageQueue> msg_queue, char* batch_buffer) const
{
    return BatchEventsInternal(msg_queue, batch_buffer);
}

int MessageBatcher::BatchEventsInternal(shared_ptr<MessageQueue> msg_queue, char* batch_buffer) const
{
    int queue_length = msg_queue->length();
    Logger::debug3("MessageBatcher::BatchEventsInternal() Initial queue length: %d\n", queue_length);

    if (queue_length == 0) {
        return 0;
    }

    try {
        // Pre-calculate constant sizes to avoid repeated calculations
        const size_t header_size = strlen(GetMessageHeader_());
        const size_t separator_size = strlen(GetMessageSeparator_());
        const size_t trailer_size = strlen(GetMessageTrailer_());

        // Write the header
        if (header_size >= static_cast<size_t>(GetMaxMessageSize_())) {
            Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Header too large\n");
            return 0;
        }

        // Copy header to local buffer
        if (header_size > 0) {
            memcpy(batch_buffer, GetMessageHeader_(), header_size);
        }
        size_t current_length = header_size;

        Logger::debug3("MessageBatcher::BatchEventsInternal()> '%.*s'\n", 
            static_cast<int>(header_size), GetMessageHeader_());

        // Limit batch size to BATCH_SIZE_THRESHOLD
        int max_batch = std::min<int>(queue_length, static_cast<int>(GetBatchSizeThreshold_()));
        Logger::debug3("MessageBatcher::BatchEventsInternal()> Will process up to %d messages\n", max_batch);

        // Get a buffer from the global pool for temporary message storage
        char* single_message_buffer = Syslog_agent::Globals::instance()->getMessageBuffer("BatchEventsInternal");
        if (!single_message_buffer) {
            Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Failed to get message buffer\n");
            return 0;
        }

        int batch_count = 0;
        for (int i = 0; i < max_batch; ++i) {
            // Peek at the next message
            int msg_size = msg_queue->peek(single_message_buffer, GetMaxMessageSize_(), i);
            Globals::instance()->peek_count_++;

            if (msg_size <= 0) break;

            // Check message size
            if (msg_size >= GetMaxMessageSize_()) {
                Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Message too large\n");
                continue;
            }

            // Calculate total size needed for this message
            size_t needed_size = msg_size + 
                (batch_count > 0 ? separator_size : 0) + 
                (i == max_batch - 1 ? trailer_size : 0);

            // Check if adding this message would exceed buffer size
            if (current_length + needed_size >= static_cast<size_t>(GetMaxMessageSize_())) {
                Logger::debug2("MessageBatcher::BatchEventsInternal()> Buffer full, stopping batch\n");
                break;
            }

            // Add separator if this isn't the first message
            if (batch_count > 0 && separator_size > 0) {
                memcpy(batch_buffer + current_length, GetMessageSeparator_(), separator_size);
                current_length += separator_size;
            }

            // Copy the message
            memcpy(batch_buffer + current_length, single_message_buffer, msg_size);
            current_length += msg_size;
            batch_count++;
        }

        // Release the buffer back to the pool
        Syslog_agent::Globals::instance()->releaseMessageBuffer(single_message_buffer);

        // Add trailer if we have any messages
        if (batch_count > 0 && trailer_size > 0) {
            memcpy(batch_buffer + current_length, GetMessageTrailer_(), trailer_size);
            current_length += trailer_size;
        }

        // Null terminate the batch buffer
        batch_buffer[current_length] = '\0';

        Globals::instance()->batched_count_ += batch_count;
        Logger::always("***** Message counts: queued %d, peeked %d, batched %d\n", 
                       Globals::instance()->queued_count_, 
                       Globals::instance()->peek_count_, 
                       Globals::instance()->batched_count_);

        return batch_count;
    }
    catch (const std::exception& e) {
        Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Exception: %s\n", e.what());
        return 0;
    }
}