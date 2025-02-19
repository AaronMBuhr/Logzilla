#include "pch.h"
#include "../Infrastructure/Logger.h"
#include "MessageBatcher.h"

namespace Syslog_agent {

    MessageBatcher::MessageBatcher(uint32_t max_batch_size, uint32_t max_batch_age)
        : max_batch_size_(max_batch_size), max_batch_age_(max_batch_age)
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
        auto logger = LOG_THIS;
        if (!batch_buffer || buffer_size == 0) {
            return BatchResult(BatchResult::Status::InvalidBuffer);
        }
        auto result = BatchEventsInternal(msg_queue, batch_buffer, buffer_size);

        // DEBUGGING
        if (result.status == BatchResult::Status::Success && result.bytes_written > 0) {
            // Get trailer to verify
            char trailer_check[64];
            size_t trailer_size;
            GetMessageTrailer_(trailer_check, sizeof(trailer_check), trailer_size);

            if (trailer_size > 0 && result.bytes_written >= trailer_size) {
                // check if batch ends with trailer
                if (memcmp(batch_buffer + result.bytes_written - trailer_size,
                    trailer_check, trailer_size) != 0) {
                    logger->recoverable_error("MessageBatcher::BatchEvents()> Batch does not end with trailer\n");
                }
            }
        }
        return result;
    }

    MessageBatcher::BatchResult MessageBatcher::BatchEventsInternal(
        shared_ptr<MessageQueue> msg_queue,
        char* batch_buffer,
        size_t buffer_size) const
    {
        auto logger = LOG_THIS;
        int queue_length = msg_queue->length();
        logger->debug3("MessageBatcher::BatchEventsInternal() Initial queue length: %d\n", queue_length);

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
                logger->recoverable_error("MessageBatcher::BatchEventsInternal()> Header too large\n");
                return BatchResult(BatchResult::Status::MessageTooLarge);
            }

            int max_batch = std::min<int>(queue_length, static_cast<int>(max_batch_size_));
            logger->debug3("MessageBatcher::BatchEventsInternal()> Will process max %d messages\n", max_batch);

            // Copy header
            if (header_size > 0) {
                GetMessageHeader_(batch_buffer, buffer_size, header_size);
            }
            size_t current_length = header_size;

            logger->debug3("MessageBatcher::BatchEventsInternal()> Will process max %d messages\n", max_batch);

            uint32_t batch_count = 0;
            auto queue_iter = msg_queue->traverseQueue();
            auto iter_end = std::experimental::generator<MessageQueue::Message*>::iterator{};

            for (auto iter = queue_iter.begin(); iter != iter_end && batch_count < max_batch; ++iter) {
                auto* msg = *iter;
                if (!msg || msg->data_length <= 0) {
                    logger->recoverable_error("MessageBatcher::BatchEventsInternal()> Message with zero length, discarding\n");
                    continue;
                }

                if (msg->data_length >= GetMaxMessageSize_()) {
                    logger->recoverable_error("MessageBatcher::BatchEventsInternal()> Message too large\n");
                    continue;
                }

                // Calculate total size needed for this message
                size_t message_space_needed = msg->data_length;
                if (batch_count > 0) {
                    message_space_needed += separator_size;
                }

                // Check if adding this message would exceed available space
                if (current_length + message_space_needed + trailer_size > buffer_size) {
                    logger->debug2("MessageBatcher::BatchEventsInternal()> Buffer space limit reached at %zu/%zu bytes\n",
                        current_length, buffer_size);
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
                    logger->recoverable_error("MessageBatcher::BatchEventsInternal()> Failed to peek message, skipping\n");
                    continue;
                }
                current_length += size;
                batch_count++;

                if (batch_count >= max_batch) {
                    logger->debug3("MessageBatcher::BatchEventsInternal()> Batch count %d reached max batch size %d\n", batch_count, max_batch);
                    break;
                }
            }

            // Add trailer if we have any messages
            if (batch_count > 0) {
                if (trailer_size > 0) {
                    size_t temp_size;
                    GetMessageTrailer_(batch_buffer + current_length,
                        buffer_size - current_length,
                        temp_size);
                    current_length += temp_size;
                }
                else {
                    logger->debug("MessageBatcher::BatchEventsInternal()> Trailer size is 0, but messages were batched\n");
                }
            }
            else {
                // No messages added, reset buffer
                logger->debug("MessageBatcher::BatchEventsInternal()> Couldn't add trailer, no messages added\n");
                current_length = 0;
            }

            // Ensure null termination if space allows
            if (current_length < buffer_size) {
                batch_buffer[current_length] = '\0';
            }

#ifdef AFTER_SPLIT_THIS_NEEDS_FIXING
            Globals::instance()->batched_count_ += batch_count;
            logger->debug2("***** Message counts: queued %d, peeked %d, batched %d\n",
                Globals::instance()->queued_count_,
                Globals::instance()->peek_count_,
                Globals::instance()->batched_count_);
#endif

            return BatchResult(
                batch_count > 0 ? BatchResult::Status::Success : BatchResult::Status::NoMessages,
                batch_count,
                current_length
            );
        }
        catch (const std::exception& e) {
            logger->recoverable_error("MessageBatcher::BatchEventsInternal()> Exception: %s\n", e.what());
            return BatchResult(BatchResult::Status::InvalidBuffer);
        }
    }
}
