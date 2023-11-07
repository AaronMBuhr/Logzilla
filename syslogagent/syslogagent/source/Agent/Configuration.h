/*
SyslogAgent: a syslog agent for Windows
Copyright © 2021 Logzilla Corp.
*/

#pragma once

#include <set>
#include <vector>
#include "Logger.h"
#include "LogConfiguration.h"
#include "SyslogAgentSharedConstants.h"

using namespace std;

namespace Syslog_agent {

    class Configuration {
    public:
        static const wstring PRIMARY_CERT_FILENAME;
        static const wstring SECONDARY_CERT_FILENAME;

        bool lookup_accounts_ = false;
        bool include_key_value_pairs_ = false;
        bool forward_to_secondary_ = false;
        const bool use_ping_ = false;
        const bool use_tcp_ = true;
        const bool use_RFC3164_ = false;
        const bool use_json_message_ = true;
        bool use_log_agent_ = true;
        bool primary_use_tls_ = false;
        bool secondary_use_tls_ = false;
        int debug_level_setting_ = Logger::NOLOG;
        int	event_log_poll_interval_ = 10;
        int facility_;
        int severity_;
        char host_name_[256];
        wstring primary_host_ = SYSLOGAGENT_DEFAULT_PRIMARY_HOST;
        wstring primary_port_ = SYSLOGAGENT_DEFAULT_PRIMARY_PORT_S;
        wstring primary_tls_port_ = SYSLOGAGENT_DEFAULT_PRIMARY_TLS_PORT_S;
        wstring secondary_host_ = SYSLOGAGENT_DEFAULT_SECONDARY_HOST;
        wstring secondary_port_ = SYSLOGAGENT_DEFAULT_SECONDARY_PORT_S;
        wstring secondary_tls_port_ = SYSLOGAGENT_DEFAULT_SECONDARY_TLS_PORT_S;
        const bool use_forwarder_ = SYSLOGAGENT_DEFAULT_USE_FORWARDER;
        wstring forwarder_destination_ = SYSLOGAGENT_DEFAULT_FORWARDER_DEST;
        wstring udp_forwarder_port_ = SYSLOGAGENT_DEFAULT_UDP_FWD_PORT_S;
        wstring tcp_forwarder_port_ = SYSLOGAGENT_DEFAULT_TCP_FWD_PORT_S;
        wstring suffix_ = SYSLOGAGENT_DEFAULT_SUFFIX;
        wstring debug_log_file_ = SYSLOGAGENT_DEFAULT_DEBUG_LOG_FILENAME;
        vector<LogConfiguration> logs_;
        set<DWORD> event_id_filter_;
        wstring tail_filename_;
        wstring tail_program_name_;
        int utc_offset_minutes_;
        bool include_vs_ignore_eventids_;

        void loadFromRegistry(bool running_from_console, bool override_log_level, Logger::LogLevel override_log_level_setting);
        void saveToRegistry();
        bool hasSecondaryHost() const;

        const int MAX_TAIL_FILE_LINE_LENGTH = 16000;

    private:
        void loadFilterIds(wstring value);
        void getTimeZoneOffset();
    };
}

