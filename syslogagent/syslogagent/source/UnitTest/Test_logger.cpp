/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

#include "stdafx.h"
#include "Logger.h"

using namespace Syslog_agent;

std::string last_log_message;
int log_count;

static void test_logger(Log_level level, const char* message) {
    last_log_message = message;
    log_count++;
}

void set_test_logger() {
    Logger::set_method(test_logger);
    last_log_message.clear();
    log_count = 0;
}
