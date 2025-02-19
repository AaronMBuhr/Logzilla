#include "stdafx.h"
#include "MessageQueue.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace Syslog_agent {

MessageQueue::MessageQueue(uint32_t message_queue_size, uint32_t message_buffers_chunk_size)
    : message_queue_chunk_size_(message_buffers_chunk_size),
      length_(0),
      items_sem_(0)
{
    messages_pool_ = std::make_unique<BitmappedObjectPool<Message>>(message_queue_size, MESSAGE_QUEUE_SLACK_PERCENT);
    message_buffers_pool_ = std::make_unique<BitmappedObjectPool<MessageBuffer>>(message_buffers_chunk_size, MESSAGE_QUEUE_SLACK_PERCENT);
}

MessageQueue::~MessageQueue() {
    // Clean up any remaining messages
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (first_message_) {
        removeFrontInternal();
    }
}

void MessageQueue::releaseMessageBuffers(Message& msg) {
    MessageBuffer* current = msg.message_buffers;
    while (current) {
        MessageBuffer* next = current->next;
        current->next = nullptr;
        message_buffers_pool_->markAsUnused(current);
        current = next;
    }
    msg.buffer_count = 0;
    msg.message_buffers = nullptr;
}

MessageQueue::Message* MessageQueue::createMessage(const char* message_content, const uint32_t message_len, uint64_t timestamp) {
    auto logger = LOG_THIS;
    Message* msg = messages_pool_->getAndMarkNextUnused();
    if (!msg) {
        logger->recoverable_error("MessageQueue::createMessage() : failed to allocate message\n");
        return nullptr;
    }

    // Initialize message before allocating buffers to ensure clean state on error
    msg->next = nullptr;
    msg->timestamp = timestamp;
    msg->data_length = message_len;
    msg->buffer_count = 0;
    msg->message_buffers = nullptr;

    try {
        uint32_t remaining = message_len;
        const char* ptr = message_content;
        MessageBuffer* lastBuffer = nullptr;

        while (remaining > 0) {
            MessageBuffer* buffer = message_buffers_pool_->getAndMarkNextUnused();
            if (!buffer) {
                logger->recoverable_error("MessageQueue::createMessage() : failed to allocate message buffer\n");
                throw std::runtime_error("Buffer allocation failed");
            }

            uint32_t toCopy = (std::min)(remaining, static_cast<uint32_t>(MESSAGE_BUFFER_SIZE));
            memcpy(buffer->buffer, ptr, toCopy);
            buffer->next = nullptr;

            if (!msg->message_buffers) {
                msg->message_buffers = buffer;
            } else {
                lastBuffer->next = buffer;
            }
            lastBuffer = buffer;

            remaining -= toCopy;
            ptr += toCopy;
            msg->buffer_count++;

            if (msg->buffer_count > MAX_BUFFERS_PER_MESSAGE) {
                logger->recoverable_error("MessageQueue::createMessage() : message requires more than MAX_BUFFERS_PER_MESSAGE\n");
                throw std::runtime_error("Too many buffers required");
            }
        }
        return msg;
    }
    catch (...) {
        // Clean up on any error
        releaseMessageBuffers(*msg);
        messages_pool_->markAsUnused(msg);
        return nullptr;
    }
}

bool MessageQueue::enqueue(const char* message_content, const uint32_t message_len) {
    auto logger = LOG_THIS;
    if (!message_content || message_len == 0 || message_len >= MESSAGE_BUFFER_SIZE * MAX_BUFFERS_PER_MESSAGE) {
        logger->recoverable_error("MessageQueue::enqueue() : invalid parameters\n");
        return false;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);

    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    Message* msg = createMessage(message_content, message_len, timestamp);
    if (!msg) {
        return false;
    }

    if (enqueue_hook_ && !enqueue_hook_(length_,msg, false)) {
            return false; // Handler cancelled the enqueue
    }

    if (!first_message_) {
        first_message_ = msg;
        last_message_ = msg;
    } else {
        last_message_->next = msg;
        last_message_ = msg;
    }
    length_++;
    
    // Post-enqueue handler
    if (enqueue_hook_) {
        enqueue_hook_(length_, msg, true);
    }

    items_sem_.release();
    items_cv_.notify_one();  // Only need to notify one waiter
    return true;
}

int MessageQueue::peek(Message* msg, char* message_content, const uint32_t max_len) const {
    auto logger = LOG_THIS;
    if (!message_content || max_len == 0) {
        logger->recoverable_error("MessageQueue::peek() : invalid parameters\n");
        return -1;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (msg == nullptr) {
        msg = first_message_;
        if (!msg) {
            logger->debug("MessageQueue::peek() : queue is empty\n");
            return -1;
        }
    }

    // Validate the message belongs to this queue
    if (!messages_pool_->belongs(msg)) {
        logger->recoverable_error("MessageQueue::peek() : invalid message pointer\n");
        return -1;
    }

    if (msg->data_length > max_len) {
        logger->recoverable_error("MessageQueue::peek() : message length %u exceeds buffer size %u\n", msg->data_length, max_len);
        return -1;
    }

    int copied = 0;
    for (MessageBuffer* buffer = msg->message_buffers; buffer != nullptr; buffer = buffer->next) {
        uint32_t toCopy = (std::min)(msg->data_length - static_cast<uint32_t>(copied), static_cast<uint32_t>(MESSAGE_BUFFER_SIZE));
        memcpy(message_content + copied, buffer->buffer, toCopy);
        copied += toCopy;
    }

    logger->debug2("MessageQueue::peek() Successfully peeked message with length %d\n", msg->data_length);
    return msg->data_length;
}

void MessageQueue::removeFrontInternal() {
    auto logger = LOG_THIS;
    if (!first_message_) {
        logger->recoverable_error("MessageQueue::removeFrontInternal() : queue empty\n");
        return;
    }

    Message* msg = first_message_;
    first_message_ = first_message_->next;
    if (!first_message_) {
        last_message_ = nullptr;
    }
    length_--;

    // Set next to nullptr before releasing to prevent any dangling pointer access
    msg->next = nullptr;
    
    // Release buffers first
    releaseMessageBuffers(*msg);
    
    // Clear message fields before marking as unused
    msg->buffer_count = 0;
    msg->data_length = 0;
    msg->timestamp = 0;
    msg->message_buffers = nullptr;
    
    // Finally mark as unused
    messages_pool_->markAsUnused(msg);
}

int MessageQueue::dequeue(char* message_content, const uint32_t max_len) {
    auto logger = LOG_THIS;
    if (!message_content || max_len == 0) {
        logger->recoverable_error("MessageQueue::dequeue() : invalid parameters\n");
        return -1;
    }

    std::unique_lock<std::mutex> lock(queue_mutex_);

    if (!first_message_) {
        logger->debug("MessageQueue::dequeue() : queue is empty\n");
        return -1;
    }

    if (first_message_->data_length > max_len) {
        logger->recoverable_error("MessageQueue::dequeue() : message length %u exceeds buffer size %u\n", 
            first_message_->data_length, max_len);
        return -1;
    }

    int copied = 0;
    for (MessageBuffer* buffer = first_message_->message_buffers; buffer != nullptr; buffer = buffer->next) {
        uint32_t toCopy = (std::min)(first_message_->data_length - static_cast<uint32_t>(copied), 
            static_cast<uint32_t>(MESSAGE_BUFFER_SIZE));
        memcpy(message_content + copied, buffer->buffer, toCopy);
        copied += toCopy;
    }

    int length = first_message_->data_length;
    removeFrontInternal();

    logger->debug2("MessageQueue::dequeue() Successfully dequeued message with length %d\n", length);
    return length;
}

bool MessageQueue::removeFront() {
    auto logger = LOG_THIS;
    items_sem_.acquire();
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (!first_message_) {
        logger->debug("MessageQueue::removeFront() : queue is empty\n");
        return false;
    }

    removeFrontInternal();
    return true;
}

std::experimental::generator<MessageQueue::Message*> MessageQueue::traverseQueue(Message* first) const {
    auto logger = LOG_THIS;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    Message* current = first ? first : first_message_;
    while (current) {
        if (!messages_pool_->belongs(current)) {
            logger->recoverable_error("MessageQueue::traverseQueue() : invalid message pointer\n");
            break;
        }
        co_yield current;
        current = current->next;
    }
}

}
