#include <gtest/gtest.h>
#include "hippomocks.h"
#include "../Agent/Configuration.h"
#include "../Agent/Registry.h"
#include "../Infrastructure/Logger.h"

using namespace Syslog_agent;

// Test fixture helper for storing mock registry values
class TestFixture {
public:
    static std::map<std::wstring, std::wstring> mockValues;

    static void setMockConfigValues(const std::map<std::wstring, std::wstring>& values) {
        mockValues = values;
    }

    static void clearMockValues() {
        mockValues.clear();
    }

    // Helper to get a value or return default
    static std::wstring getValue(const wchar_t* name, const wchar_t* defaultValue) {
        auto it = mockValues.find(std::wstring(name));
        if (it != mockValues.end()) {
            return it->second;
        }
        return std::wstring(defaultValue);
    }

    // Helper to convert string to int or return default
    static int getIntValue(const wchar_t* name, int defaultValue) {
        auto it = mockValues.find(std::wstring(name));
        if (it != mockValues.end()) {
            try {
                return std::stoi(it->second);
            }
            catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    // Helper to convert string to bool or return default
    static bool getBoolValue(const wchar_t* name, bool defaultValue) {
        auto it = mockValues.find(std::wstring(name));
        if (it != mockValues.end()) {
            std::wstring value = it->second;
            if (value == L"1" || value == L"true" || value == L"yes") {
                return true;
            }
            else if (value == L"0" || value == L"false" || value == L"no") {
                return false;
            }
        }
        return defaultValue;
    }
};

std::map<std::wstring, std::wstring> TestFixture::mockValues;

class ConfigurationTest : public ::testing::Test {
protected:
    MockRepository mocks;
    Registry* mockRegistry;

    void SetUp() override {
        // Clear any mock values before each test
        TestFixture::clearMockValues();

        // Create mock registry
        mockRegistry = mocks.Mock<Registry>();

        // Set up the default mock behavior for Registry::open (non-const version)
        static void (Registry:: * openNoArgPtr)() = static_cast<void (Registry::*)()>(&Registry::open);
        mocks.OnCall(mockRegistry, openNoArgPtr);

        // Set up mocks for const methods using function pointers to member functions.
        // Note: use OnCallConst for methods declared as const.
        static std::wstring(Registry:: * readStringMemberPtr)(const wchar_t*, const wchar_t*) const =
            static_cast<std::wstring(Registry::*)(const wchar_t*, const wchar_t*) const>(&Registry::readString);
        static int (Registry:: * readIntMemberPtr)(const wchar_t*, int) const =
            static_cast<int (Registry::*)(const wchar_t*, int) const>(&Registry::readInt);
        static bool (Registry:: * readBoolMemberPtr)(const wchar_t*, bool) const =
            static_cast<bool (Registry::*)(const wchar_t*, bool) const>(&Registry::readBool);

        // Set up pointer to wrapper functions that include the Registry pointer as the first parameter.
        static std::wstring(*readStringPtr)(const Registry*, const wchar_t*, const wchar_t*) = &ConfigurationTest::readStringWrapper;
        static int (*readIntPtr)(const Registry*, const wchar_t*, int) = &ConfigurationTest::readIntWrapper;
        static bool (*readBoolPtr)(const Registry*, const wchar_t*, bool) = &ConfigurationTest::readBoolWrapper;

        // Use OnCallConst for mocking the const member functions
        mocks.OnCallConst(mockRegistry, readStringMemberPtr).Do(readStringPtr);
        mocks.OnCallConst(mockRegistry, readIntMemberPtr).Do(readIntPtr);
        mocks.OnCallConst(mockRegistry, readBoolMemberPtr).Do(readBoolPtr);
    }

    void TearDown() override {
        // Verify all expectations have been met
        mocks.VerifyAll();
    }

    virtual ~ConfigurationTest() noexcept override = default;

    // Static helper functions to wrap TestFixture calls for mock handlers.
    // They take a pointer to Registry as the first parameter.
    static std::wstring readStringWrapper(const Registry* /*self*/, const wchar_t* name, const wchar_t* defaultValue) {
        return TestFixture::getValue(name, defaultValue);
    }
    static int readIntWrapper(const Registry* /*self*/, const wchar_t* name, int defaultValue) {
        return TestFixture::getIntValue(name, defaultValue);
    }
    static bool readBoolWrapper(const Registry* /*self*/, const wchar_t* name, bool defaultValue) {
        return TestFixture::getBoolValue(name, defaultValue);
    }
};

TEST_F(ConfigurationTest, DefaultConstructor) {
    Configuration config;

    // Verify default values
    EXPECT_FALSE(config.getUseLogAgent());
    EXPECT_TRUE(config.getLogs().empty());
    EXPECT_EQ(config.getMaxBatchAge(), 10); // Default is 10 seconds
    EXPECT_EQ(config.getMaxBatchCount(), 100); // Default is 100 events
}

TEST_F(ConfigurationTest, LoadFromRegistry) {
    // Set up mock registry values
    std::map<std::wstring, std::wstring> testValues = {
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

    TestFixture::setMockConfigValues(testValues);

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

    TestFixture::setMockConfigValues(mockValues);

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

    TestFixture::setMockConfigValues(mockValues);

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

    // Test with override disabled but console mode enabled
    config.loadFromRegistry(true, false, Logger::ALWAYS);

    // Test with both disabled (should use registry or default)
    // Set up mock registry with log level
    std::map<std::wstring, std::wstring> mockValues = {
        {L"LogLevel", L"3"} // 3 = INFO
    };
    TestFixture::setMockConfigValues(mockValues);

    config.loadFromRegistry(false, false, Logger::ALWAYS);
}

TEST_F(ConfigurationTest, TailFileConfiguration) {
    // Set up mock registry values for file tailing
    std::map<std::wstring, std::wstring> mockValues = {
        {L"TailFilename", L"C:\\logs\\app.log"},
        {L"TailProgramName", L"MyApplication"}
    };

    TestFixture::setMockConfigValues(mockValues);

    Configuration config;
    config.loadFromRegistry(false, false, Logger::LogLevel::ALWAYS);

    // Verify tail file configuration
    EXPECT_EQ(config.getTailFilename(), L"C:\\logs\\app.log");
    EXPECT_EQ(config.getTailProgramName(), L"MyApplication");
}
