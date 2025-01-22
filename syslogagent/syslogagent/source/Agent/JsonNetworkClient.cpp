#include "stdafx.h"
#include "JsonNetworkClient.h"
#include "Logger.h"
#include "Configuration.h"

#include <WinSock2.h>
#include <Windows.h>
#include <ws2tcpip.h>

#include <sstream>

using namespace Syslog_agent;

namespace {
    // Helper function to convert wide string to UTF-8 using fixed buffer
    bool ws2s(const wchar_t* wstr, char* buffer, size_t bufferSize) {
        if (!wstr || !buffer || bufferSize == 0) return false;
        
        int result = WideCharToMultiByte(
            CP_UTF8,
            0,
            wstr,
            -1,
            buffer,
            static_cast<int>(bufferSize),
            nullptr,
            nullptr
        );
        
        return result > 0;
    }
}

JsonNetworkClient::JsonNetworkClient(const std::wstring& remote_host_address, unsigned int remote_port)
    : remote_host_address_(remote_host_address)
    , remote_port_(remote_port)
    , is_connected_(false)
    , socket_(INVALID_SOCKET)
    , connect_timeout_(DEFAULT_CONNECT_TIMEOUT)
    , send_timeout_(DEFAULT_SEND_TIMEOUT)
    , receive_timeout_(DEFAULT_RECEIVE_TIMEOUT)
{
}

JsonNetworkClient::~JsonNetworkClient()
{
    close();
}

bool JsonNetworkClient::initialize(const Configuration* config, const wchar_t* api_key,
    const wchar_t* url, bool use_ssl, unsigned int port)
{
    if (port != 0) {
        remote_port_ = port;
    }
    return true;
}

bool JsonNetworkClient::connect()
{
    if (is_connected_) {
        return true;
    }

    std::lock_guard<std::recursive_mutex> lock(connecting_);
    
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        Logger::recoverable_error("JsonNetworkClient::connect() WSAStartup failed: %d\n", result);
        return false;
    }

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        Logger::recoverable_error("JsonNetworkClient::connect() socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return false;
    }

    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char*)&receive_timeout_, sizeof(receive_timeout_)) == SOCKET_ERROR) {
        Logger::warn("JsonNetworkClient::connect() failed to set receive timeout: %d\n", WSAGetLastError());
    }
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (char*)&send_timeout_, sizeof(send_timeout_)) == SOCKET_ERROR) {
        Logger::warn("JsonNetworkClient::connect() failed to set send timeout: %d\n", WSAGetLastError());
    }

    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result_addr = nullptr;
    char host_utf8[256];
    if (!ws2s(remote_host_address_.c_str(), host_utf8, sizeof(host_utf8))) {
        Logger::recoverable_error("JsonNetworkClient::connect() failed to convert host address\n");
        closesocket(socket_);
        WSACleanup();
        return false;
    }
    
    char port_str[32];
    snprintf(port_str, sizeof(port_str), "%u", remote_port_);
    
    result = ::getaddrinfo(host_utf8, port_str, &hints, &result_addr);
    if (result != 0) {
        Logger::recoverable_error("JsonNetworkClient::connect() getaddrinfo failed: %d\n", result);
        closesocket(socket_);
        WSACleanup();
        return false;
    }

    result = ::connect(socket_, result_addr->ai_addr, (int)result_addr->ai_addrlen);
    ::freeaddrinfo(result_addr);

    if (result == SOCKET_ERROR) {
        Logger::recoverable_error("JsonNetworkClient::connect() connect failed: %d\n", WSAGetLastError());
        closesocket(socket_);
        WSACleanup();
        return false;
    }

    is_connected_ = true;
    return true;
}

JsonNetworkClient::RESULT_TYPE JsonNetworkClient::post(const char* buf, uint32_t length)
{
    if (!is_connected_) {
        return ERROR_NOT_CONNECTED;
    }

    int bytes_sent = ::send(socket_, buf, length, 0);
    if (bytes_sent == SOCKET_ERROR) {
        Logger::recoverable_error("JsonNetworkClient::post() send failed: %d\n", WSAGetLastError());
        return WSAGetLastError();
    }

    return RESULT_SUCCESS;
}

void JsonNetworkClient::close()
{
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        WSACleanup();
    }
    is_connected_ = false;
}

bool JsonNetworkClient::getLogzillaVersion(char* version_buf, size_t max_length, size_t& bytes_written)
{
    // Not implemented for JSON protocol
    return false;
}

std::wstring JsonNetworkClient::connectionName() const
{
    return remote_host_address_;
}

std::string JsonNetworkClient::connectionNameUtf8() const
{
    char host_utf8[256];
    if (!ws2s(remote_host_address_.c_str(), host_utf8, sizeof(host_utf8))) {
        Logger::recoverable_error("JsonNetworkClient::connectionNameUtf8() failed to convert host address\n");
        return "";
    }
    return host_utf8;
}
