#include "stdafx.h"

#include "MessageQueueLogMessageSender.h"

void MessageQueueLogMessageSender::handleJsonMessage(const char* json_message) {
	message_queue_.lock();
	message_queue_.enqueue(json_message, (const int) strlen(json_message));
	message_queue_.unlock();
}
