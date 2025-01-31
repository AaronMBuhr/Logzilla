/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>
#include <semaphore>
#include "ArrayQueue.h"
#include "BitmappedObjectPool.h"
#include "Logger.h"

class MessageQueue
{
public:
    static constexpr unsigned int MAX_BUFFERS_PER_MESSAGE = 32;
    static constexpr int MESSAGE_BUFFER_SIZE = 2048;
    static constexpr int MESSAGE_QUEUE_SLACK_PERCENT = 80;

    MessageQueue(
        unsigned int message_queue_size,
        unsigned int message_buffers_chunk_size
    );
    
    bool isEmpty() const {
        std::lock_guard<std::recursive_mutex> lock(queue_mutex_);
        return send_buffers_queue_->isEmpty(); 
    }
    
    bool isFull() const { 
        std::lock_guard<std::recursive_mutex> lock(queue_mutex_);
        return send_buffers_queue_->isFull(); 
    }
    
    bool enqueue(const char* message_content, const uint32_t message_len);
    int dequeue(char* message_content, const uint32_t max_len);
    int peek(char* message_content, const uint32_t max_len, const uint32_t item_index = 0) const;
    bool removeFront();
    
    int length() const { 
        std::lock_guard<std::recursive_mutex> lock(queue_mutex_);
        return static_cast<int>(send_buffers_queue_->length()); 
    }
    
    // Wait for messages to be available or until timeout_ms milliseconds have elapsed
    // Returns true if messages are available, false if timed out
    bool waitForMessages(uint32_t timeout_ms) {
        std::unique_lock<std::recursive_mutex> lock(queue_mutex_);
        return items_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this]() { return !send_buffers_queue_->isEmpty(); });
    }

    template <typename Func>
    auto runInsideLock(Func&& func) -> decltype(func()) {
        std::lock_guard<std::recursive_mutex> lock(queue_mutex_);
        return func();
    }

private:
    struct MessageBuffer {
        char buffer[MESSAGE_BUFFER_SIZE];
    };
    
    struct Message {
        MessageBuffer* message_buffers[MAX_BUFFERS_PER_MESSAGE];
        uint32_t data_length;
        uint32_t buffer_count;
    };

    void releaseMessageBuffers(Message& message);

    const uint32_t message_queue_size_;
    const uint32_t message_queue_chunk_size_;
    std::atomic<uint32_t> in_use_counter_;
    
    mutable std::recursive_mutex queue_mutex_;
    std::condition_variable_any items_cv_;  // Use condition_variable_any for recursive_mutex
    std::unique_ptr<ArrayQueue<Message>> send_buffers_queue_;
    std::unique_ptr<BitmappedObjectPool<MessageBuffer>> send_buffers_;
    std::counting_semaphore<> items_sem_{ 0 };
};