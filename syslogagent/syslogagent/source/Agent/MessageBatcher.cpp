#include "stdafx.h"
#include "Globals.h"
#include "Logger.h"
#include "MessageBatcher.h"

namespace Syslog_agent {

MessageBatcher::MessageBatcher()
{
}

MessageBatcher::~MessageBatcher()
{
}

MessageBatcher::BatchResult MessageBatcher::BatchEvents(
    shared_ptr<MessageQueue> msg_queue, 
    char* batch_buffer,
    size_t buffer_size) const
{
    if (!batch_buffer || buffer_size == 0) {
        return BatchResult(BatchResult::Status::InvalidBuffer);
    }
    return BatchEventsInternal(msg_queue, batch_buffer, buffer_size);
}

MessageBatcher::BatchResult MessageBatcher::BatchEventsInternal(
    shared_ptr<MessageQueue> msg_queue,
    char* batch_buffer,
    size_t buffer_size) const
{
    int queue_length = msg_queue->length();
    Logger::debug3("MessageBatcher::BatchEventsInternal() Initial queue length: %d\n", queue_length);

    if (queue_length == 0) {
        return BatchResult(BatchResult::Status::NoMessages);
    }

    try {
        // Pre-calculate header, separator, and trailer sizes
        size_t header_size = 0;
        size_t separator_size = 0;
        size_t trailer_size = 0;
        
        char size_check_buffer[64]; // Small stack buffer for size checks
        GetMessageHeader_(size_check_buffer, sizeof(size_check_buffer), header_size);
        GetMessageSeparator_(size_check_buffer, sizeof(size_check_buffer), separator_size);
        GetMessageTrailer_(size_check_buffer, sizeof(size_check_buffer), trailer_size);

        // Validate sizes
        if (header_size >= GetMaxMessageSize_()) {
            Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Header too large\n");
            return BatchResult(BatchResult::Status::MessageTooLarge);
        }

        // Check if buffer can hold at least header + trailer
        if (buffer_size < header_size + trailer_size) {
            Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Buffer too small for header and trailer\n");
            return BatchResult(BatchResult::Status::BufferTooSmall);
        }

        // Copy header
        if (header_size > 0) {
            GetMessageHeader_(batch_buffer, buffer_size, header_size);
        }
        size_t current_length = header_size;

        // Limit batch size
        int max_batch = std::min<int>(queue_length, static_cast<int>(GetMaxBatchSize_()));
        Logger::debug3("MessageBatcher::BatchEventsInternal()> Will process up to %d messages\n", max_batch);

        uint32_t batch_count = 0;
        auto queue_iter = msg_queue->traverseQueue();
        auto iter_end = std::experimental::generator<MessageQueue::Message*>::iterator{};
        
        for (auto iter = queue_iter.begin(); iter != iter_end && batch_count < max_batch; ++iter) {
            auto* msg = *iter;
            if (!msg || msg->data_length <= 0) {
                Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Message with zero length, discarding\n");
                continue;
            }

            if (msg->data_length >= GetMaxMessageSize_()) {
                Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Message too large\n");
                continue;
            }

            // Calculate total size needed for this message
            size_t needed_size = msg->data_length + 
                (batch_count > 0 ? separator_size : 0) + 
                (batch_count == max_batch - 1 ? trailer_size : 0);

            // Check if adding this message would exceed buffer size
            if (current_length + needed_size >= buffer_size) {
                Logger::debug2("MessageBatcher::BatchEventsInternal()> Buffer full, stopping batch\n");
                break;
            }

            // Add separator if this isn't the first message
            if (batch_count > 0 && separator_size > 0) {
                GetMessageSeparator_(batch_buffer + current_length, 
                                   buffer_size - current_length,
                                   separator_size);
                current_length += separator_size;
            }

            // Copy the message
            int size = msg_queue->peek(msg, 
                                     batch_buffer + current_length,
                                     static_cast<uint32_t>(buffer_size - current_length));
            if (size <= 0) {
                Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Failed to peek message, skipping\n");
                continue;
            }
            current_length += size;
            batch_count++;

            if (batch_count >= max_batch) {
                break;
            }
        }

        // Add trailer if we have any messages
        if (batch_count > 0 && trailer_size > 0) {
            GetMessageTrailer_(batch_buffer + current_length,
                             buffer_size - current_length,
                             trailer_size);
            current_length += trailer_size;
        }

        // Ensure null termination if space allows
        if (current_length < buffer_size) {
            batch_buffer[current_length] = '\0';
        }

        Globals::instance()->batched_count_ += batch_count;
        Logger::always("***** Message counts: queued %d, peeked %d, batched %d\n", 
                      Globals::instance()->queued_count_, 
                      Globals::instance()->peek_count_, 
                      Globals::instance()->batched_count_);

        return BatchResult(
            batch_count > 0 ? BatchResult::Status::Success : BatchResult::Status::NoMessages,
            batch_count,
            current_length
        );
    }
    catch (const std::exception& e) {
        Logger::recoverable_error("MessageBatcher::BatchEventsInternal()> Exception: %s\n", e.what());
        return BatchResult(BatchResult::Status::InvalidBuffer);
    }
}
}