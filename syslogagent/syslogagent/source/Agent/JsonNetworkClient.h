#pragma once

// Windows includes
#include <WinSock2.h>
#include <Windows.h>

// Standard library includes
#include <string>
#include <mutex>
#include <utility>
#include <cstdint>

// Project includes
#include "INetworkClient.h"
#include "WindowsEvent.h"
#include "Configuration.h"

namespace Syslog_agent {

class JsonNetworkClient : public INetworkClient
{
public:
    typedef DWORD RESULT_TYPE;
    static constexpr RESULT_TYPE RESULT_SUCCESS = ERROR_SUCCESS;
    static constexpr DWORD DEFAULT_CONNECT_TIMEOUT = 30000;   // 30 seconds
    static constexpr DWORD DEFAULT_SEND_TIMEOUT = 30000;      // 30 seconds
    static constexpr DWORD DEFAULT_RECEIVE_TIMEOUT = 30000;   // 30 seconds

    JsonNetworkClient(
        const std::wstring& remote_host_address,
        unsigned int remote_port = 515  // Default to standard JSON port
    );

    virtual ~JsonNetworkClient();

    // Override the virtual methods from INetworkClient
    virtual bool initialize(const Configuration* config, const wchar_t* api_key,
        const wchar_t* url, bool use_ssl, unsigned int port = 0) override;
    virtual bool connect() override;
    virtual RESULT_TYPE post(const char* buf, uint32_t length) override;
    virtual void close() override;
    virtual bool getLogzillaVersion(char* version_buf, size_t max_length, size_t& bytes_written) override;
    virtual SOCKET getSocket() override { return socket_; }

    std::wstring connectionName() const;
    std::string connectionNameUtf8() const;
    bool isConnected() const { return is_connected_; }

protected:
    std::wstring remote_host_address_;
    unsigned int remote_port_;
    volatile bool is_connected_;
    SOCKET socket_;
    std::recursive_mutex connecting_;
    DWORD connect_timeout_;
    DWORD send_timeout_;
    DWORD receive_timeout_;
};

} // namespace Syslog_agent
