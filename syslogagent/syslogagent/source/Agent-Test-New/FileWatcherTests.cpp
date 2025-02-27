#include "pch.h"
#include <gtest/gtest.h>
#include "hippomocks.h"
#include "../Agent/FileWatcher.h"
#include "../Agent/Configuration.h"
#include "../AgentLib/MessageQueue.h"

using namespace Syslog_agent;

// Interface for message queue
class IMessageQueue {
public:
    virtual ~IMessageQueue() = default;
    virtual bool addMessageUtf8(const char* message) = 0;
    virtual bool empty() const = 0;
};

class FileWatcherTest : public ::testing::Test {
protected:
    MockRepository mocks;
    IMessageQueue* mockQueue;
    std::unique_ptr<Configuration> config;
    std::wstring testFilePath;
    std::vector<std::string> capturedMessages;
    
    void SetUp() override {
        // Create test configuration
        config = std::make_unique<Configuration>();
        
        // Create test file path
        std::wstring testDirPath = getTempDirPath();
        testFilePath = testDirPath + L"\\filewatcher_test.log";
        
        // Create mock message queue
        mockQueue = mocks.InterfaceMock<IMessageQueue>();
        
        // Set up mock to capture messages
        mocks.OnCall(mockQueue, &IMessageQueue::addMessageUtf8).Do([this](const char* message) {
            capturedMessages.push_back(message);
            return true;
        });
    }
    
    void TearDown() override {
        // Clean up any test files
        DeleteFileW(testFilePath.c_str());
        
        // Verify expectations
        mocks.VerifyAll();
    }
    
    // Helper to get temp directory path
    std::wstring getTempDirPath() {
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        return std::wstring(tempPath);
    }
    
    // Helper to write to test file
    void writeToTestFile(const std::string& content) {
        FILE* file = _wfopen(testFilePath.c_str(), L"a");
        if (file) {
            fwrite(content.c_str(), sizeof(char), content.length(), file);
            fclose(file);
        }
    }
    
    // Helper to create a test file with initial content
    void createTestFile(const std::string& initialContent = "") {
        FILE* file = _wfopen(testFilePath.c_str(), L"w");
        if (file) {
            if (!initialContent.empty()) {
                fwrite(initialContent.c_str(), sizeof(char), initialContent.length(), file);
            }
            fclose(file);
        }
    }
    
    // Helper to check if a file exists
    bool fileExists(const std::wstring& path) {
        DWORD attributes = GetFileAttributesW(path.c_str());
        return (attributes != INVALID_FILE_ATTRIBUTES && 
                !(attributes & FILE_ATTRIBUTE_DIRECTORY));
    }
    
    // Helper to wait for file watcher to process
    void waitForProcessing(int milliseconds = 500) {
        Sleep(milliseconds);
    }
    
    // Helper to verify message contains expected text
    void verifyMessageContains(const std::string& expectedText) {
        bool found = false;
        for (const auto& message : capturedMessages) {
            if (message.find(expectedText) != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected text not found in any message: " << expectedText;
    }
};

// Test file watcher initialization
TEST_F(FileWatcherTest, Initialization) {
    // Create a test file
    createTestFile("Initial line\n");
    
    // Create and initialize file watcher
    FileWatcher watcher(config.get());
    
    // Configure the watcher
    watcher.configure(testFilePath, L"TestProgram");
    
    // Set the message queue
    watcher.setMessageQueue(std::shared_ptr<IMessageQueue>(mockQueue));
    
    // Verify that it initialized correctly
    EXPECT_TRUE(watcher.isConfigured());
    EXPECT_EQ(watcher.getFilePath(), testFilePath);
    EXPECT_EQ(watcher.getProgramName(), L"TestProgram");
}

// Test file monitoring
TEST_F(FileWatcherTest, DISABLED_FileMonitoring) {
    // Create a test file with initial content
    createTestFile("Initial line\n");
    
    // Create and initialize file watcher
    FileWatcher watcher(config.get());
    watcher.configure(testFilePath, L"TestProgram");
    
    // Set the message queue and expect a call to add a message
    watcher.setMessageQueue(std::shared_ptr<IMessageQueue>(mockQueue));
    
    // Start monitoring
    watcher.start();
    
    // Wait a bit for initial processing
    waitForProcessing();
    
    // Add more content to the file
    writeToTestFile("New line 1\nNew line 2\n");
    
    // Wait for processing
    waitForProcessing();
    
    // Stop monitoring
    watcher.stop();
    
    // Verify that messages were sent for the new lines
    EXPECT_FALSE(capturedMessages.empty());
    verifyMessageContains("New line 1");
    verifyMessageContains("New line 2");
}

// Test file watcher cleanup
TEST_F(FileWatcherTest, Cleanup) {
    // Create a test file
    createTestFile("Initial line\n");
    
    // Create scope for file watcher to test cleanup
    {
        // Create and initialize file watcher
        FileWatcher watcher(config.get());
        watcher.configure(testFilePath, L"TestProgram");
        
        // Set the message queue
        watcher.setMessageQueue(std::shared_ptr<IMessageQueue>(mockQueue));
        
        // Start monitoring
        watcher.start();
        
        // Wait a bit
        waitForProcessing();
    }
    
    // At this point, the file watcher should have been destroyed
    // We can verify that it properly released resources
    
    // For now, just check that the test file still exists
    EXPECT_TRUE(fileExists(testFilePath));
}

// Test file not found handling
TEST_F(FileWatcherTest, FileNotFoundHandling) {
    // Create a file watcher with a non-existent file
    std::wstring nonExistentPath = getTempDirPath() + L"\\nonexistent_file.log";
    
    // Make sure the file doesn't exist
    DeleteFileW(nonExistentPath.c_str());
    EXPECT_FALSE(fileExists(nonExistentPath));
    
    // Create and initialize file watcher
    FileWatcher watcher(config.get());
    watcher.configure(nonExistentPath, L"TestProgram");
    
    // Set the message queue
    watcher.setMessageQueue(std::shared_ptr<IMessageQueue>(mockQueue));
    
    // Start monitoring (should handle the missing file gracefully)
    watcher.start();
    
    // Wait a bit
    waitForProcessing();
    
    // Create the file later
    createTestFile("Late file creation\n");
    
    // Rename to target path
    MoveFileW(testFilePath.c_str(), nonExistentPath.c_str());
    
    // Wait for processing
    waitForProcessing(1000);
    
    // Stop monitoring
    watcher.stop();
    
    // Cleanup
    DeleteFileW(nonExistentPath.c_str());
}

// Test maximum line length handling
TEST_F(FileWatcherTest, DISABLED_MaxLineLengthHandling) {
    // Create a test file
    createTestFile("");
    
    // Create and initialize file watcher
    FileWatcher watcher(config.get());
    watcher.configure(testFilePath, L"TestProgram");
    
    // Set the message queue
    watcher.setMessageQueue(std::shared_ptr<IMessageQueue>(mockQueue));
    
    // Start monitoring
    watcher.start();
    
    // Wait a bit for initial processing
    waitForProcessing();
    
    // Create a very long line exceeding typical limits
    std::string longLine(16385, 'X'); // More than 16K
    writeToTestFile(longLine + "\n");
    
    // Wait for processing
    waitForProcessing(1000);
    
    // Verify that the line was truncated or properly handled
    EXPECT_FALSE(capturedMessages.empty());
    
    // Stop monitoring
    watcher.stop();
}

// Test file rotation handling
TEST_F(FileWatcherTest, DISABLED_FileRotationHandling) {
    // Create a test file with initial content
    createTestFile("Initial line\n");
    
    // Create and initialize file watcher
    FileWatcher watcher(config.get());
    watcher.configure(testFilePath, L"TestProgram");
    
    // Set the message queue
    watcher.setMessageQueue(std::shared_ptr<IMessageQueue>(mockQueue));
    
    // Start monitoring
    watcher.start();
    
    // Wait a bit for initial processing
    waitForProcessing();
    
    // Simulate file rotation: rename current file and create a new one
    std::wstring rotatedFilePath = testFilePath + L".1";
    MoveFileW(testFilePath.c_str(), rotatedFilePath.c_str());
    
    // Create a new file with different content
    createTestFile("New file after rotation\n");
    
    // Wait for processing
    waitForProcessing(1000);
    
    // Add more content to confirm monitoring continues
    writeToTestFile("Additional line after rotation\n");
    
    // Wait for processing
    waitForProcessing();
    
    // Stop monitoring
    watcher.stop();
    
    // Verify that messages were captured for the new content
    EXPECT_FALSE(capturedMessages.empty());
    verifyMessageContains("New file after rotation");
    verifyMessageContains("Additional line after rotation");
    
    // Cleanup
    DeleteFileW(rotatedFilePath.c_str());
}
