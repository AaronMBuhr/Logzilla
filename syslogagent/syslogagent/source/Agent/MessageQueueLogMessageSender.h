#pragma once

#include "JsonLogMessageHandler.h"
#include "MessageQueue.h"

class MessageQueueLogMessageSender : public JsonLogMessageHandler {

public:
	MessageQueueLogMessageSender(MessageQueue& message_queue) : message_queue_(message_queue) { };
	void handleJsonMessage(const char* json_message) override;

private:
	MessageQueue& message_queue_;
};

