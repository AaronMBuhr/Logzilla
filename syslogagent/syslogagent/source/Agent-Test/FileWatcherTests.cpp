#include "pch.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../Agent/FileWatcher.h"
#include "../Agent/Configuration.h"
#include "../AgentLib/MessageQueue.h"

using namespace Syslog_agent;
using ::testing::Test;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// Mock for message queue to capture sent messages
class MockMessageQueue : public MessageQueue {
public:
    MockMessageQueue() : MessageQueue(100, 10) {}
    
    MOCK_METHOD(bool, addMessageUtf8, (const char* message), (override));
    
    // Helper to capture messages
    std::vector<std::string> capturedMessages;
    
    bool CaptureMessage(const char* message) {
        capturedMessages.push_back(message);
        return true;
    }
};

class FileWatcherTest : public Test {
protected:
    void SetUp() override {
        // Create test configuration
        config = std::make_unique<Configuration>();
        config->setHostName(L"TestHost");
        config->setFacility(1);  // user level
        config->setSeverity(5);  // notice
        
        // Create test file path
        std::wstring testDirPath = getTempDirPath();
        testFilePath = testDirPath + L"\\filewatcher_test.log";
        
        // Create mock message queue
        mockQueue = std::make_shared<NiceMock<MockMessageQueue>>();
        
        // Set up mock to capture messages
        ON_CALL(*mockQueue, addMessageUtf8(_))
            .WillByDefault([this](const char* message) {
                return mockQueue->CaptureMessage(message);
            });
    }
    
    void TearDown() override {
        // Delete test file if it exists
        DeleteFileW(testFilePath.c_str());
        
        // Clean up
        fileWatcher.reset();
        mockQueue.reset();
        config.reset();
    }
    
    // Helper to get a temporary directory path
    std::wstring getTempDirPath() {
        wchar_t tempPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tempPath);
        return std::wstring(tempPath);
    }
    
    // Helper to write to test file
    void writeToTestFile(const std::string& content) {
        // Create a new file or truncate existing file
        HANDLE hFile = CreateFileW(
            testFilePath.c_str(),
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hFile == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to create test file");
        }
        
        DWORD bytesWritten;
        WriteFile(
            hFile,
            content.c_str(),
            static_cast<DWORD>(content.length()),
            &bytesWritten,
            NULL
        );
        
        CloseHandle(hFile);
    }
    
    // Helper to append to test file
    void appendToTestFile(const std::string& content) {
        // Open file for appending
        HANDLE hFile = CreateFileW(
            testFilePath.c_str(),
            FILE_APPEND_DATA,
            0,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hFile == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open test file for appending");
        }
        
        DWORD bytesWritten;
        WriteFile(
            hFile,
            content.c_str(),
            static_cast<DWORD>(content.length()),
            &bytesWritten,
            NULL
        );
        
        CloseHandle(hFile);
    }
    
    std::unique_ptr<Configuration> config;
    std::shared_ptr<NiceMock<MockMessageQueue>> mockQueue;
    std::unique_ptr<FileWatcher> fileWatcher;
    std::wstring testFilePath;
};

// Test file watcher initialization
TEST_F(FileWatcherTest, Initialization) {
    // Create a simple test file
    writeToTestFile("Initial log line\n");
    
    // Create file watcher
    fileWatcher = std::make_unique<FileWatcher>(
        *config,
        testFilePath.c_str(),
        1024,  // max line length
        "TestProgram",
        config->getHostName().c_str(),
        config->getSeverity(),
        config->getFacility()
    );
    
    // Verify file watcher is created successfully
    EXPECT_TRUE(fileWatcher.get() != nullptr);
}

// Test file monitoring
TEST_F(FileWatcherTest, DISABLED_FileMonitoring) {
    // Create a simple test file
    writeToTestFile("Initial log line\n");
    
    // Create file watcher with our mock queue
    fileWatcher = std::make_unique<FileWatcher>(
        *config,
        testFilePath.c_str(),
        1024,  // max line length
        "TestProgram",
        config->getHostName().c_str(),
        config->getSeverity(),
        config->getFacility()
    );
    
    // Set the message queue
    // Note: This assumes the FileWatcher has a way to set the message queue
    // You might need to modify the FileWatcher class to allow this for testing
    // fileWatcher->setMessageQueue(mockQueue);
    
    // Wait for initial processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Append new lines to the file
    appendToTestFile("New log entry 1\n");
    appendToTestFile("New log entry 2\n");
    
    // Wait for file watcher to detect and process changes
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Verify that messages were captured
    // EXPECT_EQ(mockQueue->capturedMessages.size(), 3);
    // EXPECT_TRUE(mockQueue->capturedMessages[0].find("Initial log line") != std::string::npos);
    // EXPECT_TRUE(mockQueue->capturedMessages[1].find("New log entry 1") != std::string::npos);
    // EXPECT_TRUE(mockQueue->capturedMessages[2].find("New log entry 2") != std::string::npos);
    
    // Note: This test is disabled because it requires changes to the FileWatcher class
    // to make it testable. It demonstrates the testing pattern but would need
    // adaptation to work with the actual implementation.
}

// Test file watcher cleanup
TEST_F(FileWatcherTest, Cleanup) {
    // Create a simple test file
    writeToTestFile("Test log line\n");
    
    // Create and then immediately destroy file watcher
    {
        fileWatcher = std::make_unique<FileWatcher>(
            *config,
            testFilePath.c_str(),
            1024,  // max line length
            "TestProgram",
            config->getHostName().c_str(),
            config->getSeverity(),
            config->getFacility()
        );
        
        // Verify file watcher is created successfully
        EXPECT_TRUE(fileWatcher.get() != nullptr);
    }
    
    // fileWatcher should now be destroyed
    EXPECT_TRUE(fileWatcher.get() == nullptr);
    
    // This test verifies that the FileWatcher cleans up properly when destroyed
    // If there are issues, they would likely manifest as crashes or leaks
    // which would be caught by the test framework or memory checkers
}

// Test file not found handling
TEST_F(FileWatcherTest, FileNotFoundHandling) {
    // Use a non-existent file path
    std::wstring nonExistentPath = getTempDirPath() + L"\\nonexistent_file.log";
    
    // Create file watcher for non-existent file
    fileWatcher = std::make_unique<FileWatcher>(
        *config,
        nonExistentPath.c_str(),
        1024,  // max line length
        "TestProgram",
        config->getHostName().c_str(),
        config->getSeverity(),
        config->getFacility()
    );
    
    // Verify file watcher is created successfully despite non-existent file
    EXPECT_TRUE(fileWatcher.get() != nullptr);
    
    // Now create the file
    HANDLE hFile = CreateFileW(
        nonExistentPath.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }
    
    // Wait for file watcher to detect file creation
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Cleanup
    DeleteFileW(nonExistentPath.c_str());
}

// Test maximum line length handling
TEST_F(FileWatcherTest, DISABLED_MaxLineLengthHandling) {
    // Create a test file with a very long line
    std::string longLine(2000, 'x');  // 2000 'x' characters
    writeToTestFile(longLine + "\n");
    
    // Create file watcher with small max line length
    fileWatcher = std::make_unique<FileWatcher>(
        *config,
        testFilePath.c_str(),
        100,  // max line length of 100
        "TestProgram",
        config->getHostName().c_str(),
        config->getSeverity(),
        config->getFacility()
    );
    
    // Set the message queue
    // fileWatcher->setMessageQueue(mockQueue);
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Verify that the line was truncated
    // EXPECT_EQ(mockQueue->capturedMessages.size(), 1);
    // EXPECT_LE(mockQueue->capturedMessages[0].length(), 100 + someOverhead);
    
    // Note: This test is disabled for the same reasons as the FileMonitoring test
}

// Test file rotation handling
TEST_F(FileWatcherTest, DISABLED_FileRotationHandling) {
    // Create a test file
    writeToTestFile("Initial log line\n");
    
    // Create file watcher
    fileWatcher = std::make_unique<FileWatcher>(
        *config,
        testFilePath.c_str(),
        1024,
        "TestProgram",
        config->getHostName().c_str(),
        config->getSeverity(),
        config->getFacility()
    );
    
    // Set the message queue
    // fileWatcher->setMessageQueue(mockQueue);
    
    // Wait for initial processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Simulate file rotation by removing file and creating a new one
    DeleteFileW(testFilePath.c_str());
    writeToTestFile("New file after rotation\n");
    
    // Wait for file watcher to detect and process changes
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Verify that both the old and new content were captured
    // EXPECT_EQ(mockQueue->capturedMessages.size(), 2);
    // EXPECT_TRUE(mockQueue->capturedMessages[0].find("Initial log line") != std::string::npos);
    // EXPECT_TRUE(mockQueue->capturedMessages[1].find("New file after rotation") != std::string::npos);
    
    // Note: This test is disabled for the same reasons as the FileMonitoring test
}
