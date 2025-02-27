#include "pch.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../Agent/Configuration.h"
#include "../Agent/Registry.h"
#include "../Infrastructure/Logger.h"

using namespace Syslog_agent;

// Mock registry class for testing
class MockRegistry : public Registry {
public:
    static void setMockConfigValues(const std::map<std::wstring, std::wstring>& values) {
        mockValues = values;
    }

    static void clearMockValues() {
        mockValues.clear();
    }

    static bool readRegistryValueW(const wchar_t* key, const wchar_t* name, std::wstring& value) {
        auto it = mockValues.find(std::wstring(name));
        if (it != mockValues.end()) {
            value = it->second;
            return true;
        }
        return false;
    }

private:
    static std::map<std::wstring, std::wstring> mockValues;
};

std::map<std::wstring, std::wstring> MockRegistry::mockValues;

class ConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear any mock values before each test
        MockRegistry::clearMockValues();
    }
};

TEST_F(ConfigurationTest, DefaultConstructor) {
    Configuration config;
    
    // Verify default values
    EXPECT_FALSE(config.getLogToAgentEvents());
    EXPECT_FALSE(config.getUseLogAgent());
    EXPECT_TRUE(config.getLogs().empty());
    EXPECT_EQ(config.getMaxBatchAge(), 10); // Default is 10 seconds
    EXPECT_EQ(config.getMaxBatchCount(), 100); // Default is 100 events
}

TEST_F(ConfigurationTest, LoadFromRegistry) {
    // Set up mock registry values
    std::map<std::wstring, std::wstring> mockValues = {
        {L"PrimaryHost", L"logzilla.example.com"},
        {L"PrimaryApiKey", L"test-api-key"},
        {L"PrimaryUseTls", L"1"},
        {L"PrimaryPort", L"443"},
        {L"MaxBatchAge", L"15"},
        {L"MaxBatchCount", L"200"},
        {L"Facility", L"16"},
        {L"Severity", L"5"},
        {L"HostName", L"testhost"},
        {L"OnlyWhileRunning", L"1"}
    };
    
    MockRegistry::setMockConfigValues(mockValues);
    
    Configuration config;
    config.loadFromRegistry(false, false, Logger::LogLevel::ALWAYS);
    
    // Verify loaded values
    EXPECT_EQ(config.getPrimaryHost(), L"logzilla.example.com");
    EXPECT_EQ(config.getPrimaryApiKey(), L"test-api-key");
    EXPECT_TRUE(config.getPrimaryUseTls());
    EXPECT_EQ(config.getPrimaryPort(), 443);
    EXPECT_EQ(config.getMaxBatchAge(), 15);
    EXPECT_EQ(config.getMaxBatchCount(), 200);
    EXPECT_EQ(config.getFacility(), 16);
    EXPECT_EQ(config.getSeverity(), 5);
    EXPECT_EQ(config.getHostName(), L"testhost");
    EXPECT_TRUE(config.getOnlyWhileRunning());
}

TEST_F(ConfigurationTest, SecondaryServerConfiguration) {
    // Set up mock registry values with secondary server
    std::map<std::wstring, std::wstring> mockValues = {
        {L"PrimaryHost", L"primary.example.com"},
        {L"PrimaryApiKey", L"primary-api-key"},
        {L"SecondaryHost", L"secondary.example.com"},
        {L"SecondaryApiKey", L"secondary-api-key"},
        {L"SecondaryUseTls", L"1"},
        {L"SecondaryPort", L"8443"}
    };
    
    MockRegistry::setMockConfigValues(mockValues);
    
    Configuration config;
    config.loadFromRegistry(false, false, Logger::LogLevel::ALWAYS);
    
    // Verify primary values
    EXPECT_EQ(config.getPrimaryHost(), L"primary.example.com");
    EXPECT_EQ(config.getPrimaryApiKey(), L"primary-api-key");
    
    // Verify secondary values
    EXPECT_TRUE(config.hasSecondaryHost());
    EXPECT_EQ(config.getSecondaryHost(), L"secondary.example.com");
    EXPECT_EQ(config.getSecondaryApiKey(), L"secondary-api-key");
    EXPECT_TRUE(config.getSecondaryUseTls());
    EXPECT_EQ(config.getSecondaryPort(), 8443);
}

TEST_F(ConfigurationTest, LogConfigurationHandling) {
    // Set up mock registry values with log configurations
    std::map<std::wstring, std::wstring> mockValues = {
        {L"LogCount", L"2"},
        {L"Log_0_Name", L"Application"},
        {L"Log_0_Channel", L"Application"},
        {L"Log_0_Bookmark", L"bookmark1"},
        {L"Log_1_Name", L"System"},
        {L"Log_1_Channel", L"System"},
        {L"Log_1_Bookmark", L"bookmark2"}
    };
    
    MockRegistry::setMockConfigValues(mockValues);
    
    Configuration config;
    config.loadFromRegistry(false, false, Logger::LogLevel::ALWAYS);
    
    // Verify log configurations
    const auto& logs = config.getLogs();
    ASSERT_EQ(logs.size(), 2);
    
    EXPECT_EQ(logs[0].name_, L"Application");
    EXPECT_EQ(logs[0].channel_, L"Application");
    EXPECT_EQ(logs[0].bookmark_, L"bookmark1");
    
    EXPECT_EQ(logs[1].name_, L"System");
    EXPECT_EQ(logs[1].channel_, L"System");
    EXPECT_EQ(logs[1].bookmark_, L"bookmark2");
}

TEST_F(ConfigurationTest, OverrideLogLevel) {
    Configuration config;
    
    // Test with override enabled
    config.loadFromRegistry(false, true, Logger::DEBUG2);
    EXPECT_EQ(config.getLogLevel(), Logger::DEBUG2);
    
    // Test with override disabled but console mode enabled
    config.loadFromRegistry(true, false, Logger::ALWAYS);
    EXPECT_EQ(config.getLogLevel(), Logger::DEBUG); // Console mode default is DEBUG
    
    // Test with both disabled (should use registry or default)
    // Set up mock registry with log level
    std::map<std::wstring, std::wstring> mockValues = {
        {L"LogLevel", L"3"} // 3 = INFO
    };
    MockRegistry::setMockConfigValues(mockValues);
    
    config.loadFromRegistry(false, false, Logger::ALWAYS);
    EXPECT_EQ(config.getLogLevel(), Logger::INFO);
}

TEST_F(ConfigurationTest, TailFileConfiguration) {
    // Set up mock registry values for file tailing
    std::map<std::wstring, std::wstring> mockValues = {
        {L"TailFilename", L"C:\\logs\\app.log"},
        {L"TailProgramName", L"MyApplication"}
    };
    
    MockRegistry::setMockConfigValues(mockValues);
    
    Configuration config;
    config.loadFromRegistry(false, false, Logger::LogLevel::ALWAYS);
    
    // Verify tail file configuration
    EXPECT_EQ(config.getTailFilename(), L"C:\\logs\\app.log");
    EXPECT_EQ(config.getTailProgramName(), L"MyApplication");
}
