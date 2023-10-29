#include "stdafx.h"
#include "MessageQueue.h"


MessageQueue::MessageQueue(
	int message_queue_size,
	int message_buffers_chunk_size
) :
	message_queue_size_(message_queue_size),
	message_queue_chunk_size_(message_buffers_chunk_size),
	in_use_counter_(0)
{
	send_buffers_queue_ = make_unique<ArrayQueue<Message>>(message_queue_size_);
	send_buffers_ = make_unique<BitmappedUsageCollection<MessageBuffer>>(message_buffers_chunk_size, MESSAGE_QUEUE_SLACK_PERCENT);
}


bool MessageQueue::enqueue(const char* message_content, const int message_len) {
	if (isFull()) {
		return false;
	}
	Message message;
	ZeroMemory(&message, sizeof(message));
	int leftover = message_len % MESSAGE_BUFFER_SIZE;
	int num_chunks = message_len / MESSAGE_BUFFER_SIZE + (leftover == 0 ? 0 : 1);
	char* bufptr = const_cast<char*>(message_content);
	message.data_length = message_len;
	for (int c = 0; c < num_chunks; ++c) {
		MessageBuffer* buffer = send_buffers_->getAndMarkNextUnused();
		int buffer_size = (leftover == 0 || c < num_chunks - 1) ? MESSAGE_BUFFER_SIZE : leftover;
		memcpy(buffer->buffer, bufptr, buffer_size);
		message.message_buffers[c] = buffer;
		bufptr += MESSAGE_BUFFER_SIZE;
	}
	bool result = send_buffers_queue_->enqueue(std::move(message));
	return result;
}


int MessageQueue::dequeue(char* message_content, const int max_len) {
	Message message;
	bool error = false;
	if (send_buffers_queue_->isEmpty()) {
		return -1;
	}
	modifying_.lock();
	if (!send_buffers_queue_->peek(message)) {
		Logger::critical("MessageQueue::dequeue() : could not peek queue\n");
		error = true;
	}
	if (!error && message.data_length > max_len) {
		Logger::critical("MessageQueue::dequeue() : message data size %d exceeds dequeue size %d\n", message.data_length, max_len);
		error = true;
	}

	if (!error && send_buffers_queue_->removeFront(message)) {
		Logger::critical("MessageQueue::dequeue() : could not release peeked message\n");
		error = true;
	}
	modifying_.unlock();
	if (error) {
		return -1;
	}
	int leftover = message.data_length % MESSAGE_BUFFER_SIZE;
	int num_chunks = message.data_length / MESSAGE_BUFFER_SIZE + (leftover == 0 ? 0 : 1);
	char* bufptr = const_cast<char*>(message_content);
	for (int c = 0; c < num_chunks; ++c) {
		int copy_size = (leftover == 0 || c < num_chunks - 1) ? MESSAGE_BUFFER_SIZE : leftover;
		memcpy(bufptr, message.message_buffers[c]->buffer, copy_size);
		send_buffers_->markUnused(message.message_buffers[c]);
		bufptr += MESSAGE_BUFFER_SIZE;
	}
	return message.data_length;
}


int MessageQueue::peek(char* message_content, const int max_len) {
	Message message;
	bool error = false;
	if (send_buffers_queue_->isEmpty()) {
		return -1;
	}
	if (!send_buffers_queue_->peek(message)) {
		Logger::critical("MessageQueue::dequeue() : could not peek queue\n");
		error = true;
	}
	if (!error && message.data_length > max_len) {
		Logger::critical("MessageQueue::dequeue() : message data size %d exceeds dequeue size %d\n", message.data_length, max_len);
		error = true;
	}
	if (error) {
		return -1;
	}
	int leftover = message.data_length % MESSAGE_BUFFER_SIZE;
	int num_chunks = message.data_length / MESSAGE_BUFFER_SIZE + (leftover == 0 ? 0 : 1);
	char* bufptr = const_cast<char*>(message_content);
	for (int c = 0; c < num_chunks; ++c) {
		int copy_size = (leftover == 0 || c < num_chunks - 1) ? MESSAGE_BUFFER_SIZE : leftover;
		memcpy(bufptr, message.message_buffers[c]->buffer, copy_size);
		send_buffers_->markUnused(message.message_buffers[c]);
		bufptr += MESSAGE_BUFFER_SIZE;
	}
	return message.data_length;

}


bool MessageQueue::removeFront() {
	Message message;
	if (send_buffers_queue_->isEmpty()) {
		return false;
	}
	bool was_error = false;
	const std::lock_guard<std::recursive_mutex> lock(modifying_);
	if (!send_buffers_queue_->peek(message)) {
		Logger::critical("MessageQueue::dequeue() : could not peek queue\n");
		was_error = true;
	}
	else {
		send_buffers_queue_->removeFront();
		int leftover = message.data_length % MESSAGE_BUFFER_SIZE;
		int num_chunks = message.data_length / MESSAGE_BUFFER_SIZE + (leftover == 0 ? 0 : 1);
		for (int c = 0; c < num_chunks; ++c) {
			send_buffers_->markUnused(message.message_buffers[c]);
		}
	}
	if (was_error) {
		return false;
	}
	return true;
}
