/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include "MessageQueue.h"

MessageQueue::MessageQueue(
	uint32_t message_queue_size,
	uint32_t message_buffers_chunk_size
) :
	message_queue_size_(message_queue_size),
	message_queue_chunk_size_(message_buffers_chunk_size),
	in_use_counter_(0)
{
	send_buffers_queue_ = make_unique<ArrayQueue<Message>>(message_queue_size_);
	send_buffers_ = make_unique<BitmappedObjectPool<MessageBuffer>>(message_buffers_chunk_size, MESSAGE_QUEUE_SLACK_PERCENT);
}

void MessageQueue::releaseMessageBuffers(Message& message) {
	for (uint32_t i = 0; i < message.buffer_count; ++i) {
		if (message.message_buffers[i]) {
			send_buffers_->markAsUnused(message.message_buffers[i]);
			message.message_buffers[i] = nullptr;
		}
	}
	message.buffer_count = 0;
}

bool MessageQueue::enqueue(const char* message_content, const uint32_t message_len) {
	if (message_len == 0) {
		return false;
	}

	const int num_chunks = (message_len + MESSAGE_BUFFER_SIZE - 1) / MESSAGE_BUFFER_SIZE;
	if (num_chunks > MAX_BUFFERS_PER_MESSAGE) {
		Logger::recoverable_error("MessageQueue::enqueue() : message requires %d chunks, exceeds maximum %d\n", 
			num_chunks, MAX_BUFFERS_PER_MESSAGE);
		return false;
	}

	std::unique_lock<std::recursive_mutex> lock(queue_mutex_);
	while (isFull()) {
		notify_cv_.wait(lock);
	}

	Message message = {};
	message.data_length = message_len;
	
	try {
		const char* src_ptr = message_content;
		uint32_t remaining = message_len;
		
		for (int i = 0; i < num_chunks; ++i) {
			MessageBuffer* buffer = send_buffers_->getAndMarkNextUnused();
			if (!buffer) {
				Logger::recoverable_error("MessageQueue::enqueue() : failed to allocate buffer %d of %d\n", 
					i, num_chunks);
				releaseMessageBuffers(message);
				return false;
			}
			
			const uint32_t chunk_size = (std::min)(remaining, static_cast<uint32_t>(MESSAGE_BUFFER_SIZE));
			memcpy(buffer->buffer, src_ptr, chunk_size);
			
			message.message_buffers[i] = buffer;
			message.buffer_count++;
			
			src_ptr += chunk_size;
			remaining -= chunk_size;
		}

		if (!send_buffers_queue_->enqueue(std::move(message))) {
			Logger::recoverable_error("MessageQueue::enqueue() : failed to enqueue message\n");
			releaseMessageBuffers(message);
			return false;
		}

		return true;
	}
	catch (...) {
		releaseMessageBuffers(message);
		throw;
	}
}

int MessageQueue::dequeue(char* message_content, const uint32_t max_len) {
	if (!message_content || max_len == 0) {
		return -1;
	}

	std::unique_lock<std::recursive_mutex> lock(queue_mutex_);

	Message message = {};
	if (!send_buffers_queue_->peek(message)) {
		return -1;  // Queue is empty
	}

	if (message.data_length > max_len) {
		Logger::recoverable_error("MessageQueue::dequeue() : message size %d exceeds buffer size %d\n", 
			message.data_length, max_len);
		return -1;
	}

	char* dest_ptr = message_content;
	uint32_t remaining = message.data_length;

	for (uint32_t i = 0; i < message.buffer_count; ++i) {
		const uint32_t chunk_size = (std::min)(remaining, static_cast<uint32_t>(MESSAGE_BUFFER_SIZE));
		memcpy(dest_ptr, message.message_buffers[i]->buffer, chunk_size);
		dest_ptr += chunk_size;
		remaining -= chunk_size;
	}

	const uint32_t bytes_copied = message.data_length;

	if (!send_buffers_queue_->removeFront()) {
		Logger::recoverable_error("MessageQueue::dequeue() : failed to remove message from queue\n");
		return -1;
	}

	releaseMessageBuffers(message);
	notify_cv_.notify_one();
	
	return bytes_copied;
}

int MessageQueue::peek(char* message_content, const uint32_t max_len, 
	const uint32_t item_index) const {
	if (!message_content || max_len == 0) {
		return -1;
	}

	std::lock_guard<std::recursive_mutex> lock(queue_mutex_);

	Message message = {};
	if (!send_buffers_queue_->peek(message, item_index)) {
		Logger::debug2("MessageQueue::peek() Queue is empty or index %d out of range\n", item_index);
		return -1;  // Queue is empty or index out of range
	}

	if (message.data_length > max_len) {
		Logger::recoverable_error("MessageQueue::peek() : message size %d exceeds buffer size %d\n", 
			message.data_length, max_len);
		return -1;
	}

	char* dest_ptr = message_content;
	uint32_t remaining = message.data_length;

	for (uint32_t i = 0; i < message.buffer_count; ++i) {
		const uint32_t chunk_size = (std::min)(remaining, static_cast<uint32_t>(MESSAGE_BUFFER_SIZE));
		memcpy(dest_ptr, message.message_buffers[i]->buffer, chunk_size);
		dest_ptr += chunk_size;
		remaining -= chunk_size;
	}

	Logger::debug2("MessageQueue::peek() Successfully peeked message at index %d with length %d\n", 
		item_index, message.data_length);

	return message.data_length;
}

bool MessageQueue::removeFront() {
	std::lock_guard<std::recursive_mutex> lock(queue_mutex_);

	Message message = {};
	if (!send_buffers_queue_->peek(message)) {
		return false;  // Queue is empty
	}

	if (!send_buffers_queue_->removeFront()) {
		Logger::recoverable_error("MessageQueue::removeFront() : failed to remove message from queue\n");
		return false;
	}

	releaseMessageBuffers(message);
	notify_cv_.notify_one();
	
	return true;
}