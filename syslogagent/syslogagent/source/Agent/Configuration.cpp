/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
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

#define REG_BUFFER_LEN 2048

int Configuration::debug_level_setting_ = Logger::NOLOG;
int Configuration::event_log_poll_interval_ = SharedConstants::Defaults::POLL_INTERVAL_SEC;

Configuration::Configuration() {
    getTimeZoneOffset();
}

bool Configuration::hasSecondaryHost() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return forward_to_secondary_ && secondary_host_.size() > 0;
}

void Configuration::loadFromRegistry(bool running_from_console, bool override_log_level, 
    Logger::LogLevel override_log_level_setting) {
    // Use unique_lock for write operations
    unique_lock lock(mutex_);

    Registry registry;
    registry.open();

    if (override_log_level) {
        debug_level_setting_ = override_log_level_setting;
    }
    else {
        debug_level_setting_ = registry.readInt(SharedConstants::RegistryKey::DEBUG_LEVEL_SETTING, 0);
    }
    
    debug_log_file_ = registry.readString(SharedConstants::RegistryKey::DEBUG_LOG_FILE, L"");
    Logger::setLogFile(debug_log_file_);
    
    if (debug_level_setting_ != (int)Logger::NOLOG) {
        if (debug_log_file_.length() > 0) {
            Logger::setLogDestination(running_from_console ? 
                Logger::DEST_CONSOLE_AND_FILE : Logger::DEST_FILE);
        }
        else {
            Logger::setLogDestination(Logger::DEST_CONSOLE);
        }
        Logger::setLogLevel((Logger::LogLevel)debug_level_setting_);
    }
    else {
        Logger::setLogLevel(Logger::NOLOG);
    }
    // Handle legacy only_while_running_ string conversion
    try {
        only_while_running_ = registry.readBool(SharedConstants::RegistryKey::ONLY_WHILE_RUNNING, false);
    }
    catch (const std::exception&) {
        try {
            wstring bad_reg_string = registry.readString(SharedConstants::RegistryKey::ONLY_WHILE_RUNNING, L"");
            wstring lower_str = Util::toLowercase(bad_reg_string);
            only_while_running_ = (lower_str == L"true")
                || (lower_str == L"yes")
                || (bad_reg_string == L"1");
        }
        catch (const std::exception&) {
            only_while_running_ = false;
        }
    }

    api_path_ = SharedConstants::HTTP_API_PATH;
    event_log_poll_interval_ = registry.readInt(
        SharedConstants::RegistryKey::EVENT_LOG_POLL_INTERVAL, 
        SharedConstants::Defaults::POLL_INTERVAL_SEC);
    
    if (event_log_poll_interval_ == 0) {
        event_log_poll_interval_ = SharedConstants::Defaults::POLL_INTERVAL_SEC;
    }

    primary_host_ = registry.readString(SharedConstants::RegistryKey::PRIMARY_HOST, L"localhost");
    primary_api_key_ = registry.readString(SharedConstants::RegistryKey::PRIMARY_API_KEY, L"");
    Logger::debug2("Configuration::loadFromRegistry() primary api key: %ls\n", primary_api_key_.c_str());

    // Try to read primary port from registry
    try {
        primary_port_ = registry.readInt(SharedConstants::RegistryKey::PRIMARY_PORT, 0);
        if (primary_port_ > 0) {
            Logger::debug2("Configuration::loadFromRegistry() primary port from registry: %d\n", primary_port_);
        }
    }
    catch (const std::exception&) {
        primary_port_ = 0;  // Will be determined by format and TLS
    }
    
    // Try to read secondary port from registry
    try {
        secondary_port_ = registry.readInt(SharedConstants::RegistryKey::SECONDARY_PORT, 0);
        if (secondary_port_ > 0) {
            Logger::debug2("Configuration::loadFromRegistry() secondary port from registry: %d\n", secondary_port_);
        }
    }
    catch (const std::exception&) {
        secondary_port_ = 0;  // Will be determined by format and TLS
    }

    secondary_host_ = registry.readString(SharedConstants::RegistryKey::SECONDARY_HOST, L"");
    secondary_api_key_ = registry.readString(SharedConstants::RegistryKey::SECONDARY_API_KEY, L"");
    Logger::debug2("Configuration::loadFromRegistry() secondary api key: %ls\n", secondary_api_key_.c_str());
    
    suffix_ = registry.readString(SharedConstants::RegistryKey::SUFFIX, L"");
    forward_to_secondary_ = registry.readBool(SharedConstants::RegistryKey::FORWARD_TO_SECONDARY, false);
    primary_use_tls_ = registry.readBool(SharedConstants::RegistryKey::PRIMARY_USE_TLS, false);
    secondary_use_tls_ = registry.readBool(SharedConstants::RegistryKey::SECONDARY_USE_TLS, false);
    lookup_accounts_ = registry.readBool(SharedConstants::RegistryKey::LOOKUP_ACCOUNTS, true);
    batch_interval_ = registry.readInt(SharedConstants::RegistryKey::BATCH_INTERVAL, SharedConstants::Defaults::BATCH_INTERVAL);
    facility_ = registry.readInt(SharedConstants::RegistryKey::FACILITY, SharedConstants::Defaults::FACILITY);
    severity_ = registry.readInt(SharedConstants::RegistryKey::SEVERITY, SharedConstants::Defaults::SEVERITY);
    tail_filename_ = registry.readString(SharedConstants::RegistryKey::TAIL_FILENAME, L"");
    char tail_file_buf[1024];
    Util::wstr2str(tail_file_buf, sizeof(tail_file_buf), tail_filename_.c_str());
    Logger::debug("Tail requested for file %s\n", tail_file_buf);
    
    tail_program_name_ = registry.readString(SharedConstants::RegistryKey::TAIL_PROGRAM_NAME, L"");
    include_vs_ignore_eventids_ = registry.readBool(SharedConstants::RegistryKey::INCLUDE_VS_IGNORE_EVENT_IDS, false);
    loadFilterIds(registry.readString(SharedConstants::RegistryKey::EVENT_ID_FILTER, L""));

    auto channels = registry.readChannels();
    logs_.clear();
    logs_.reserve(channels.size());
    
    for (const auto& channel : channels) {
        logs_.push_back(LogConfiguration());
        logs_.back().channel_ = channel;
        // Set the name_ field to be the same as channel_ initially
        logs_.back().name_ = channel;
        char channel_buf[1024];
        Util::wstr2str(channel_buf, sizeof(channel_buf), channel.c_str());
        logs_.back().nname_ = channel_buf;
        logs_.back().loadFromRegistry(registry);
        Logger::debug("Configuration::loadFromRegistry() event log %ls\n", channel.c_str());
    }

    registry.close();
    host_name_ = getHostName();

    Logger::debug("Loaded configuration from registry (from console: %s)\n", 
        (running_from_console ? "true" : "false"));
}

void Configuration::saveToRegistry() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    Registry registry;
    registry.open();
    
    for (const auto& log : logs_) {
        log.saveToRegistry(registry);
    }
    
    registry.close();
}

void Configuration::loadFilterIds(wstring value) {
    if (value.empty()) return;

    set<DWORD> new_filter;
    auto input = value + L",";
    auto id = 0u;

    for (size_t i = 0; i < input.size(); i++) {
        if (input[i] == L',') {
            if (id > 0) {
                Logger::debug2("Configuration::loadFilterIds() adding event filter id for %u\n", id);
                new_filter.insert(id);
            }
            id = 0;
            continue;
        }
        
        if (input[i] >= L'0' && input[i] <= L'9') {
            id = id * 10 + (input[i] - L'0');
        }
    }

    event_id_filter_ = std::move(new_filter);
}

void Configuration::getTimeZoneOffset() {
    TIME_ZONE_INFORMATION time_zone_info;
    GetTimeZoneInformation(&time_zone_info);
    utc_offset_minutes_ = static_cast<int>(time_zone_info.Bias);
}

string Configuration::getHostName() const {
    static constexpr size_t HOSTNAME_BUFFER_SIZE = MAX_COMPUTERNAME_LENGTH + 1;
    WCHAR computerName[HOSTNAME_BUFFER_SIZE];
    DWORD size = HOSTNAME_BUFFER_SIZE;

    if (GetComputerNameW(computerName, &size) == TRUE) {
        char computer_name_buf[1024];
        Util::wstr2str(computer_name_buf, sizeof(computer_name_buf), computerName);
        return computer_name_buf;
    }
    
    return string();
}

int Configuration::setLogformatForVersion(int& logformat, const string& version) {
    // DEBUGGING
	return SharedConstants::LOGFORMAT_JSONPORT;

    if (Util::compareSoftwareVersions(version, SharedConstants::LOGFORMAT_LZ_VERSION_HTTP) < 0) {
        logformat = SharedConstants::LOGFORMAT_JSONPORT;
        return logformat;
    }
    else {
        logformat = SharedConstants::LOGFORMAT_HTTPPORT;
        return logformat;
    }
}

void Configuration::setPrimaryLogzillaVersion(const string& version) {
    unique_lock lock(mutex_);
    primary_logzilla_version_ = version;
    setLogformatForVersion(primary_logformat_, version);
}

void Configuration::setSecondaryLogzillaVersion(const string& version) {
    unique_lock lock(mutex_);
    secondary_logzilla_version_ = version;
    setLogformatForVersion(secondary_logformat_, version);
}

int Configuration::getPrimaryLogformat() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (primary_logformat_ == SharedConstants::LOGFORMAT_DETECT) {
        return SharedConstants::LOGFORMAT_JSONPORT;
    }
    return primary_logformat_;
}

int Configuration::getSecondaryLogformat() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (secondary_logformat_ == SharedConstants::LOGFORMAT_DETECT) {
        return SharedConstants::LOGFORMAT_JSONPORT;
    }
    return secondary_logformat_;
}

int Configuration::getPrimaryPort() const {
    shared_lock<shared_mutex> lock(mutex_);
    
#if THIS_CODE_IS_OBSOLETE
    if (primary_port_ > 0) {
        return primary_port_;
    }
    
    // If no port specified in registry, determine by format and TLS
    if (primary_logformat_ == SharedConstants::LOGFORMAT_DETECT || 
        primary_logformat_ == SharedConstants::LOGFORMAT_JSONPORT) {
        return 514;  // Default syslog port for JSON format
    }
    
    return primary_use_tls_ ? 443 : 80;  // HTTP ports
#endif
    return primary_port_;
}

int Configuration::getSecondaryPort() const {
    shared_lock<shared_mutex> lock(mutex_);
    
    if (secondary_port_ > 0) {
        return secondary_port_;
    }
    
    // If no port specified in registry, determine by format and TLS
    if (secondary_logformat_ == SharedConstants::LOGFORMAT_DETECT || 
        secondary_logformat_ == SharedConstants::LOGFORMAT_JSONPORT) {
        return 514;  // Default syslog port for JSON format
    }
    
    return secondary_use_tls_ ? 443 : 80;  // HTTP ports
}
