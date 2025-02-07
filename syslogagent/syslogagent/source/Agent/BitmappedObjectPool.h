/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#pragma once
#include "Bitmap.h"
#include <mutex>
#include <string>
#include <vector>

template <class T>
class BitmappedObjectPool
{
public:

    /* Note:    memory is allocated in chunks of multiples of the given template unit.
                percent_slack is used so that for a given chunk, if the chunks above it
                are empty/currently-unused then those extra chunks are available to be
                freed to release the memory.  0% slack means release extra chunks as
                soon as they are unneeded.  100% slack means wait until the current
                chunk is entirely unused before getting rid of ones above it. -1
                percent_slack means never free up chunks, keep them reserved forever. */

    BitmappedObjectPool(const int chunk_size, const int percent_slack) 
        : chunk_size_(chunk_size), percent_slack_(percent_slack) {
    }

    template <class U> BitmappedObjectPool(const BitmappedObjectPool<U>& old_obj) {
        usage_bitmaps_.reserve(old_obj.usage_bitmaps_.size());
        for (const auto& e : old_obj.usage_bitmaps_) {
            usage_bitmaps_.push_back(std::make_shared<Bitmap>(*e));
        }
        data_elements_.reserve(old_obj.data_elements_.size());
        for (const auto& e : old_obj.data_elements_) {
            data_elements_.push_back(std::make_shared<T[]>(*e));
        }
        chunk_size_ = old_obj.chunk_size_;
        percent_slack_ = old_obj.percent_slack_;
    }

    T* getAndMarkNextUnused() {
        // no maximum, if you do too much you'll get an out-of-memory error
        std::lock_guard<std::mutex> lock(in_use_);
        int32_t bitmap_index = -1;
        int bitnum = -1;
        for (unsigned int i = 0; i < usage_bitmaps_.size(); ++i) {
            int idx = usage_bitmaps_[i]->getAndSetFirstZero();
            if (idx >= 0) {
                bitmap_index = i;
                bitnum = idx;
                break;
            }
        }
        if (bitmap_index == -1) {
            // we didn't find a zero anywhere, so add another chunk
            usage_bitmaps_.push_back(std::make_unique<Bitmap>(chunk_size_, 0));
            data_elements_.push_back(std::make_unique<T[]>(chunk_size_));
            bitmap_index = static_cast<int32_t>(usage_bitmaps_.size() - 1);
            bitnum = usage_bitmaps_.back()->getAndSetFirstZero();
        }

        if (bitmap_index >= 0 && bitmap_index < static_cast<int32_t>(data_elements_.size())) {
            return &data_elements_[bitmap_index][bitnum];
        }
        return nullptr;
    }

    bool markAsUnused(T*& now_unused) {
        std::lock_guard<std::mutex> lock(in_use_);
        if (!belongs(now_unused)) {
            return false;
        }

        for (size_t i = 0; i < usage_bitmaps_.size(); ++i) {
            T* start_address = getPoolStart(i);
            if (now_unused >= start_address && now_unused <= getPoolEnd(i)) {
                // Calculate offset safely using ptrdiff_t
                std::ptrdiff_t offset = now_unused - start_address;
                // Validate offset is within expected range
                if (offset >= 0 && offset < chunk_size_) {
                    usage_bitmaps_[i]->setBitTo(static_cast<int>(offset), 0);

                    // Handle slack space management
                    if (percent_slack_ != -1) {
                        if (i < usage_bitmaps_.size() - 1) {
                            // Take a snapshot of the current state under lock
                            // We know max size is usage_bitmaps_.size() - (i + 1)
                            bool empty_above_us = true;
                            for (size_t cn = i + 1; cn < usage_bitmaps_.size() && empty_above_us; ++cn) {
                                empty_above_us = (usage_bitmaps_[cn]->countOnes() == 0);
                            }

                            // if empty above us then see if we have enough slack
                            if (empty_above_us) {
                                // Use int64_t to prevent overflow and maintain precision
                                int64_t number_of_zeroes = usage_bitmaps_[i]->countZeroes();
                                int64_t slack_ratio = (number_of_zeroes * 100LL) / static_cast<int64_t>(chunk_size_);
                                
                                if (slack_ratio >= percent_slack_) {
                                    // if empty above us and we have enough slack, 
                                    // get rid of extra chunk(s) atomically
                                    auto new_size = i + 1;
                                    usage_bitmaps_.resize(new_size);
                                    data_elements_.resize(new_size);
                                }
                            }
                        }
                    }
                    return true;
                }
                return false;  // Invalid offset
            }
        }
        return false;  // Not found in any chunk
    }

    bool belongs(const T* item) const {
        if (!item) return false;
        
        for (size_t i = 0; i < data_elements_.size(); ++i) {
            T* start = getPoolStart(i);
            T* end = getPoolEnd(i);
            if (item >= start && item <= end) {
                return true;
            }
        }
        return false;
    }

    bool isValidObject(const T* item) const {
        if (!item || !belongs(item)) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(in_use_);
        // Find which chunk this item belongs to
        for (size_t i = 0; i < data_elements_.size(); ++i) {
            T* start = getPoolStart(i);
            T* end = getPoolEnd(i);
            if (item >= start && item <= end) {
                // Calculate offset
                std::ptrdiff_t offset = item - start;
                if (offset >= 0 && offset < chunk_size_) {
                    // Check if this slot is marked as in use
                    return usage_bitmaps_[i]->isSet(static_cast<int>(offset));
                }
                return false;
            }
        }
        return false;
    }

    int countBuffers() const {
        int count = 0;
        for (auto& bm : usage_bitmaps_) {
            count += bm->countOnes();
        }
        return count;
    }

    const std::string asHexString() const {
        std::string result;
        for (auto& bm : usage_bitmaps_) {
            result.append(bm->asHexString());
        }
        return result;
    }

    const std::string asBinaryString() const {
        std::string result;
        for (auto& bm : usage_bitmaps_) {
            result.append(bm->asBinaryString());
        }
        return result;
    }

private:
    T* getPoolStart(size_t index) const {
        if (index < data_elements_.size()) {
            return &data_elements_[index][0];
        }
        return nullptr;
    }

    T* getPoolEnd(size_t index) const {
        if (index < data_elements_.size()) {
            return &data_elements_[index][chunk_size_ - 1];
        }
        return nullptr;
    }

    mutable std::mutex in_use_;
    std::vector<std::shared_ptr<Bitmap>> usage_bitmaps_;
    std::vector<std::shared_ptr<T[]>> data_elements_;
    int chunk_size_;
    int percent_slack_;
};
