#pragma once

#include "MessageBatcher.h"

namespace Syslog_agent {

class JSONMessageBatcher : public MessageBatcher {
public:
    static constexpr uint32_t MAX_MESSAGE_SIZE = 65536;         // Maximum size of a message batch in bytes
    static constexpr uint32_t BATCH_SIZE_THRESHOLD = 100;       // Messages per batch
    static constexpr uint32_t MIN_BATCH_INTERVAL = 100;         // Minimum ms between batches

private:
    // Store string literals as member variables to return char*
    static constexpr const char* HEADER = "";
    static constexpr const char* SEPARATOR = "\n";
    static constexpr const char* TRAILER = "";
    
    char header_[sizeof(HEADER)];      // Includes null terminator
    char separator_[sizeof(SEPARATOR)]; // Includes null terminator
    char trailer_[sizeof(TRAILER)];    // Includes null terminator

public:
    JSONMessageBatcher(uint32_t max_batch_size, uint32_t max_batch_age) 
        : MessageBatcher(max_batch_size, max_batch_age) {
        strcpy_s(header_, sizeof(header_), HEADER);
        strcpy_s(separator_, sizeof(separator_), SEPARATOR);
        strcpy_s(trailer_, sizeof(trailer_), TRAILER);
    }
    ~JSONMessageBatcher() {}

    uint32_t GetMaxMessageSize_() const override { return MAX_MESSAGE_SIZE; }
    uint32_t GetMinBatchInterval_() const override { return MIN_BATCH_INTERVAL; }
    
    void GetMessageHeader_(char* dest, size_t max_size, size_t& size_out) const override {
        size_out = strlen(header_);
        if (size_out < max_size) {
            strcpy_s(dest, max_size, header_);
        }
    }
    
    void GetMessageSeparator_(char* dest, size_t max_size, size_t& size_out) const override {
        size_out = strlen(separator_);
        if (size_out < max_size) {
            strcpy_s(dest, max_size, separator_);
        }
    }
    
    void GetMessageTrailer_(char* dest, size_t max_size, size_t& size_out) const override {
        size_out = strlen(trailer_);
        if (size_out < max_size) {
            strcpy_s(dest, max_size, trailer_);
        }
        else {
            dest[0] = '\0';
            size_out = 0;
        }
    }
};

}