#pragma once

#include "MessageBatcher.h"

namespace Syslog_agent {

class HTTPMessageBatcher : public MessageBatcher {
public:
    static constexpr uint32_t MAX_MESSAGE_SIZE = 65536;         // Maximum size of a message batch in bytes
    static constexpr uint32_t MIN_BATCH_INTERVAL = 100;         // Minimum ms between batches

private:
    // Store string literals as member variables to return char*
    static constexpr const char* HEADER = "{ \"events\": [ ";
    static constexpr const char* SEPARATOR = ", ";
    static constexpr const char* TRAILER = " ] }";

public:
    HTTPMessageBatcher(uint32_t max_batch_size, uint32_t max_batch_age) 
        : MessageBatcher(max_batch_size, max_batch_age) {
    }
    ~HTTPMessageBatcher() {}

    uint32_t GetMaxMessageSize_() const override { return MAX_MESSAGE_SIZE; }
    uint32_t GetMinBatchInterval_() const override { return MIN_BATCH_INTERVAL; }
    
    void GetMessageHeader_(char* dest, size_t max_size, size_t& size_out) const override {
        size_t len = strlen(HEADER);
        if (len < max_size) {
            memcpy(dest, HEADER, len);
            size_out = len;
        } else {
            size_out = 0;
        }
    }
    
    void GetMessageSeparator_(char* dest, size_t max_size, size_t& size_out) const override {
        size_t len = strlen(SEPARATOR);
        if (len < max_size) {
            memcpy(dest, SEPARATOR, len);
            size_out = len;
        } else {
            size_out = 0;
        }
    }
    
    void GetMessageTrailer_(char* dest, size_t max_size, size_t& size_out) const override {
        size_t len = strlen(TRAILER);
        if (len < max_size) {
            memcpy(dest, TRAILER, len);
            size_out = len;
        } else {
            size_out = 0;
        }
    }
};

}