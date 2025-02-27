#include "pch.h"
#include <gtest/gtest.h>
#include "hippomocks.h"
#include "../Agent/EventLogEvent.h"
#include "../Agent/EventLogSubscription.h"
#include "../AgentLib/IEventHandler.h"
#include "../Infrastructure/Result.h"

using namespace Syslog_agent;

// Base class for event handler
class ChannelEventHandlerBase {
public:
    ChannelEventHandlerBase(const std::wstring& channel) : channel_(channel) {}
    virtual ~ChannelEventHandlerBase() = default;
    virtual void handleEvent(const EventLogEvent& event) = 0;
    
    std::wstring getChannel() const { return channel_; }
    
protected:
    std::wstring channel_;
};

class EventLogTest : public ::testing::Test {
protected:
    MockRepository mocks;
    ChannelEventHandlerBase* mockEventHandler;
    
    void SetUp() override {
        // Set up test environment
        mockEventHandler = mocks.InterfaceMock<ChannelEventHandlerBase>(L"TestChannel");
    }

    void TearDown() override {
        // Clean up test environment
        mocks.VerifyAll();
    }
};

TEST_F(EventLogTest, EventLogEventBasicProperties) {
    // Create a basic event log event
    std::wstring channel = L"Application";
    DWORD eventID = 1000;
    WORD eventType = 4; // Information
    std::wstring source = L"TestSource";
    std::wstring computer = L"TestComputer";
    SYSTEMTIME timestamp;
    GetSystemTime(&timestamp);
    std::wstring message = L"Test event message";
    
    EventLogEvent event(channel, eventID, eventType, source, computer, timestamp, message);
    
    // Verify properties
    EXPECT_EQ(event.getChannel(), channel);
    EXPECT_EQ(event.getEventID(), eventID);
    EXPECT_EQ(event.getEventType(), eventType);
    EXPECT_EQ(event.getSource(), source);
    EXPECT_EQ(event.getComputer(), computer);
    EXPECT_EQ(event.getMessage(), message);
    
    // Test timestamp comparison
    SYSTEMTIME retrievedTime = event.getTimestamp();
    EXPECT_EQ(retrievedTime.wYear, timestamp.wYear);
    EXPECT_EQ(retrievedTime.wMonth, timestamp.wMonth);
    EXPECT_EQ(retrievedTime.wDay, timestamp.wDay);
    EXPECT_EQ(retrievedTime.wHour, timestamp.wHour);
    EXPECT_EQ(retrievedTime.wMinute, timestamp.wMinute);
    EXPECT_EQ(retrievedTime.wSecond, timestamp.wSecond);
}

TEST_F(EventLogTest, EventHandling) {
    // Create a test event
    EventLogEvent testEvent(L"TestChannel", 1001, 4, L"TestSource", L"TestComputer", SYSTEMTIME(), L"Test event for handling");
    
    // Set up expectation on the mock event handler
    mocks.ExpectCall(mockEventHandler, ChannelEventHandlerBase::handleEvent).With(testEvent);
    
    // Call handler
    mockEventHandler->handleEvent(testEvent);
}

TEST_F(EventLogTest, EventLogSubscriptionCreation) {
    // Create a subscription for the test channel
    EventLogSubscription subscription(L"TestChannel");
    
    // Verify properties
    EXPECT_EQ(subscription.getChannel(), L"TestChannel");
    EXPECT_FALSE(subscription.isRunning());
    EXPECT_EQ(subscription.getPosition(), nullptr);
}

TEST_F(EventLogTest, EventLogSubscriptionSetHandler) {
    // Create a subscription
    EventLogSubscription subscription(L"TestChannel");
    
    // Set handler and verify
    subscription.setHandler(std::shared_ptr<ChannelEventHandlerBase>(mockEventHandler));
    
    // Test channel matching
    EXPECT_EQ(subscription.getChannel(), mockEventHandler->getChannel());
}
