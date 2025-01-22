#pragma once

#include <Windows.h>
#include "Configuration.h"

namespace Syslog_agent {

class INetworkClient {
public:
    typedef DWORD RESULT_TYPE;
    static constexpr RESULT_TYPE RESULT_SUCCESS = ERROR_SUCCESS;

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
