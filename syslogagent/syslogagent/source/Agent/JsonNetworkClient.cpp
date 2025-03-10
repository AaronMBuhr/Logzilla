#include "stdafx.h"
#include "Logger.h"
#include "JsonNetworkClient.h"
#include "Configuration.h"

#include <WinSock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <WinError.h>

#include <sstream>
#include <vector>

using namespace std;
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

JsonNetworkClient::JsonNetworkClient(const std::wstring& hostname, unsigned int port)
    : remote_host_address_(hostname)
    , remote_port_(port)
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

bool JsonNetworkClient::connect()
{
    if (is_connected_) {
        return true;
    }

    auto logger = LOG_THIS;
    std::lock_guard<std::recursive_mutex> lock(connecting_);
    
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        logger->recoverable_error("JsonNetworkClient::connect() WSAStartup failed: %d\n", result);
        return false;
    }

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        logger->recoverable_error("JsonNetworkClient::connect() socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return false;
    }

    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char*)&receive_timeout_, sizeof(receive_timeout_)) == SOCKET_ERROR) {
        logger->warning("JsonNetworkClient::connect() failed to set receive timeout: %d\n", WSAGetLastError());
    }
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (char*)&send_timeout_, sizeof(send_timeout_)) == SOCKET_ERROR) {
        logger->warning("JsonNetworkClient::connect() failed to set send timeout: %d\n", WSAGetLastError());
    }

    char host_utf8[256];
    if (!ws2s(remote_host_address_.c_str(), host_utf8, sizeof(host_utf8))) {
        logger->recoverable_error("JsonNetworkClient::connect() failed to convert host address\n");
        closesocket(socket_);
        WSACleanup();
        return false;
    }

    // Try direct IP connection first
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(remote_port_);
    
    INT ip_result = InetPtonA(AF_INET, host_utf8, &addr.sin_addr);
    if (ip_result == 1) {
        logger->info("JsonNetworkClient::connect() attempting direct IP connection to: %s\n", host_utf8);
        int connect_result = ::connect(socket_, (struct sockaddr*)&addr, sizeof(addr));
        if (connect_result != SOCKET_ERROR) {
            is_connected_ = true;
            return true;
        }
        logger->warning("JsonNetworkClient::connect() direct connection failed: %d\n", WSAGetLastError());
    }

    // If direct connection failed or hostname was not an IP, fall back to DNS
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    logger->info("JsonNetworkClient::connect() falling back to DNS resolution for host: %s\n", host_utf8);
    struct addrinfo* result_addr;
    int dns_result = ::getaddrinfo(host_utf8, std::to_string(remote_port_).c_str(), &hints, &result_addr);
    if (dns_result != 0) {
        logger->recoverable_error("JsonNetworkClient::connect() all connection attempts failed for host: %s\n", host_utf8);
        closesocket(socket_);
        WSACleanup();
        return false;
    }

    bool connected = false;
    struct addrinfo* ptr = result_addr;
    while (ptr != nullptr && !connected) {
        int connect_result = ::connect(socket_, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (connect_result != SOCKET_ERROR) {
            connected = true;
            break;
        }
        ptr = ptr->ai_next;
    }
    
    ::freeaddrinfo(result_addr);

    if (!connected) {
        logger->recoverable_error("JsonNetworkClient::connect() connect failed: %d\n", WSAGetLastError());
        closesocket(socket_);
        WSACleanup();
        return false;
    }

    is_connected_ = true;
    return true;
}

INetworkClient::RESULT_TYPE JsonNetworkClient::post(const char* buf, uint32_t length)
{
    auto logger = LOG_THIS;
    if (!is_connected_) {
        logger->recoverable_error("JsonNetworkClient::post() not connected\n");
        return RESULT_TYPE(ERROR_NETWORK_UNREACHABLE, "Failed: not connected to server\n(no response)");
    }

    if (socket_ == INVALID_SOCKET) {
        return RESULT_TYPE(ERROR_NETWORK_UNREACHABLE, "Failed: invalid socket\n(no response)");
    }

    int bytes_sent = ::send(socket_, buf, length, 0);
    if (bytes_sent == SOCKET_ERROR) {
        DWORD error = WSAGetLastError();
        char msg[1024];
        snprintf(msg, sizeof(msg), "Failed: send error WSA %lu\n(no response)", error);
        logger->recoverable_error("JsonNetworkClient::post() send failed: %d\n", error);
        return RESULT_TYPE(ERROR_NETWORK_UNREACHABLE, msg);
    }

    if (static_cast<uint32_t>(bytes_sent) != length) {
        char msg[1024];
        snprintf(msg, sizeof(msg), "Failed: incomplete send - %d of %u bytes sent\n(no response)", bytes_sent, length);
        return RESULT_TYPE(ERROR_NETWORK_UNREACHABLE, msg);
    }

    // Try to read response
    char response[1024] = { 0 };
    int bytes_received = ::recv(socket_, response, sizeof(response) - 1, 0);
    
    char msg[2048];  // Large enough for status + response
    if (bytes_received == SOCKET_ERROR) {
        DWORD error = WSAGetLastError();
        snprintf(msg, sizeof(msg), "Success: data sent but receive failed with WSA %lu\n(no response)", error);
    } else if (bytes_received == 0) {
        snprintf(msg, sizeof(msg), "Success\n(no response)");
    } else {
        response[bytes_received] = '\0';  // Ensure null termination
        snprintf(msg, sizeof(msg), "Success\n%s", response);
    }

    return RESULT_TYPE(ERROR_SUCCESS, msg);
}

void JsonNetworkClient::close()
{
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    WSACleanup();
    is_connected_ = false;
}

bool JsonNetworkClient::getLogzillaVersion(char* version_buf, size_t max_length, size_t& bytes_written)
{
    auto logger = LOG_THIS;
    // Not implemented for JSON client
    logger->debug2("JsonNetworkClient::getLogzillaVersion() not implemented for JSON client\n");
    bytes_written = 0;
    return false;
}

std::string JsonNetworkClient::connectionNameUtf8()
{
    auto logger = LOG_THIS;
    char buffer[256];
    if (ws2s(remote_host_address_.c_str(), buffer, sizeof(buffer))) {
        return std::string(buffer);
    }
    logger->warning("JsonNetworkClient::connectionNameUtf8() failed to convert host address\n");
    return "";
}
