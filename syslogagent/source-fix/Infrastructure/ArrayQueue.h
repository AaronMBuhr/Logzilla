#pragma once

#include <mutex>
#include <vector>
#include <stdexcept>
#include <utility>     // For std::move
#include "Logger.h"   // Assumes Logger provides debug(), debug2(), critical(), etc.

template <typename T>
class ArrayQueue {
public:
    // Constructor. Throws if size is not positive.
    // Note: The maximum number of storable items is (size - 1).
    explicit ArrayQueue(int size)
        : head_pos_(0), next_pos_(-1)
    {
        if (size <= 0) {
            throw std::invalid_argument("ArrayQueue size must be greater than 0");
        }
        data_.resize(size);
    }

    // Returns true if the queue is empty.
    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(data_locker_);
        return isEmptyLocked();
    }

    // Returns true if the queue is full.
    bool isFull() const {
        std::lock_guard<std::mutex> lock(data_locker_);
        return isFullLocked();
    }

    // Enqueue an element using move semantics.
    // Returns false if the queue is full.
    bool enqueue(T&& item) {
        std::lock_guard<std::mutex> lock(data_locker_);
        if (isFullLocked()) {
            auto logger = LOG_THIS;
            logger->critical("ArrayQueue::enqueue() Queue Full (length==%d)\n", (int)lengthLocked());
            return false;
        }
        // If the queue is empty, initialize next_pos_.
        if (isEmptyLocked()) {
            next_pos_ = head_pos_;  // (head_pos_ is typically 0 on first insertion)
        }
        data_[next_pos_] = std::move(item);
        // Advance next_pos_ to the next free slot.
        next_pos_ = (next_pos_ + 1) % data_.size();
        auto logger = LOG_THIS;
        logger->debug3("ArrayQueue::enqueue() success: head_pos_=%d, next_pos_=%d, length=%d\n",
            head_pos_, next_pos_, (int)lengthLocked());
        return true;
    }

    // Dequeue an element into 'item'. Returns false if the queue is empty.
    bool dequeue(T& item) {
        std::lock_guard<std::mutex> lock(data_locker_);
        if (isEmptyLocked()) {
            auto logger = LOG_THIS;
            logger->debug("ArrayQueue::dequeue() can't, queue is empty\n");
            return false;
        }
        item = std::move(data_[head_pos_]);
        head_pos_ = (head_pos_ + 1) % data_.size();
        // Only mark as empty if we've consumed all items
        if (head_pos_ == next_pos_) {
            next_pos_ = -1;  // Queue is now empty
        }
        auto logger = LOG_THIS;
        logger->debug3("ArrayQueue::dequeue() success: new head_pos_=%d, next_pos_=%d, length=%d\n",
            head_pos_, next_pos_, (int)lengthLocked());
        return true;
    }

    // Peek at an element (without removing it) at a given offset from the front.
    // Returns false if the queue is empty or if the requested index is out of range.
    bool peek(T& item, int item_index = 0) {
        std::lock_guard<std::mutex> lock(data_locker_);
        if (isEmptyLocked()) {
            auto logger = LOG_THIS;
            logger->debug("ArrayQueue::peek() can't, queue is empty\n");
            return false;
        }
        size_t len = lengthLocked();
        if (item_index < 0 || static_cast<size_t>(item_index) >= len) {
            auto logger = LOG_THIS;
            logger->debug("ArrayQueue::peek() index out of range\n");
            return false;
        }
        int idx = (head_pos_ + item_index) % data_.size();
        item = data_[idx];
        return true;
    }

    // Remove the front element only if it equals the provided value (using operator==).
    // Returns true if the element was removed.
    bool removeFront(const T& item) {
        std::lock_guard<std::mutex> lock(data_locker_);
        if (isEmptyLocked()) {
            auto logger = LOG_THIS;
            logger->debug("ArrayQueue::removeFront(item) can't, queue is empty\n");
            return false;
        }
        if (data_[head_pos_] == item) {
            head_pos_ = (head_pos_ + 1) % data_.size();
            if (head_pos_ == next_pos_) {
                next_pos_ = -1;
            }
            auto logger = LOG_THIS;
            logger->debug3("ArrayQueue::removeFront(item) success: new head_pos_=%d, next_pos_=%d, length=%d\n",
                head_pos_, next_pos_, (int)lengthLocked());
            return true;
        }
        return false;
    }

    // Remove the front element unconditionally.
    bool removeFront() {
        std::lock_guard<std::mutex> lock(data_locker_);
        if (isEmptyLocked()) {
            auto logger = LOG_THIS;
            logger->debug("ArrayQueue::removeFront() can't, queue is empty\n");
            return false;
        }
        head_pos_ = (head_pos_ + 1) % data_.size();
        if (head_pos_ == next_pos_) {
            next_pos_ = -1;
        }
        auto logger = LOG_THIS;
        logger->debug3("ArrayQueue::removeFront() success: new head_pos_=%d, next_pos_=%d, length=%d\n",
            head_pos_, next_pos_, (int)lengthLocked());
        return true;
    }

    // Return the number of items in the queue.
    size_t length() const {
        std::lock_guard<std::mutex> lock(data_locker_);
        return lengthLocked();
    }

private:
    // Internal helper: returns true if the queue is empty.
    // Assumes data_locker_ is already held.
    bool isEmptyLocked() const {
        return next_pos_ == -1;
    }

    // Internal helper: returns true if the queue is full.
    bool isFullLocked() const {
        if (next_pos_ == -1) return false;
        if (head_pos_ == next_pos_) return true;  // Queue has wrapped and filled completely
        size_t len = lengthLocked();
        return len >= data_.size();
    }

    // Internal helper: computes the current number of stored items.
    // Assumes data_locker_ is already held.
    size_t lengthLocked() const {
        if (isEmptyLocked()) {
            return 0;
        }
        if (head_pos_ <= next_pos_) {
            return next_pos_ - head_pos_;
        } else {
            return data_.size() - head_pos_ + next_pos_;
        }
    }

    std::vector<T> data_;
    int head_pos_; // Index of the front element.
    int next_pos_; // Next free index; -1 indicates the queue is empty.
    mutable std::mutex data_locker_;
};
