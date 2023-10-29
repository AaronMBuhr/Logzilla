/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

#include "stdafx.h"
#include "CppUnitTest.h"
#include "Test_logger.h"
#include "Logger.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(Logger_test) {

    TEST_METHOD(duplicate_messages_are_not_logged) {
        Syslog_agent::Logger::set_level(Syslog_agent::information);
        Syslog_agent::Logger::write(Syslog_agent::error, "bad stuff");
        Syslog_agent::Logger::write(Syslog_agent::error, "bad stuff");
        Assert::AreEqual(1, log_count);
    }

    TEST_METHOD(debug_level_logs_everything) {
        Syslog_agent::Logger::set_level(Syslog_agent::debug);
        Syslog_agent::Logger::write(Syslog_agent::error, "bad stuff");
        Syslog_agent::Logger::write(Syslog_agent::error, "bad stuff");
        Assert::AreEqual(2, log_count);
    }

    TEST_METHOD_INITIALIZE(setup) {
        set_test_logger();
    }
};
