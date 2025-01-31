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

    int batch_count = 0;
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

        unique_ptr<char[]> single_message_buffer = make_unique<char[]>(GetMaxMessageSize_());

        for (int i = 0; i < max_batch; ++i) {
            // Peek at the next message
            int msg_size = msg_queue->peek(single_message_buffer.get(), GetMaxMessageSize_(), i);
            if (msg_size <= 0) break;

            // Check message size
            if (static_cast<size_t>(msg_size) >= static_cast<size_t>(GetMaxMessageSize_())) {
                Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Message too large, skipping\n");
                msg_queue->removeFront();
                continue;
            }

            // Calculate total size needed for this message
            const size_t needed_size = static_cast<size_t>(msg_size) + 
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
                current_length += static_cast<size_t>(separator_size);
            }

            // Copy the message
            memcpy(batch_buffer + current_length, single_message_buffer.get(), static_cast<size_t>(msg_size));
            current_length += static_cast<size_t>(msg_size);
            
            msg_queue->removeFront();
            batch_count++;
        }

        // Add trailer if we have any messages
        if (batch_count > 0 && trailer_size > 0) {
            memcpy(batch_buffer + current_length, GetMessageTrailer_(), trailer_size);
            current_length += static_cast<size_t>(trailer_size);
        }

        // Null terminate the batch buffer
        batch_buffer[current_length] = '\0';

        Logger::debug2("MessageBatcher::BatchEventsInternal()> Batched %d messages\n", batch_count);
        Logger::debug3("MessageBatcher::BatchEventsInternal()> Final batch: '%s'\n", batch_buffer);
    }
    catch (const std::exception& e) {
        Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Exception during batch processing: %s\n", e.what());
        return 0;
    }

    return batch_count;
}