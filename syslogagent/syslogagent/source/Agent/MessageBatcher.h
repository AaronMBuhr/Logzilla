#pragma once

#include <memory>
#include "MessageQueue.h"

using std::shared_ptr;

class MessageBatcher
{
public:

    MessageBatcher();
    ~MessageBatcher();
    int BatchEvents(shared_ptr<MessageQueue> message_queue, char* batch_buffer) const;

protected:
    virtual uint32_t GetMaxMessageSize_() const = 0;
    virtual uint32_t GetBatchSizeThreshold_() const = 0;
    virtual uint32_t GetMinBatchInterval_() const = 0;
    virtual uint32_t GetMaxBatchSize_() const = 0;
    virtual char* GetMessageHeader_() const = 0;
    virtual char* GetMessageSeparator_() const = 0;
    virtual char* GetMessageTrailer_() const = 0;

    int BatchEventsInternal(shared_ptr<MessageQueue> message_queue, char* batch_buffer) const;

};