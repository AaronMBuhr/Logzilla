#include "stdafx.h"
#include "MessageQueue.h"
#include <algorithm>
#include <chrono>
#include <cstring>

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
    Message* msg = messages_pool_->getAndMarkNextUnused();
    if (!msg) {
        Logger::recoverable_error("MessageQueue::createMessage() : failed to allocate message\n");
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
                Logger::recoverable_error("MessageQueue::createMessage() : failed to allocate message buffer\n");
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
                Logger::recoverable_error("MessageQueue::createMessage() : message requires more than MAX_BUFFERS_PER_MESSAGE\n");
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
    if (!message_content || message_len == 0 || message_len >= MESSAGE_BUFFER_SIZE * MAX_BUFFERS_PER_MESSAGE) {
        Logger::recoverable_error("MessageQueue::enqueue() : invalid parameters\n");
        return false;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);

    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    Message* msg = createMessage(message_content, message_len, timestamp);
    if (!msg) {
        return false;
    }

    if (!first_message_) {
        first_message_ = msg;
        last_message_ = msg;
    } else {
        last_message_->next = msg;
        last_message_ = msg;
    }
    length_++;
    items_sem_.release();
    items_cv_.notify_one();  // Only need to notify one waiter
    return true;
}

int MessageQueue::peek(Message* msg, char* message_content, const uint32_t max_len) const {
    if (!message_content || max_len == 0) {
        Logger::recoverable_error("MessageQueue::peek() : invalid parameters\n");
        return -1;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (msg == nullptr) {
        msg = first_message_;
        if (!msg) {
            Logger::debug("MessageQueue::peek() : queue is empty\n");
            return -1;
        }
    }

    // Validate the message belongs to this queue
    if (!messages_pool_->belongs(msg)) {
        Logger::recoverable_error("MessageQueue::peek() : invalid message pointer\n");
        return -1;
    }

    if (msg->data_length > max_len) {
        Logger::recoverable_error("MessageQueue::peek() : message length %u exceeds buffer size %u\n", msg->data_length, max_len);
        return -1;
    }

    int copied = 0;
    for (MessageBuffer* buffer = msg->message_buffers; buffer != nullptr; buffer = buffer->next) {
        uint32_t toCopy = (std::min)(msg->data_length - static_cast<uint32_t>(copied), static_cast<uint32_t>(MESSAGE_BUFFER_SIZE));
        memcpy(message_content + copied, buffer->buffer, toCopy);
        copied += toCopy;
    }

    Logger::debug2("MessageQueue::peek() Successfully peeked message with length %d\n", msg->data_length);
    return msg->data_length;
}

void MessageQueue::removeFrontInternal() {
    if (!first_message_) {
        Logger::recoverable_error("MessageQueue::removeFrontInternal() : queue empty\n");
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
    if (!message_content || max_len == 0) {
        return -1;
    }

    // First acquire the semaphore to ensure message availability
    items_sem_.acquire();
    
    // Then lock the mutex for the actual dequeue operation
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (!first_message_) {
        Logger::recoverable_error("MessageQueue::dequeue() : queue empty after semaphore acquire\n");
        items_sem_.release(); // Release the semaphore since we're not consuming a message
        return -1;
    }

    int ret = peek(first_message_, message_content, max_len);
    if (ret == -1) {
        Logger::recoverable_error("MessageQueue::dequeue() : peek failed\n");
        items_sem_.release(); // Release the semaphore since we're not consuming a message
        return -1;
    }

    removeFrontInternal();
    return ret;
}

bool MessageQueue::removeFront() {
    items_sem_.acquire();
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (!first_message_) {
        Logger::recoverable_error("MessageQueue::removeFront() : queue empty after semaphore acquire\n");
        items_sem_.release(); // Release the semaphore since we're not consuming a message
        return false;
    }

    removeFrontInternal();
    return true;
}

std::experimental::generator<MessageQueue::Message*> MessageQueue::traverseQueue(Message* first) {
    Message* current;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        current = first ? first : first_message_;
        // Take a snapshot of the current message - we'll validate it belongs before yielding
        if (current && !messages_pool_->belongs(current)) {
            Logger::recoverable_error("MessageQueue::traverseQueue() : invalid message pointer\n");
            current = nullptr;
        }
    }
    
    while (current) {
        co_yield current;
        
        Message* next;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            next = current->next;
            if (next && !messages_pool_->belongs(next)) {
                Logger::recoverable_error("MessageQueue::traverseQueue() : invalid next message pointer\n");
                next = nullptr;
            }
        }
        current = next;
    }
}