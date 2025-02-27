#include "pch.h"
#include "../AgentLib/MessageQueue.h"
#include "../AgentLib/MessageBatcher.h"
#include "../AgentLib/HTTPMessageBatcher.h"
#include "../AgentLib/JSONMessageBatcher.h"

// NOTE: These tests have been moved to AgentLib-Test
// This file remains as a reference but the tests should be run from AgentLib-Test

using namespace Syslog_agent;
using ::testing::Test;

class MessageQueueTest : public Test {
protected:
    // Set up common test parameters
    static constexpr size_t QUEUE_SIZE = 100;
    static constexpr size_t BUFFER_CHUNK_SIZE = 10;
    
    void SetUp() override {
        // Create a fresh queue for each test
        queue = std::make_shared<MessageQueue>(QUEUE_SIZE, BUFFER_CHUNK_SIZE);
    }
    
    void TearDown() override {
        // Clean up
        queue.reset();
    }
    
    std::shared_ptr<MessageQueue> queue;
};

// Test basic queue operations
TEST_F(MessageQueueTest, BasicOperations) {
    // Verify initial state
    EXPECT_TRUE(queue->isEmpty());
    EXPECT_EQ(queue->length(), 0);
    
    // Add a message
    const char* testMessage = "Test message";
    EXPECT_TRUE(queue->addMessageUtf8(testMessage));
    
    // Verify queue state after adding
    EXPECT_FALSE(queue->isEmpty());
    EXPECT_EQ(queue->length(), 1);
    
    // Get the message
    std::string message = queue->front();
    EXPECT_EQ(message, testMessage);
    
    // Remove the message
    queue->removeFront();
    
    // Verify queue state after removing
    EXPECT_TRUE(queue->isEmpty());
    EXPECT_EQ(queue->length(), 0);
}

// Test queue capacity limits
TEST_F(MessageQueueTest, CapacityLimits) {
    // Fill the queue to capacity
    for (size_t i = 0; i < QUEUE_SIZE; i++) {
        std::string message = "Message " + std::to_string(i);
        EXPECT_TRUE(queue->addMessageUtf8(message.c_str()));
    }
    
    // Verify queue is full
    EXPECT_EQ(queue->length(), QUEUE_SIZE);
    
    // Try to add one more message (should fail)
    EXPECT_FALSE(queue->addMessageUtf8("One too many"));
    
    // Verify queue state hasn't changed
    EXPECT_EQ(queue->length(), QUEUE_SIZE);
    
    // Remove a message
    queue->removeFront();
    
    // Verify we can add another message now
    EXPECT_TRUE(queue->addMessageUtf8("New message"));
    EXPECT_EQ(queue->length(), QUEUE_SIZE);
}

// Test concurrent access
TEST_F(MessageQueueTest, ConcurrentAccess) {
    // Create multiple threads to add and remove messages
    std::vector<std::thread> threads;
    std::atomic<int> messagesAdded(0);
    std::atomic<int> messagesRemoved(0);
    
    // Function to add messages
    auto addFunc = [this, &messagesAdded]() {
        for (int i = 0; i < 50; i++) {
            std::string message = "Thread message " + std::to_string(i);
            if (queue->addMessageUtf8(message.c_str())) {
                messagesAdded++;
            }
            // Small sleep to increase chance of thread interleaving
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    
    // Function to remove messages
    auto removeFunc = [this, &messagesRemoved]() {
        for (int i = 0; i < 40; i++) {
            if (!queue->isEmpty()) {
                queue->front();  // Just read, don't verify content
                queue->removeFront();
                messagesRemoved++;
            }
            // Small sleep to increase chance of thread interleaving
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    
    // Start the threads
    threads.push_back(std::thread(addFunc));
    threads.push_back(std::thread(addFunc));
    threads.push_back(std::thread(removeFunc));
    threads.push_back(std::thread(removeFunc));
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify final state
    // The queue length should be the difference between adds and removes
    EXPECT_EQ(queue->length(), messagesAdded - messagesRemoved);
}

// Test shutdown behavior
TEST_F(MessageQueueTest, ShutdownBehavior) {
    // Add some messages
    for (int i = 0; i < 10; i++) {
        std::string message = "Shutdown test " + std::to_string(i);
        queue->addMessageUtf8(message.c_str());
    }
    
    // Begin shutdown
    queue->beginShutdown();
    
    // Verify we can still remove messages but not add new ones
    EXPECT_FALSE(queue->isEmpty());
    EXPECT_FALSE(queue->addMessageUtf8("New message during shutdown"));
    
    // We should be able to remove all existing messages
    while (!queue->isEmpty()) {
        queue->removeFront();
    }
    
    // Verify queue is now empty
    EXPECT_TRUE(queue->isEmpty());
}

// Message Batcher Tests
class MessageBatcherTest : public Test {
protected:
    static constexpr size_t MAX_BATCH_COUNT = 5;
    static constexpr size_t MAX_BATCH_AGE_SEC = 1;
    
    void SetUp() override {
        // Create fresh batchers for each test
        httpBatcher = std::make_shared<HTTPMessageBatcher>(MAX_BATCH_COUNT, MAX_BATCH_AGE_SEC);
        jsonBatcher = std::make_shared<JSONMessageBatcher>(MAX_BATCH_COUNT, MAX_BATCH_AGE_SEC);
    }
    
    void TearDown() override {
        // Clean up
        httpBatcher.reset();
        jsonBatcher.reset();
    }
    
    std::shared_ptr<HTTPMessageBatcher> httpBatcher;
    std::shared_ptr<JSONMessageBatcher> jsonBatcher;
};

// Test HTTP batcher basic operation
TEST_F(MessageBatcherTest, HttpBatcherBasicOperation) {
    // Add messages to the batcher
    for (int i = 0; i < 3; i++) {
        std::string message = "HTTP message " + std::to_string(i);
        httpBatcher->addMessage(message);
    }
    
    // Check batch status - shouldn't be ready yet (not enough messages)
    EXPECT_FALSE(httpBatcher->isBatchReady());
    
    // Add more messages to reach the limit
    for (int i = 3; i < MAX_BATCH_COUNT; i++) {
        std::string message = "HTTP message " + std::to_string(i);
        httpBatcher->addMessage(message);
    }
    
    // Check batch status - should be ready now
    EXPECT_TRUE(httpBatcher->isBatchReady());
    
    // Get the batch
    std::string batch = httpBatcher->getBatch();
    
    // Verify the batch contains all messages
    for (int i = 0; i < MAX_BATCH_COUNT; i++) {
        std::string message = "HTTP message " + std::to_string(i);
        EXPECT_TRUE(batch.find(message) != std::string::npos);
    }
    
    // Verify batch is properly formatted for HTTP
    EXPECT_TRUE(batch.find("[") != std::string::npos);  // JSON array start
    EXPECT_TRUE(batch.find("]") != std::string::npos);  // JSON array end
    
    // Verify the batcher is reset after getting the batch
    EXPECT_FALSE(httpBatcher->isBatchReady());
}

// Test JSON batcher basic operation
TEST_F(MessageBatcherTest, JsonBatcherBasicOperation) {
    // Add messages to the batcher
    for (int i = 0; i < 3; i++) {
        std::string message = "JSON message " + std::to_string(i);
        jsonBatcher->addMessage(message);
    }
    
    // Check batch status - shouldn't be ready yet (not enough messages)
    EXPECT_FALSE(jsonBatcher->isBatchReady());
    
    // Add more messages to reach the limit
    for (int i = 3; i < MAX_BATCH_COUNT; i++) {
        std::string message = "JSON message " + std::to_string(i);
        jsonBatcher->addMessage(message);
    }
    
    // Check batch status - should be ready now
    EXPECT_TRUE(jsonBatcher->isBatchReady());
    
    // Get the batch
    std::string batch = jsonBatcher->getBatch();
    
    // Verify the batch contains all messages
    for (int i = 0; i < MAX_BATCH_COUNT; i++) {
        std::string message = "JSON message " + std::to_string(i);
        EXPECT_TRUE(batch.find(message) != std::string::npos);
    }
    
    // Verify the batcher is reset after getting the batch
    EXPECT_FALSE(jsonBatcher->isBatchReady());
}

// Test batch age timeout
TEST_F(MessageBatcherTest, BatchAgeTimeout) {
    // Add a single message (not enough to trigger batch by count)
    httpBatcher->addMessage("Timeout test message");
    
    // Batch shouldn't be ready yet
    EXPECT_FALSE(httpBatcher->isBatchReady());
    
    // Wait for the age timeout
    std::this_thread::sleep_for(std::chrono::seconds(MAX_BATCH_AGE_SEC + 1));
    
    // Batch should be ready now due to age
    EXPECT_TRUE(httpBatcher->isBatchReady());
    
    // Get the batch
    std::string batch = httpBatcher->getBatch();
    
    // Verify the batch contains the message
    EXPECT_TRUE(batch.find("Timeout test message") != std::string::npos);
    
    // Verify the batcher is reset after getting the batch
    EXPECT_FALSE(httpBatcher->isBatchReady());
}

// Test empty batch handling
TEST_F(MessageBatcherTest, EmptyBatchHandling) {
    // No messages added, batch shouldn't be ready
    EXPECT_FALSE(httpBatcher->isBatchReady());
    
    // Attempting to get an empty batch
    std::string batch = httpBatcher->getBatch();
    
    // Should return an empty or properly formatted empty batch
    EXPECT_FALSE(batch.empty());  // Should return valid JSON even if empty
    EXPECT_TRUE(batch.find("[]") != std::string::npos);  // Empty JSON array
}
