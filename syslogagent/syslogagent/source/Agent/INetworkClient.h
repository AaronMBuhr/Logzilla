#pragma once

#include <Windows.h>
#include "Configuration.h"
#include <cstring>

namespace Syslog_agent {

class NetworkResult {
public:
    static constexpr size_t MAX_MESSAGE_LENGTH = 1024;

    // Constructor for success
    NetworkResult() : code_(ERROR_SUCCESS) {
        message_[0] = '\0';
    }

    // Constructor for error with code and optional message
    NetworkResult(DWORD error_code, const char* message = nullptr) : code_(error_code) {
        if (message) {
            strncpy_s(message_, message, MAX_MESSAGE_LENGTH - 1);
        } else {
            message_[0] = '\0';
        }
    }

    // Implicit conversion to DWORD for easy numeric comparisons
    operator DWORD() const { return code_; }

    // Equality operators to maintain existing comparison behavior
    bool operator==(DWORD other) const { return code_ == other; }
    bool operator!=(DWORD other) const { return code_ != other; }

    // Getters
    DWORD getCode() const { return code_; }
    const char* getMessage() const { return message_; }
    bool hasMessage() const { return message_[0] != '\0'; }

private:
    DWORD code_;
    char message_[MAX_MESSAGE_LENGTH];
};

class INetworkClient {
public:
    using RESULT_TYPE = NetworkResult;
    static const RESULT_TYPE RESULT_SUCCESS; // Defined in cpp file

    virtual ~INetworkClient() = default;

    virtual bool initialize(const Configuration* config, const wchar_t* api_key,
        const wchar_t* url, bool use_ssl, unsigned int port = 0) = 0;
    virtual bool connect() = 0;
    virtual RESULT_TYPE post(const char* buf, uint32_t length) = 0;
    virtual void close() = 0;
    virtual bool getLogzillaVersion(char* version_buf, size_t max_length, size_t& bytes_written) = 0;
    virtual SOCKET getSocket() = 0;

protected:
    INetworkClient() = default;
};

} // namespace Syslog_agent
