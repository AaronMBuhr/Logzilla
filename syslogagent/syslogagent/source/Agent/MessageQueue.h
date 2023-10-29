#pragma once
#include <memory>
#include <mutex>
#include "ArrayQueue.h"
#include "BitmappedUsageCollection.h"

class MessageQueue
{
public:
	static const int MAX_BUFFERS_PER_MESSAGE = 32;
	static const int MESSAGE_BUFFER_SIZE = 2048;
	static const int MESSAGE_QUEUE_SLACK_PERCENT = 80;

	MessageQueue(
		int message_queue_size,
		int message_buffers_chunk_size
	);
	bool isEmpty() { return send_buffers_queue_->isEmpty(); }
	bool isFull() { return send_buffers_queue_->isFull(); }
	bool enqueue(const char* message_content, const int message_len);
	int dequeue(char* message_content, const int max_len);
	int peek(char* message_content, const int max_len);
	bool removeFront();
	int length() { return (int) send_buffers_queue_->length(); }
	void lock() { /* Logger::debug2("MessageQueue::lock() called with in_use_counter_==%d\n", in_use_counter_++); */ in_use_.lock(); }
	void unlock() { /* Logger::debug2("MessageQueue::unlock() called with in_use_counter_==%d\n", in_use_counter_--); */ in_use_.unlock(); }

private:
	typedef struct MessageBufferStruct {
		char buffer[MESSAGE_BUFFER_SIZE];
	} MessageBuffer;
	typedef struct MessageStruct {
		MessageBufferStruct* message_buffers[MAX_BUFFERS_PER_MESSAGE];
		int data_length;
	} Message;
	unique_ptr<ArrayQueue<Message>> send_buffers_queue_;
	unique_ptr<BitmappedUsageCollection<MessageBuffer>> send_buffers_;

	int message_queue_size_;
	int message_queue_chunk_size_;
	volatile int in_use_counter_;
	recursive_mutex in_use_;
	recursive_mutex modifying_;

};

