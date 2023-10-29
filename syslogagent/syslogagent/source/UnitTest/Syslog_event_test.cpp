/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

#include "stdafx.h"
#include "CppUnitTest.h"
#include "Syslog_event.h"
#include <ctime>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Syslog_agent;

TEST_CLASS(Syslog_event_test) {
public:

    TEST_METHOD(rfc3164_is_output) {
        Configuration config;
        config.include_key_value_pairs = false;
        Syslog_event event(config);
        set_event(event);

        std::string result;
        event.output_3164(result, "myhost");

        Assert::AreEqual("<123>Jan  2 03:04:05 myhost from_here this is a message", result.c_str());
    }

    TEST_METHOD(rfc3164_is_output_with_kvps) {
        Configuration config;
        config.include_key_value_pairs = true;
        Syslog_event event(config);
        set_event(event);

        std::string result;
        event.output_3164(result, "myhost");

        Assert::AreEqual("<123>Jan  2 03:04:05 myhost from_here this is a message EventID=\"654321\" Source=\"LZ_SyslogAgent\"", result.c_str());
    }

    TEST_METHOD(rfc3164_is_output_with_character_substitution) {
        Configuration config;
        config.include_key_value_pairs = true;
        config.carriage_return_replacement = 32;
        config.line_feed_replacement = 0;
        config.tab_replacement = '!';
        Syslog_event event(config);
        set_event(event);
        event.append_message(L"\r\n\t");
        event.add_string(L"some");
        event.add_string(L"\"special\" stuff");

        std::string result;
        event.output_3164(result, "myhost");

        Assert::AreEqual("<123>Jan  2 03:04:05 myhost from_here this is a message ! EventID=\"654321\" Source=\"LZ_SyslogAgent\" S1=\"some\" S2=\"'special' stuff\"", result.c_str());
    }

    TEST_METHOD(rfc3164_is_output_with_parameters) {
        Configuration config;
        config.include_key_value_pairs = true;
        Syslog_event event(config);
        set_event(event);
        event.add_string(L"some");
        event.add_string(L"stuff");

        std::string result;
        event.output_3164(result, "myhost");

        Assert::AreEqual(
            "<123>Jan  2 03:04:05 myhost from_here this is a message EventID=\"654321\" Source=\"LZ_SyslogAgent\" S1=\"some\" S2=\"stuff\"",
            result.c_str());
    }

    TEST_METHOD(rfc3164_is_output_with_suffix) {
        Configuration config;
        config.suffix = L"mysuffix";
        config.include_key_value_pairs = false;
        Syslog_event event(config);
        set_event(event);

        std::string result;
        event.output_3164(result, "myhost");

        Assert::AreEqual("<123>Jan  2 03:04:05 myhost from_here this is a message mysuffix", result.c_str());
    }

    TEST_METHOD(rfc5424_is_output) {
        Configuration config;
        config.include_key_value_pairs = true;
        Syslog_event event(config);
        set_event(event);

        std::string result;
        event.output_5424(result, "myhost");

        assert_5424(result.c_str(), "");
    }

    TEST_METHOD(rfc5424_is_output_with_parameters) {
        Configuration config;
        config.include_key_value_pairs = true;
        Syslog_event event(config);
        set_event(event);
        event.add_string(L"some");
        event.add_string(L"stuff");

        std::string result;
        event.output_5424(result, "myhost");

        assert_5424(result.c_str(), " S1=\"some\" S2=\"stuff\"");
    }

    TEST_METHOD(rfc5424_is_output_with_suffix) {
        Configuration config;
        config.suffix = L"mysuffix";
        config.include_key_value_pairs = false;
        Syslog_event event(config);
        set_event(event);

        std::string result;
        event.output_5424(result, "myhost");

        assert_5424(result.c_str(), " mysuffix");
    }

    static void set_event(Syslog_event& event) {
        struct tm time_info;
        time_info.tm_year = 119;
        time_info.tm_mon = 0;
        time_info.tm_mday = 2;
        time_info.tm_hour = 3;
        time_info.tm_min = 4;
        time_info.tm_sec = 5;
        event.set_time(mktime(&time_info));
        event.set_id(654321);
        event.set_priority(123);
        event.set_source(L"from here");
        event.append_message(L"this is a message");
    }

    static void assert_5424(const char* actual, const char* extra) {
        TIME_ZONE_INFORMATION time_zone;
        GetTimeZoneInformation(&time_zone);

        char expected[1000];
        sprintf_s(expected, "<123>1 2019-01-02T%d:04:05Z myhost from_here %u 654321 - \xEF\xBB\xBFthis is a message EventID=\"654321\" Source=\"LZ_SyslogAgent\"%s", 3 + time_zone.Bias / 60, GetCurrentProcessId(), extra);

        Assert::AreEqual(expected, actual);
    }
    
};
