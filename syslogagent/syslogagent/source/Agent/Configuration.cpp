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
#include "WindowsEventLog.h"

using namespace Syslog_agent;

// Local implementation of wstr2str to resolve compiler errors
// This is a temporary solution until project dependencies are properly updated
namespace {
    size_t local_wstr2str(char* dest, size_t dest_size, const wchar_t* src) {
        if (!dest || !src || dest_size == 0) return 0;

        size_t converted = 0;
        if (WideCharToMultiByte(CP_UTF8, 0, src, -1, dest, static_cast<int>(dest_size), NULL, NULL)) {
            converted = strlen(dest);
        }
        dest[dest_size - 1] = '\0';  // Ensure null termination
        return converted;
    }
}

#define REG_BUFFER_LEN 2048

int Configuration::debug_level_setting_ = Logger::NONE;
int Configuration::event_log_poll_interval_ = SharedConstants::Defaults::POLL_INTERVAL_SEC;

Configuration::Configuration() {
    getTimeZoneOffset();
    setHostName();
}

bool Configuration::hasSecondaryHost() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return forward_to_secondary_ && secondary_host_.size() > 0;
}


void Configuration::loadFromRegistry(bool running_from_console, bool override_log_level,
    Logger::LogLevel override_log_level_setting) {
    auto logger = LOG_THIS;
    // Use unique_lock for write operations
    unique_lock lock(mutex_);

    Registry registry;
	LAST_RESORT_LOGGER->always("Configuration::loadFromRegistry() opening registry\n");
    registry.open();

    if (override_log_level) {
        debug_level_setting_ = override_log_level_setting;
    }
    else {
        debug_level_setting_ = registry.readInt(SharedConstants::RegistryKey::DEBUG_LEVEL_SETTING, 0);
		LAST_RESORT_LOGGER->always("Configuration::loadFromRegistry() debug level setting: %d\n", debug_level_setting_);
    }
    
    debug_log_file_ = registry.readString(SharedConstants::RegistryKey::DEBUG_LOG_FILE, L"");
    
    // Handle both absolute and relative paths for the debug log file
    if (!debug_log_file_.empty()) {
        // Check if the path starts with a drive letter (e.g., C:), a UNC path (\\server), or a single leading backslash
        bool is_absolute_path = false;
        
        // Check for drive letter (contains ':')
        if (debug_log_file_.find(L':') != std::wstring::npos) {
            is_absolute_path = true;
        }
        // Check for UNC path (starts with "\\")
        else if (debug_log_file_.size() >= 2 && debug_log_file_[0] == L'\\' && debug_log_file_[1] == L'\\') {
            is_absolute_path = true;
        }
        // Check for a single leading backslash (assume it's from the root of C:)
        else if (debug_log_file_.size() >= 1 && debug_log_file_[0] == L'\\') {
            debug_log_file_ = L"C:" + debug_log_file_;
            is_absolute_path = true;
        }
        
        if (is_absolute_path) {
            logger->setLogFileW(debug_log_file_);
        } else {
            std::wstring fullPath = Util::getThisPath(true) + debug_log_file_;
            logger->setLogFileW(fullPath);
        }
    }
    
    if (debug_level_setting_ != (int)Logger::NONE) {
        if (debug_log_file_.length() > 0) {
            logger->setLogDestination(running_from_console ? 
                Logger::DEST_CONSOLE_AND_FILE : Logger::DEST_FILE);
        }
        else {
            logger->setLogDestination(Logger::DEST_CONSOLE);
        }
        logger->setLogLevel((Logger::LogLevel)debug_level_setting_);
        if ((Logger::LogLevel)debug_level_setting_ == Logger::DEBUG3) {
            if (LAST_RESORT_LOGGER->getLogDestination() == Logger::DEST_NONE) {
                logger_holder_ = std::make_shared<Logger>(Logger::LAST_RESORT_LOGGER_NAME);
                std::string logFilePath = Util::getAppropriateLogPath("syslogagent_failsafe.log");
                logger_holder_->setLogFile(logFilePath.c_str());
                logger_holder_->setLogDestination(Logger::DEST_FILE);
                logger_holder_->setCloseAfterWrite(true);
                Logger::setLogger(logger_holder_, { Logger::LAST_RESORT_LOGGER_NAME });
                // Log the location of the last resort log file to the Windows Event Log
                WindowsEventLog eventLog;
                eventLog.WriteEvent(
                    WindowsEventLog::EventType::INFORMATION_EVENT,
                    1000,  // Event ID
                    "LogZilla SyslogAgent started",
                    ("Last resort log file is located at: " + logFilePath).c_str());
            }
        }
    }
    else {
        logger->setLogLevel(Logger::NONE);
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
    logger->debug2("Configuration::loadFromRegistry() primary api key: %ls\n", primary_api_key_.c_str());

    // Try to read primary port from registry
    try {
        primary_port_ = registry.readInt(SharedConstants::RegistryKey::PRIMARY_PORT, 0);
        if (primary_port_ > 0) {
            logger->debug2("Configuration::loadFromRegistry() primary port from registry: %d\n", primary_port_);
        }
    }
    catch (const std::exception&) {
        primary_port_ = 0;  // Will be determined by format and TLS
    }
    
    // Try to read secondary port from registry
    try {
        secondary_port_ = registry.readInt(SharedConstants::RegistryKey::SECONDARY_PORT, 0);
        if (secondary_port_ > 0) {
            logger->debug2("Configuration::loadFromRegistry() secondary port from registry: %d\n", secondary_port_);
        }
    }
    catch (const std::exception&) {
        secondary_port_ = 0;  // Will be determined by format and TLS
    }

    secondary_host_ = registry.readString(SharedConstants::RegistryKey::SECONDARY_HOST, L"");
    secondary_api_key_ = registry.readString(SharedConstants::RegistryKey::SECONDARY_API_KEY, L"");
    logger->debug2("Configuration::loadFromRegistry() secondary api key: %ls\n", secondary_api_key_.c_str());
    
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
    local_wstr2str(tail_file_buf, sizeof(tail_file_buf), tail_filename_.c_str());
    logger->debug("Tail requested for file %s\n", tail_file_buf);
    
    tail_program_name_ = registry.readString(SharedConstants::RegistryKey::TAIL_PROGRAM_NAME, L"");
    include_vs_ignore_eventids_ = registry.readBool(SharedConstants::RegistryKey::INCLUDE_VS_IGNORE_EVENT_IDS, false);
    loadFilterIds(registry.readString(SharedConstants::RegistryKey::EVENT_ID_FILTER, L""));

    // Load batch configuration
    max_batch_size_ = static_cast<uint32_t>(registry.readInt(SharedConstants::RegistryKey::MAX_BATCH_SIZE,
        SharedConstants::Defaults::MAX_BATCH_SIZE));
    max_batch_age_ = static_cast<uint32_t>(registry.readInt(SharedConstants::RegistryKey::MAX_BATCH_AGE,
        SharedConstants::Defaults::MAX_BATCH_AGE));

    auto channels = registry.readChannels();
    logs_.clear();
    logs_.reserve(channels.size());
    
    for (const auto& channel : channels) {
        logs_.push_back(LogConfiguration());
        logs_.back().channel_ = channel;
        // Set the name_ field to be the same as channel_ initially
        logs_.back().name_ = channel;
        char channel_buf[1024];
        local_wstr2str(channel_buf, sizeof(channel_buf), channel.c_str());
        logs_.back().nname_ = channel_buf;
        logs_.back().loadFromRegistry(registry);
        logger->debug("Configuration::loadFromRegistry() event log %ls\n", channel.c_str());
    }

    registry.close();

    logger->debug("Loaded configuration from registry (from console: %s)\n", 
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
    auto logger = LOG_THIS;
    if (value.empty()) return;

    set<DWORD> new_filter;
    auto input = value + L",";
    auto id = 0u;

    for (size_t i = 0; i < input.size(); i++) {
        if (input[i] == L',') {
            if (id > 0) {
                logger->debug2("Configuration::loadFilterIds() adding event filter id for %u\n", id);
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

void Configuration::setHostName() {
    auto logger = LOG_THIS;
    static constexpr size_t HOSTNAME_BUFFER_SIZE = MAX_COMPUTERNAME_LENGTH + 1;
    WCHAR computerName[HOSTNAME_BUFFER_SIZE];
    DWORD size = HOSTNAME_BUFFER_SIZE;

    if (GetComputerNameW(computerName, &size) == TRUE) {
        char computer_name_buf[1024];
        local_wstr2str(computer_name_buf, sizeof(computer_name_buf), computerName);
        host_name_ = string(computer_name_buf);
    } else {
        logger->warning("Configuration::setHostName() GetComputerNameW() failed: %u\n", GetLastError());
        host_name_ = string("unknown");
    }
}

const string& Configuration::getHostName() const {
    shared_lock<shared_mutex> lock(mutex_);
    return host_name_;
}

int Configuration::setLogformatForVersion(int& logformat, const string& version) {

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
