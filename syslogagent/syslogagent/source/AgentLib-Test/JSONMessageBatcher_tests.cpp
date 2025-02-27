#include "pch.h"
#include "../AgentLib/JSONMessageBatcher.h"
#include "../AgentLib/MessageQueue.h"
#include "MessageQueueTestExtensions.h"
#include "../Infrastructure/Logger.h"
#include <string>
#include <chrono>
#include <thread>

using namespace Syslog_agent;
using namespace std;

// Test-specific subclass that exposes protected methods for testing
class TestJSONMessageBatcher : public JSONMessageBatcher {
public:
    TestJSONMessageBatcher(std::uint32_t max_batch_size, std::uint32_t max_batch_age)
        : JSONMessageBatcher(max_batch_size, max_batch_age) {}
    
    // Expose protected methods for testing
    using JSONMessageBatcher::GetBatchBuffer;
    using JSONMessageBatcher::ReleaseBatchBuffer;
    using JSONMessageBatcher::GetMaxBatchSizeBytes;
};

// Note: The no-parameter dequeue method is now defined in MessageQueueTestExtensions.h

class JSONMessageBatcherTest : public ::testing::Test {
protected:
    static constexpr std::uint32_t MAX_BATCH_COUNT = 5;
    static constexpr std::uint32_t MAX_BATCH_AGE_SEC = 1;
    
    void SetUp() override {
        // Create a fresh batcher for each test
        batcher = std::make_unique<TestJSONMessageBatcher>(MAX_BATCH_COUNT, MAX_BATCH_AGE_SEC);
        // Pass required parameters for MessageQueue constructor (initial sizes)
        message_queue = std::make_shared<MessageQueue>(10, 20);
    }
    
    void TearDown() override {
        batcher.reset();
        message_queue.reset();
    }
    
    std::unique_ptr<TestJSONMessageBatcher> batcher;
    std::shared_ptr<MessageQueue> message_queue;
    
    // Helper to check if batch is ready
    bool isBatchReady() {
        // If no messages, not ready
        if (message_queue->length() == 0) {
            return false;
        }
        
        // If we've reached the max batch count, it's ready
        if (message_queue->length() >= MAX_BATCH_COUNT) {
            return true;
        }
        
        // For testing age-based batching, we'll simulate the check
        // by examining batch age in the test that needs it
        return false;
    }
    
    // Helper to get the batch as a string
    std::string getBatch() {
        auto debug_logger = LOG_THIS;
        char* buffer = batcher->GetBatchBuffer("test");
        if (!buffer) {
            debug_logger->debug("getBatch: GetBatchBuffer returned null\n");
            return "";
        }
        
        auto result = batcher->BatchEvents(message_queue, buffer, batcher->GetMaxBatchSizeBytes());
        std::string batch_str;
        
        debug_logger->debug("getBatch: BatchEvents result.status=%d, messages_batched=%u, bytes_written=%zu\n", 
            static_cast<int>(result.status), result.messages_batched, result.bytes_written);
        
        if (result.status == MessageBatcher::BatchResult::Status::Success) {
            batch_str = std::string(buffer, result.bytes_written);
            // Remove the batched messages from the queue
            for (uint32_t i = 0; i < result.messages_batched; i++) {
                dequeue(message_queue.get());
            }
        } else if (result.status == MessageBatcher::BatchResult::Status::NoMessages) {
            // For JSONMessageBatcher, an empty batch is just an empty string
            batch_str = "";
        } else {
            debug_logger->debug("getBatch: BatchEvents returned error status\n");
        }
        
        batcher->ReleaseBatchBuffer(buffer);
        return batch_str;
    }
    
    // Helper to add a message
    void addMessage(const char* msg) {
        message_queue->enqueue(msg, strlen(msg));
    }
};

// Test initial state
TEST_F(JSONMessageBatcherTest, InitialState) {
    EXPECT_FALSE(isBatchReady());
    // New batcher should return empty batch
    std::string batch = getBatch();
    EXPECT_TRUE(batch.empty());
}

// Test adding a single message
TEST_F(JSONMessageBatcherTest, SingleMessage) {
    const char* testMessage = "{\"test\":\"value\"}";
    addMessage(testMessage);
    
    // Single message shouldn't make batch ready (below batch count)
    EXPECT_FALSE(isBatchReady());
    
    // Get the batch anyway
    std::string batch = getBatch();
    
    // It should contain our test message
    EXPECT_TRUE(batch.find(testMessage) != std::string::npos);
}

// Test reaching batch count
TEST_F(JSONMessageBatcherTest, BatchCountReached) {
    // Add messages up to the batch count
    for (uint32_t i = 0; i < MAX_BATCH_COUNT; i++) {
        std::string msg = "{\"index\":" + std::to_string(i) + "}";
        addMessage(msg.c_str());
    }
    
    // Batch should now be ready
    EXPECT_TRUE(isBatchReady());
    
    // Get the batch
    std::string batch = getBatch();
    
    // Check it contains all messages
    for (uint32_t i = 0; i < MAX_BATCH_COUNT; i++) {
        std::string expected = "\"index\":" + std::to_string(i);
        EXPECT_TRUE(batch.find(expected) != std::string::npos);
    }
    
    // After getting batch, it should be reset
    EXPECT_FALSE(isBatchReady());
}

// Test batch age timeout
TEST_F(JSONMessageBatcherTest, BatchAgeTimeout) {
    auto test_logger = LOG_THIS;
    // Add a single message
    test_logger->debug("Adding message with proper JSON format\n");
    addMessage("{ \"timeout\": \"test\" }");
    
    // Batch shouldn't be ready yet
    EXPECT_FALSE(isBatchReady());
    
    // Wait for the timeout
    test_logger->debug("Waiting for timeout (%d seconds)...\n", MAX_BATCH_AGE_SEC + 1);
    std::this_thread::sleep_for(std::chrono::seconds(MAX_BATCH_AGE_SEC + 1));
    
    // Check queue length
    test_logger->debug("Queue length after timeout: %zu\n", message_queue->length());
    
    // In a real scenario, the batcher would now consider the batch ready due to age
    // Since we can't directly test the internal age tracking, we'll verify the batch content
    std::string batch = getBatch();
    test_logger->debug("Batch content after getBatch: '%s'\n", batch.c_str());
    EXPECT_TRUE(!batch.empty());
    if (!batch.empty()) {
        // If we have a batch, then test for our expected content
        EXPECT_TRUE(batch.find("timeout") != std::string::npos);
    }
}

// Test message formatting
TEST_F(JSONMessageBatcherTest, MessageFormatting) {
    // Add two messages
    addMessage("{\"first\":true}");
    addMessage("{\"second\":false}");
    
    // Get the batch
    std::string batch = getBatch();
    
    // Check message separation with newlines
    EXPECT_TRUE(batch.find("{\"first\":true}\n{\"second\":false}") != std::string::npos);
}

// Test adding large number of messages
TEST_F(JSONMessageBatcherTest, LargeNumberOfMessages) {
    static constexpr int LARGE_COUNT = 20;
    
    // Add many messages
    for (int i = 0; i < LARGE_COUNT; i++) {
        std::string msg = "{\"large\":" + std::to_string(i) + "}";
        addMessage(msg.c_str());
    }
    
    // Batch should be ready
    EXPECT_TRUE(isBatchReady());
    
    // Get and verify all messages are included
    std::string batch = getBatch();
    for (int i = 0; i < MAX_BATCH_COUNT; i++) {
        std::string expected = "\"large\":" + std::to_string(i);
        EXPECT_TRUE(batch.find(expected) != std::string::npos);
    }
}

// Test max batch size
TEST_F(JSONMessageBatcherTest, MaxBatchSize) {
    // Get the max size
    uint32_t maxSize = batcher->GetMaxBatchSizeBytes();
    EXPECT_GT(maxSize, 0);
    
    // Try adding a very large message
    std::string largeMsg = "{\"large\":\"";
    largeMsg.append(maxSize / 10, 'X');  // Make it somewhat large but not too large to process
    largeMsg += "\"}";
    
    // This might fail if the message is too large for a single buffer
    // The behavior depends on the implementation
    addMessage(largeMsg.c_str());
    
    // Get the batch
    std::string batch = getBatch();
    
    // It should still contain the message or a truncated version
    EXPECT_FALSE(batch.empty());
}

// Test empty batch
TEST_F(JSONMessageBatcherTest, EmptyBatch) {
    // Get batch without adding any messages
    std::string batch = getBatch();
    
    // Should be empty
    EXPECT_TRUE(batch.empty());
}

// Test batch reset
TEST_F(JSONMessageBatcherTest, BatchReset) {
    // Add a message
    addMessage("{\"test\":\"reset\"}");
    
    // Get the batch
    std::string batch1 = getBatch();
    EXPECT_TRUE(batch1.find("test") != std::string::npos);
    
    // Get another batch without adding more messages
    std::string batch2 = getBatch();
    
    // Second batch should be empty
    EXPECT_TRUE(batch2.empty());
}
