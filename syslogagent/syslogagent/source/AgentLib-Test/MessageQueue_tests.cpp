#include "pch.h"
#include "MessageQueue.h"

using namespace Syslog_agent;

class MessageQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create queue with initial sizes
        queue = std::make_unique<MessageQueue>(10, 20);  // 10 messages, 20 buffers initially
    }

    void TearDown() override {
        queue.reset();
    }

    std::unique_ptr<MessageQueue> queue;
};

// Test empty queue behavior
TEST_F(MessageQueueTest, NewQueueIsEmpty) {
    EXPECT_TRUE(queue->isEmpty());
    EXPECT_EQ(queue->length(), 0);
    EXPECT_EQ(queue->getOldestMessageTimestamp(), 0);
}

// Test basic enqueue/peek/dequeue operations
TEST_F(MessageQueueTest, BasicEnqueueAndDequeue) {
    const char* testMessage = "Test Message";
    const size_t msgLen = strlen(testMessage);

    // Test enqueue
    EXPECT_TRUE(queue->enqueue(testMessage, msgLen));
    EXPECT_FALSE(queue->isEmpty());
    EXPECT_EQ(queue->length(), 1);

    // Test peek
    char peekBuffer[100] = { 0 };
    int peekedLen = queue->peek(nullptr, peekBuffer, sizeof(peekBuffer));
    EXPECT_EQ(peekedLen, msgLen);
    EXPECT_STREQ(peekBuffer, testMessage);

    // Test dequeue
    char dequeueBuffer[100] = { 0 };
    int dequeueLen = queue->dequeue(dequeueBuffer, sizeof(dequeueBuffer));
    EXPECT_EQ(dequeueLen, msgLen);
    EXPECT_STREQ(dequeueBuffer, testMessage);

    // Queue should be empty after dequeue
    EXPECT_TRUE(queue->isEmpty());
    EXPECT_EQ(queue->length(), 0);
}

// Test enqueue with invalid parameters
TEST_F(MessageQueueTest, EnqueueInvalidParams) {
    // Null content
    EXPECT_FALSE(queue->enqueue(nullptr, 10));

    // Zero length
    EXPECT_FALSE(queue->enqueue("test", 0));

    // Too long message
    const size_t tooLong = MessageQueue::MESSAGE_BUFFER_SIZE * MessageQueue::MAX_BUFFERS_PER_MESSAGE;
    std::string longMsg(tooLong, 'A');
    EXPECT_FALSE(queue->enqueue(longMsg.c_str(), longMsg.length()));
}

// Test multiple messages
TEST_F(MessageQueueTest, MultipleMessages) {
    const char* messages[] = { "First", "Second", "Third" };

    // Enqueue all messages
    for (const char* msg : messages) {
        EXPECT_TRUE(queue->enqueue(msg, strlen(msg)));
    }

    EXPECT_EQ(queue->length(), 3);

    // Dequeue and verify order
    char buffer[100];
    for (const char* expected : messages) {
        int len = queue->dequeue(buffer, sizeof(buffer));
        EXPECT_EQ(len, strlen(expected));
        EXPECT_STREQ(buffer, expected);
    }
}

// Test timestamp behavior
TEST_F(MessageQueueTest, MessageTimestamp) {
    const char* msg = "Test";
    EXPECT_TRUE(queue->enqueue(msg, strlen(msg)));

    int64_t timestamp = queue->getOldestMessageTimestamp();
    EXPECT_GT(timestamp, 0);  // Should have a valid timestamp
}

// Test waiting for messages
TEST_F(MessageQueueTest, WaitForMessages) {
    // Should timeout when empty
    EXPECT_FALSE(queue->waitForMessages(100));  // 100ms timeout

    // Add a message
    const char* msg = "Test";
    EXPECT_TRUE(queue->enqueue(msg, strlen(msg)));

    // Should return immediately when message is available
    EXPECT_TRUE(queue->waitForMessages(100));
}

// Test shutdown behavior
TEST_F(MessageQueueTest, ShutdownBehavior) {
    const char* msg = "Test";
    EXPECT_TRUE(queue->enqueue(msg, strlen(msg)));

    queue->beginShutdown();
    EXPECT_TRUE(queue->isShuttingDown());
    EXPECT_TRUE(queue->isEmpty());  // Should report as empty during shutdown
    EXPECT_EQ(queue->length(), 0);  // Should report length 0 during shutdown
}
