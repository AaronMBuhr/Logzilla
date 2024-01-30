#include "stdafx.h"

#include "MessageQueueLogMessageSender.h"

void MessageQueueLogMessageSender::handleJsonMessage(const char* json_message) {
	primary_message_queue_->lock();
	primary_message_queue_->enqueue(json_message, (const int) strlen(json_message));
	primary_message_queue_->unlock();
	secondary_message_queue_->lock();
	secondary_message_queue_->enqueue(json_message, (const int)strlen(json_message));
	secondary_message_queue_->unlock();
}
