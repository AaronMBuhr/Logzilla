#pragma once

#include "JsonLogMessageHandler.h"
#include "MessageQueue.h"

class MessageQueueLogMessageSender : public JsonLogMessageHandler {

public:
	MessageQueueLogMessageSender(shared_ptr<MessageQueue> primary_message_queue, shared_ptr<MessageQueue> secondary_message_queue) 
		: primary_message_queue_(primary_message_queue), secondary_message_queue_(secondary_message_queue) { };
	void handleJsonMessage(const char* json_message) override;

private:
	shared_ptr<MessageQueue> primary_message_queue_;
	shared_ptr<MessageQueue> secondary_message_queue_;
};

