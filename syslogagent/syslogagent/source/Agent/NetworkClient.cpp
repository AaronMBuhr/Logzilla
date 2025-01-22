/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include "NetworkClient.h"
#include "Logger.h"
#include "Util.h"
#include "INetworkClient.h"
#include "SyslogAgentSharedConstants.h"

#include <WinSock2.h>
#include <Windows.h>
#include <winhttp.h>

#include <string>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32")

namespace Syslog_agent {

NetworkClient::NetworkClient()
    : config_(nullptr)
    , use_ssl_(false)
    , use_compression_(false)
    , hSession_(NULL)
    , hConnection_(NULL)
    , hRequest_(NULL)
    , connect_timeout_(DEFAULT_CONNECT_TIMEOUT)
    , send_timeout_(DEFAULT_SEND_TIMEOUT)
    , receive_timeout_(DEFAULT_RECEIVE_TIMEOUT)
    , port_(0)
    , is_connected_(false)
    , remaining_redirects_(MAX_REDIRECT_COUNT)
{
    api_key_[0] = L'\0';
    url_[0] = L'\0';
}

NetworkClient::~NetworkClient()
{
    close();
}

bool NetworkClient::applyTimeouts(HINTERNET handle)
{
    if (!WinHttpSetTimeouts(handle,
        connect_timeout_,
        send_timeout_,
        receive_timeout_,
        receive_timeout_))
    {
        Logger::recoverable_error("Failed to set timeouts: %u\n", GetLastError());
        return false;
    }
    return true;
}

bool NetworkClient::negotiateCompression()
{
    if (!use_compression_) {
        return true;
    }

    DWORD supportedEncodings = 0;
    DWORD bufferSize = sizeof(supportedEncodings);

    if (WinHttpQueryOption(hSession_, WINHTTP_OPTION_DECOMPRESSION, &supportedEncodings, &bufferSize)) {
        if (supportedEncodings & (WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE)) {
            return WinHttpSetOption(hRequest_, WINHTTP_OPTION_DECOMPRESSION, &supportedEncodings, sizeof(supportedEncodings)) != FALSE;
        }
    }

    use_compression_ = false;
    Logger::debug2("Compression not available\n");
    return true;
}

void NetworkClient::drainConnection()
{
    if (!hRequest_) return;

    static constexpr DWORD DRAIN_BUFFER_SIZE = 16384;  // 16KB drain buffer
    static constexpr DWORD MAX_DRAIN_TIME_MS = 5000;   // Max 5 seconds for draining
    char drainBuffer[DRAIN_BUFFER_SIZE];
    DWORD bytesRead = 0;
    DWORD totalSize = 0;
    DWORD size = 0;
    DWORD startTime = GetTickCount();

    // First query for available data with timeout protection
    DWORD lastError = 0;
    while (GetTickCount() - startTime < MAX_DRAIN_TIME_MS) {
        if (!WinHttpQueryDataAvailable(hRequest_, &size)) {
            lastError = GetLastError();
            Logger::debug2("drainConnection: WinHttpQueryDataAvailable failed with %u\n", lastError);
            return;
        }

        if (size == 0) break;  // No more data

        // Read the data chunk
        while (size > 0 && (GetTickCount() - startTime < MAX_DRAIN_TIME_MS)) {
            DWORD chunkSize = (std::min)(size, DRAIN_BUFFER_SIZE);
            bytesRead = 0;

            if (!WinHttpReadData(hRequest_, drainBuffer, chunkSize, &bytesRead)) {
                lastError = GetLastError();
                Logger::debug2("drainConnection: WinHttpReadData failed with %u\n", lastError);
                return;
            }

            if (bytesRead == 0) break;  // End of data
            
            if (bytesRead > size) {
                // Protect against underflow and corrupted size reporting
                Logger::debug2("drainConnection: Read more bytes than reported available\n");
                return;
            }

            size -= bytesRead;
            totalSize += bytesRead;
        }
    }

    if (GetTickCount() - startTime >= MAX_DRAIN_TIME_MS) {
        Logger::debug2("drainConnection: Timed out after draining %u bytes\n", totalSize);
    } else {
        Logger::debug2("drainConnection: Successfully drained %u bytes\n", totalSize);
    }
}

bool NetworkClient::checkServerCert()
{
    if (!hRequest_) {
        Logger::critical("Cannot check server cert: no active request\n");
        return false;
    }

    // We need to query current flags first
    DWORD securityFlags = 0;
    DWORD securityFlagsSize = sizeof(securityFlags);
    if (!WinHttpQueryOption(hRequest_,
        WINHTTP_OPTION_SECURITY_FLAGS,
        &securityFlags,
        &securityFlagsSize))
    {
        Logger::recoverable_error("Failed to query security flags: %u\n", GetLastError());
        return false;
    }

    // Set the certificate validation flags
    securityFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID;

    if (!WinHttpSetOption(hRequest_,
        WINHTTP_OPTION_SECURITY_FLAGS,
        &securityFlags,
        sizeof(securityFlags)))
    {
        Logger::recoverable_error("Failed to set security flags: %u\n", GetLastError());
        return false;
    }

    // Log the final security state for debugging
    Logger::debug2("Certificate validation state:\n");
    if (securityFlags & SECURITY_FLAG_IGNORE_UNKNOWN_CA) {
        Logger::debug2("- Ignoring unknown CA\n");
    }
    if (securityFlags & SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE) {
        Logger::debug2("- Ignoring incorrect certificate usage\n");
    }
    if (securityFlags & SECURITY_FLAG_IGNORE_CERT_CN_INVALID) {
        Logger::debug2("- Ignoring invalid CN\n");
    }
    if (securityFlags & SECURITY_FLAG_IGNORE_CERT_DATE_INVALID) {
        Logger::debug2("- Ignoring invalid certificate date\n");
    }

    return true;
}

bool NetworkClient::followRedirect(wchar_t* redirect_buffer, size_t buffer_size)
{
    if (remaining_redirects_ <= 0) {
        Logger::recoverable_error("Too many redirects\n");
        return false;
    }

    DWORD size = 0;
    WinHttpQueryHeaders(hRequest_, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &size, WINHTTP_NO_HEADER_INDEX);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        Logger::recoverable_error("Failed to get redirect location\n");
        return false;
    }

    if (size > buffer_size) {
        Logger::recoverable_error("Redirect URL too long\n");
        return false;
    }

    if (!WinHttpQueryHeaders(hRequest_, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, redirect_buffer, &size, WINHTTP_NO_HEADER_INDEX)) {
        Logger::recoverable_error("Failed to get redirect URL\n");
        return false;
    }

    cleanup_request();

    if (wcsncmp(redirect_buffer, L"http://", 7) == 0 || wcsncmp(redirect_buffer, L"https://", 8) == 0) {
        use_ssl_ = (wcsncmp(redirect_buffer, L"https://", 8) == 0);
        if (!initialize(config_, api_key_, redirect_buffer, use_ssl_)) {
            return false;
        }
    }

    remaining_redirects_--;
    return true;
}

bool NetworkClient::initialize(const Configuration* config, const wchar_t* api_key,
    const wchar_t* url, bool use_ssl, unsigned int port)
{
    config_ = config;
    use_ssl_ = use_ssl;

    if (!api_key || wcslen(api_key) >= MAX_API_KEY_LENGTH) {
        Logger::fatal("API key empty or too long\n");
        return false;
    }
    wcsncpy_s(api_key_, api_key, MAX_API_KEY_LENGTH);

    const wchar_t* parsed_url = url;
    if (wcsncmp(parsed_url, L"https://", 8) == 0) {
        parsed_url += 8;
        use_ssl_ = true;
    }
    else if (wcsncmp(parsed_url, L"http://", 7) == 0) {
        parsed_url += 7;
        use_ssl_ = false;
    }

    const wchar_t* port_pos = wcschr(parsed_url, L':');
    const wchar_t* path_pos = wcschr(parsed_url, L'/');

    size_t host_len = (port_pos && (!path_pos || port_pos < path_pos)) ? (port_pos - parsed_url) :
        (path_pos ? (path_pos - parsed_url) : wcslen(parsed_url));

    if (wcslen(url) >= MAX_URL_LENGTH) {
        Logger::critical("URL too long\n");
        return false;
    }
    wcsncpy_s(url_, parsed_url, host_len);

    if (port != 0) {
        port_ = port;
    }
    else if (port_pos && (!path_pos || port_pos < path_pos)) {
        port_ = _wtoi(port_pos + 1);
        if (port_ == 0 || port_ > 65535) {
            Logger::critical("Invalid port number\n");
            return false;
        }
    }
    else {
        port_ = use_ssl_ ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    }

    hSession_ = WinHttpOpen(L"SyslogAgent",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession_) {
        Logger::recoverable_error("Error %u in WinHttpOpen\n", GetLastError());
        return false;
    }

    if (!applyTimeouts(hSession_)) {
        close();
        return false;
    }

    if (config_->getUseHTTP2()) {
        DWORD dwOption = WINHTTP_PROTOCOL_FLAG_HTTP2;
        if (!WinHttpSetOption(hSession_, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &dwOption, sizeof(dwOption))) {
            Logger::warn("Failed to enable HTTP/2, falling back to HTTP/1.1: %u\n", GetLastError());
        }
    }

    return true;
}

bool NetworkClient::connect()
{
    if (is_connected_) {
        return true;
    }

    hConnection_ = WinHttpConnect(hSession_, url_, port_, 0);
    if (!hConnection_) {
        Logger::recoverable_error("Error %u in WinHttpConnect\n", GetLastError());
        return false;
    }

    is_connected_ = true;
    return true;
}

NetworkClient::RESULT_TYPE NetworkClient::post(const char* buf, uint32_t length)
{
    if (!is_connected_ || !hConnection_) {
        return ERROR_NOT_CONNECTED;
    }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (use_ssl_) {
        flags |= WINHTTP_FLAG_SECURE;
    }

    hRequest_ = WinHttpOpenRequest(hConnection_,
        L"POST",
        L"/",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest_) {
        Logger::recoverable_error("Error %u in WinHttpOpenRequest\n", GetLastError());
        return GetLastError();
    }

    if (!checkServerCert()) {
        cleanup_request();
        return ERROR_INVALID_CERTIFICATE;
    }

    if (!negotiateCompression()) {
        Logger::warn("Failed to negotiate compression\n");
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    headers += L"X-API-KEY: ";
    headers += api_key_;
    headers += L"\r\n";

    if (!WinHttpSendRequest(hRequest_,
        headers.c_str(),
        -1,
        (LPVOID)buf,
        length,
        length,
        0))
    {
        DWORD error = GetLastError();
        Logger::recoverable_error("Error %u in WinHttpSendRequest\n", error);
        cleanup_request();
        return error;
    }

    if (!WinHttpReceiveResponse(hRequest_, NULL)) {
        DWORD error = GetLastError();
        Logger::recoverable_error("Error %u in WinHttpReceiveResponse\n", error);
        cleanup_request();
        return error;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);

    if (!WinHttpQueryHeaders(hRequest_,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL,
        &statusCode,
        &statusCodeSize,
        NULL))
    {
        DWORD error = GetLastError();
        Logger::recoverable_error("Error %u in WinHttpQueryHeaders\n", error);
        cleanup_request();
        return error;
    }

    // Handle redirects
    if (statusCode >= 300 && statusCode < 400) {
        wchar_t redirectUrl[MAX_URL_LENGTH];
        if (followRedirect(redirectUrl, MAX_URL_LENGTH)) {
            return post(buf, length);
        }
        return ERROR_WINHTTP_REDIRECT_FAILED;
    }

    drainConnection();
    cleanup_request();

    return statusCode == 200 ? RESULT_SUCCESS : ERROR_INVALID_DATA;
}

void NetworkClient::close()
{
    cleanup_request();
    if (hConnection_) {
        WinHttpCloseHandle(hConnection_);
        hConnection_ = NULL;
    }
    if (hSession_) {
        WinHttpCloseHandle(hSession_);
        hSession_ = NULL;
    }
    is_connected_ = false;
}

void NetworkClient::cleanup_request()
{
    if (hRequest_) {
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
    }
}

bool NetworkClient::getLogzillaVersion(char* version_buf, size_t max_length, size_t& bytes_written)
{
    // Not implemented for base NetworkClient
    return false;
}

SOCKET NetworkClient::getSocket()
{
    // Not used for WinHTTP-based clients
    return NULL;
}

} // namespace Syslog_agent