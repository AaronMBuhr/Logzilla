/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

#include "stdafx.h"
#include "CppUnitTest.h"
#include "Record_number.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Syslog_agent;

TEST_CLASS(Record_number_test) {
public:
    TEST_METHOD(increments_less_than_maximum) {
        RecordNumber number(ULONG_MAX - 1);
        number.increment();
        Assert::IsTrue(ULONG_MAX == number);
    }

    TEST_METHOD(increments_maximum) {
        RecordNumber number(ULONG_MAX);
        number.increment();
        Assert::IsTrue(0 == number);
    }

    TEST_METHOD(greater_than_with_no_wrap) {
        RecordNumber small_number(1);
        RecordNumber large_number(ULONG_MAX / 2);
        Assert::IsTrue(large_number.is_greater(small_number));
        Assert::IsFalse(small_number.is_greater(large_number));
    }

    TEST_METHOD(greater_than_with_wrap) {
        RecordNumber small_number(1);
        RecordNumber large_number(ULONG_MAX / 2 + 1);
        Assert::IsTrue(small_number.is_greater(large_number));
        Assert::IsFalse(large_number.is_greater(small_number));
    }

    TEST_METHOD(not_greater_than_when_equal) {
        RecordNumber small_number(1);
        RecordNumber large_number(1);
        Assert::IsFalse(small_number.is_greater(large_number));
        Assert::IsFalse(large_number.is_greater(small_number));
    }
};
