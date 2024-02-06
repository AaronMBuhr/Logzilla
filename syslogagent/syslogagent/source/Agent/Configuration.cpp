/*
SyslogAgent: a syslog agent for Windows
Copyright © 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include "timezoneapi.h"
#include "Logger.h"
#include "Configuration.h"
#include "RecordNumber.h"
#include "Registry.h"
#include "SyslogAgentSharedConstants.h"
#include "Util.h"

using namespace Syslog_agent;

#define REG_BUFFER_LEN				2048


const wstring Configuration::PRIMARY_CERT_FILENAME = SYSLOGAGENT_CERT_FILENAME_PRIMARY;
const wstring Configuration::SECONDARY_CERT_FILENAME = SYSLOGAGENT_CERT_FILENAME_SECONDARY;;

bool Configuration::hasSecondaryHost() const {
    return forward_to_secondary_ && secondary_host_.size() > 0;
}

void Configuration::loadFromRegistry(bool running_from_console, bool override_log_level, Logger::LogLevel override_log_level_setting) {

    strcpy(host_name_, getHostName().c_str());
    Registry registry;
    registry.open();

    if (override_log_level) {
        debug_level_setting_ = override_log_level_setting;
    }
    else {
        debug_level_setting_ = registry.readInt(SYSLOGAGENT_REGISTRYKEY_DEBUG_LEVEL_SETTING, 0);
    }
    debug_log_file_ = registry.readString(SYSLOGAGENT_REGISTRYKEY_DEBUG_LOG_FILE, L"");
    Logger::setLogFile(debug_log_file_);
    if (debug_level_setting_ != (int)Logger::NOLOG) {
        if (debug_log_file_.length() > 0) {
            if (running_from_console) {
                Logger::setLogDestination(Logger::DEST_CONSOLE_AND_FILE);
            }
            else {
                Logger::setLogDestination(Logger::DEST_FILE);
            }
        }
        else {
            Logger::setLogDestination(Logger::DEST_CONSOLE);
        }
        Logger::setLogLevel((Logger::LogLevel)debug_level_setting_);
    }
    else {
        Logger::setLogLevel(Logger::NOLOG);
    }

    event_log_poll_interval_ = registry.readInt(SYSLOGAGENT_REGISTRYKEY_EVENT_LOG_POLL_INTERVAL, 10);
    primary_host_ = registry.readString(SYSLOGAGENT_REGISTRYKEY_PRIMARY_HOST, L"localhost");
    secondary_host_ = registry.readString(SYSLOGAGENT_REGISTRYKEY_SECONDARY_HOST, L"");
    suffix_ = registry.readString(SYSLOGAGENT_REGISTRYKEY_SUFFIX, L"");
    forward_to_secondary_ = registry.readBool(SYSLOGAGENT_REGISTRYKEY_FORWARD_TO_SECONDARY, false);
    primary_use_tls_ = registry.readBool(SYSLOGAGENT_REGISTRYKEY_PRIMARY_USE_TLS, false);
    secondary_use_tls_ = registry.readBool(SYSLOGAGENT_REGISTRYKEY_SECONDARY_USE_TLS, false);
    lookup_accounts_ = registry.readBool(SYSLOGAGENT_REGISTRYKEY_LOOKUP_ACCOUNTS, true);
    include_key_value_pairs_ = registry.readBool(SYSLOGAGENT_REGISTRYKEY_INCLUDE_KEY_VALUE_PAIRS, false);
    batch_interval_ = registry.readInt(SYSLOGAGENT_REGISTRYKEY_BATCH_INTERVAL, SYSLOGAGENT_DEFAULT_BATCH_INTERVAL);
    facility_ = registry.readInt(SYSLOGAGENT_REGISTRYKEY_FACILITY, SYSLOGAGENT_DEFAULT_FACILITY);
    severity_ = registry.readInt(SYSLOGAGENT_REGISTRYKEY_SEVERITY, SYSLOGAGENT_DEFAULT_SEVERITY);
    tail_filename_ = registry.readString(SYSLOGAGENT_REGISTRYKEY_TAIL_FILENAME, L"");
    tail_program_name_ = registry.readString(SYSLOGAGENT_REGISTRYKEY_TAIL_PROGRAM_NAME, L"");
    string tail_file = Util::wstr2str(tail_filename_);
    Logger::debug("Tail requested for file %s\n", tail_file.c_str());

    include_vs_ignore_eventids_ = registry.readBool(SYSLOGAGENT_REGISTRYKEY_INCLUDE_VS_IGNORE_EVENTIDS, false);
    loadFilterIds(registry.readString(SYSLOGAGENT_REGISTRYKEY_EVENT_ID_FILTER, L""));

    auto channels = registry.readChannels();
    logs_.clear();
    logs_.reserve(channels.size());
    for (auto& channel : registry.readChannels()) {
        logs_.push_back(LogConfiguration());
        logs_.back().channel_ = channel;
        logs_.back().name_ = channel;
        logs_.back().nname_ = Util::wstr2str(channel);
        logs_.back().loadFromRegistry(registry);
        Logger::debug("Configuration::loadFromRegistry() event log %ls\n", channel.c_str());
    }

    registry.close();

    getTimeZoneOffset();
    Logger::debug("Loaded configuration from registry (from console: %s)\n", (running_from_console ? "true" : "false"));

}

void Configuration::saveToRegistry() {
    Registry registry;
    registry.open();
    for (auto& log : logs_) {
        log.saveToRegistry(registry);
    }
    registry.close();
}

void Configuration::loadFilterIds(wstring value) {
    if (value.size() == 0) return;
    auto input = value + L",";
    auto id = 0;
    for (auto i = 0; i < input.size(); i++) {
        if (input[i] == L',') {
            Logger::debug2("Configuration::loadFilterIds() adding event filter id for %d\n", id);
            event_id_filter_.insert(id);
            id = 0;
        }
        if (input[i] < L'0' || input[i] > L'9') continue;
        id = id * 10 + input[i] - L'0';
    }
}

void Configuration::getTimeZoneOffset() {
    TIME_ZONE_INFORMATION time_zone_info;
    auto result = GetTimeZoneInformation(&time_zone_info);
    utc_offset_minutes_ = (int) time_zone_info.Bias;
}

string Configuration::getHostName() const {
    TCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName) / sizeof(computerName[0]);

    if (!GetComputerName(computerName, &size)) {
        return string("");
    }
    if (sizeof(TCHAR) == sizeof(wchar_t)) {
        return Util::wstr2str(wstring(computerName));
    }
    else {
        return string((char*)computerName);
    }
}