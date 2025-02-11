/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#pragma once

#include <set>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include "Logger.h"
#include "LogConfiguration.h"
#include "SyslogAgentSharedConstants.h"

// Use specific std types we need
using std::string;
using std::wstring;
using std::shared_lock;
using std::shared_mutex;
using std::unique_lock;
using std::lock_guard;
using std::vector;
using std::set;

namespace Syslog_agent {
    class Configuration {
    public:
        static constexpr const wchar_t* PRIMARY_CERT_FILENAME = SharedConstants::CERT_FILE_PRIMARY;
        static constexpr const wchar_t* SECONDARY_CERT_FILENAME = SharedConstants::CERT_FILE_SECONDARY;
        static constexpr int MAX_TAIL_FILE_LINE_LENGTH = 16000;

        Configuration();
        ~Configuration() = default;
        
        // Delete copy to prevent accidental sharing
        Configuration(const Configuration&) = delete;
        Configuration& operator=(const Configuration&) = delete;

        // Thread-safe configuration loading/saving
        void loadFromRegistry(bool running_from_console, bool override_log_level, 
            Logger::LogLevel override_log_level_setting);
        void saveToRegistry() const;

        // Thread-safe accessors
        bool hasSecondaryHost() const;
        const string& getHostName() const;
        void setPrimaryLogzillaVersion(const string& version);
        void setSecondaryLogzillaVersion(const string& version);
        int getPrimaryLogformat() const;
        int getSecondaryLogformat() const;
        int getPrimaryPort() const;
        int getSecondaryPort() const;

        // Thread-safe property access
        wstring getApiPath() const {
            shared_lock<shared_mutex> lock(mutex_);
            return api_path_;
        }

        wstring getVersionPath() const {
            shared_lock<shared_mutex> lock(mutex_);
            return version_path_;
        }

        bool getLookupAccounts() const {
            shared_lock<shared_mutex> lock(mutex_);
            return lookup_accounts_;
        }

        bool getForwardToSecondary() const {
            shared_lock<shared_mutex> lock(mutex_);
            return forward_to_secondary_;
        }

        bool getPrimaryUseTls() const {
            shared_lock<shared_mutex> lock(mutex_);
            return primary_use_tls_;
        }

        bool getSecondaryUseTls() const {
            shared_lock<shared_mutex> lock(mutex_);
            return secondary_use_tls_;
        }

        int getFacility() const {
            shared_lock<shared_mutex> lock(mutex_);
            return facility_;
        }

        int getSeverity() const {
            shared_lock<shared_mutex> lock(mutex_);
            return severity_;
        }

        int getBatchInterval() const {
            shared_lock<shared_mutex> lock(mutex_);
            return batch_interval_;
        }

        wstring getPrimaryHost() const {
            shared_lock<shared_mutex> lock(mutex_);
            return primary_host_;
        }

        wstring getPrimaryApiKey() const {
            shared_lock<shared_mutex> lock(mutex_);
            return primary_api_key_;
        }

        wstring getSecondaryHost() const {
            shared_lock<shared_mutex> lock(mutex_);
            return secondary_host_;
        }

        wstring getSecondaryApiKey() const {
            shared_lock<shared_mutex> lock(mutex_);
            return secondary_api_key_;
        }

        wstring getSuffix() const {
            shared_lock<shared_mutex> lock(mutex_);
            return suffix_;
        }

        vector<LogConfiguration> getLogs() const {
            shared_lock<shared_mutex> lock(mutex_);
            return logs_;
        }

        set<DWORD> getEventIdFilter() const {
            shared_lock<shared_mutex> lock(mutex_);
            return event_id_filter_;
        }

        bool getIncludeVsIgnoreEventIds() const {
            shared_lock<shared_mutex> lock(mutex_);
            return include_vs_ignore_eventids_;
        }

        bool getOnlyWhileRunning() const {
            shared_lock<shared_mutex> lock(mutex_);
            return only_while_running_;
        }

        bool getUseHTTP2() const {
            shared_lock<shared_mutex> lock(mutex_);
            return use_http2_;
        }

        bool getUseLogAgent() const {
            shared_lock<shared_mutex> lock(mutex_);
            return use_log_agent_;
        }

        void setUseLogAgent(bool value) {
            unique_lock<shared_mutex> lock(mutex_);
            use_log_agent_ = value;
        }

        bool getCertDateValidation() const {
            shared_lock<shared_mutex> lock(mutex_);
            return !SharedConstants::LENIENT_CERT_DATE_CHECK;
        }

        int getUtcOffsetMinutes() const {
            shared_lock<shared_mutex> lock(mutex_);
            return utc_offset_minutes_;
        }

        static int getDebugLevelSetting() {
            return debug_level_setting_;
        }

        static int getEventLogPollInterval() {
            return event_log_poll_interval_;
        }

        int getEventLogPollIntervalValue() const {
            shared_lock<shared_mutex> lock(mutex_);
            return event_log_poll_interval_;
        }

        wstring getTailFilename() const {
            lock_guard<shared_mutex> lock(mutex_);
            return tail_filename_;
        }

        wstring getTailProgramName() const {
            lock_guard<shared_mutex> lock(mutex_);
            return tail_program_name_;
        }

        bool getUseCompression() const {
            shared_lock<shared_mutex> lock(mutex_);
            return use_compression_;
        }

        uint32_t getMaxBatchSize() const {
            shared_lock<shared_mutex> lock(mutex_);
            return max_batch_size_;
        }

        uint32_t getMaxBatchAge() const {
            shared_lock<shared_mutex> lock(mutex_);
            return max_batch_age_;
        }

        // Protected data access for internal use
        class ScopedAccess {
        public:
            ScopedAccess(const Configuration& config) : 
                lock_(config.mutex_), config_(config) {}

            const Configuration& get() const { return config_; }

        private:
            shared_lock<shared_mutex> lock_;
            const Configuration& config_;
        };

        ScopedAccess scopedAccess() const {
            return ScopedAccess(*this);
        }

    private:
        static constexpr int MAX_COMPUTERNAME_LENGH = 200;
        void loadFilterIds(wstring value);
        void getTimeZoneOffset();
        int setLogformatForVersion(int& logformat, const string& version);

        // Configuration data
        wstring api_path_ = SharedConstants::HTTP_API_PATH;
        wstring version_path_ = SharedConstants::LOGZILLA_VERSION_PATH;
        bool lookup_accounts_ = false;
        bool forward_to_secondary_ = false;
        const bool use_ping_ = false;
        const bool use_tcp_ = true;
        const bool use_RFC3164_ = false;
        const bool use_json_message_ = true;
        bool use_log_agent_ = true;
        bool primary_use_tls_ = false;
        bool secondary_use_tls_ = false;
        bool use_http2_ = true;
        int facility_;
        int severity_;
        string host_name_;
        int batch_interval_;
        wstring primary_host_ = SharedConstants::Defaults::PRIMARY_HOST;
        wstring primary_api_key_ = L"";
        wstring secondary_host_ = SharedConstants::Defaults::SECONDARY_HOST;
        wstring secondary_api_key_ = L"";
        wstring suffix_ = SharedConstants::Defaults::SUFFIX;
        wstring debug_log_file_ = SharedConstants::Defaults::DEBUG_LOG_FILENAME;
        vector<LogConfiguration> logs_;
        set<DWORD> event_id_filter_;
        wstring tail_filename_;
        wstring tail_program_name_;
        int utc_offset_minutes_;
        bool include_vs_ignore_eventids_;
        bool only_while_running_;
        string primary_logzilla_version_ = SharedConstants::Defaults::LOGZILLA_VER;
        string secondary_logzilla_version_ = SharedConstants::Defaults::LOGZILLA_VER;
        int primary_logformat_ = SharedConstants::LOGFORMAT_DETECT;
        int secondary_logformat_ = SharedConstants::LOGFORMAT_DETECT;
        int primary_port_ = 0;  // Stores port from registry or 0 if not specified
        int secondary_port_ = 0;  // Stores secondary port from registry or 0 if not specified
		bool use_compression_ = SharedConstants::USE_COMPRESSION;
        uint32_t max_batch_size_ = SharedConstants::Defaults::MAX_BATCH_SIZE;
        uint32_t max_batch_age_ = SharedConstants::Defaults::MAX_BATCH_AGE;

        // Thread synchronization
        mutable shared_mutex mutex_;

        // Static configuration
        static int debug_level_setting_;
        static int event_log_poll_interval_;

        void setHostName();
    };
}