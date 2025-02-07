#pragma once

#include "MessageBatcher.h"

namespace Syslog_agent {

class HTTPMessageBatcher : public MessageBatcher {
public:
    static constexpr uint32_t MAX_MESSAGE_SIZE = 65536;         // Maximum size of a message batch in bytes
    static constexpr uint32_t MIN_BATCH_INTERVAL = 100;         // Minimum ms between batches
    static constexpr uint32_t MAX_BATCH_SIZE = 1000;            // Maximum messages in a single batch

private:
    // Store string literals as member variables to return char*
    static constexpr const char* HEADER = "{ \"events\": [ ";
    static constexpr const char* SEPARATOR = ", ";
    static constexpr const char* TRAILER = " ] }";
    
    char header_[16];      // Size of "{ \"events\": [ " + null terminator
    char separator_[3];     // Size of ", " + null terminator
    char trailer_[5];      // Size of " ] }" + null terminator

public:
    HTTPMessageBatcher() : MessageBatcher() {
        strcpy_s(header_, sizeof(header_), HEADER);
        strcpy_s(separator_, sizeof(separator_), SEPARATOR);
        strcpy_s(trailer_, sizeof(trailer_), TRAILER);
    }
    ~HTTPMessageBatcher() {}

    uint32_t GetMaxMessageSize_() const override { return MAX_MESSAGE_SIZE; }
    uint32_t GetMinBatchInterval_() const override { return MIN_BATCH_INTERVAL; }
    uint32_t GetMaxBatchSize_() const override { return MAX_BATCH_SIZE; }
    
    void GetMessageHeader_(char* dest, size_t max_size, size_t& size_out) const override {
        size_t len = strlen(header_);
        if (len < max_size) {
            memcpy(dest, header_, len);
            size_out = len;
        } else {
            size_out = 0;
        }
    }
    
    void GetMessageSeparator_(char* dest, size_t max_size, size_t& size_out) const override {
        size_t len = strlen(separator_);
        if (len < max_size) {
            memcpy(dest, separator_, len);
            size_out = len;
        } else {
            size_out = 0;
        }
    }
    
    void GetMessageTrailer_(char* dest, size_t max_size, size_t& size_out) const override {
        size_t len = strlen(trailer_);
        if (len < max_size) {
            memcpy(dest, trailer_, len);
            size_out = len;
        } else {
            size_out = 0;
        }
    }
};

}