#include "stdafx.h"
#include "HttpNetworkClient.h"
#include "Logger.h"
#include "Configuration.h"

#include <WinSock2.h>
#include <Windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <string>
#include <sstream>
#include <locale>
#include <codecvt>
#include <mutex>

using namespace Syslog_agent;

namespace {
    // Convert wide string to UTF-8 using fixed buffer
    bool ws2s(const wchar_t* wstr, char* buffer, size_t bufferSize) {
        if (!wstr || !buffer || bufferSize == 0) return false;
        
        int result = WideCharToMultiByte(
            CP_UTF8,
            0,
            wstr,
            -1,
            buffer,
            static_cast<int>(bufferSize),
            NULL,
            NULL
        );
        
        return result > 0;
    }

    // Convert UTF-8 to wide string using fixed buffer
    bool s2ws(const char* str, wchar_t* buffer, size_t bufferSize) {
        if (!str || !buffer || bufferSize == 0) return false;
        
        int result = MultiByteToWideChar(
            CP_UTF8,
            0,
            str,
            -1,
            buffer,
            static_cast<int>(bufferSize)
        );
        
        return result > 0;
    }
}

HttpNetworkClient::HttpNetworkClient()
    : use_ssl_(false)
    , use_compression_(false)
    , hSession_(NULL)
    , hConnection_(NULL)
    , hRequest_(NULL)
    , connect_timeout_(DEFAULT_CONNECT_TIMEOUT)
    , send_timeout_(DEFAULT_SEND_TIMEOUT)
    , receive_timeout_(DEFAULT_RECEIVE_TIMEOUT)
    , port_(0)
    , is_connected_(false)
{
    // Initialize string buffers
    url_[0] = L'\0';
    host_[0] = L'\0';
    path_[0] = L'\0';
    api_key_[0] = L'\0';
}

HttpNetworkClient::~HttpNetworkClient()
{
    close();
}

bool HttpNetworkClient::initialize(const Configuration* config, const wchar_t* api_key,
    const wchar_t* url, bool use_ssl, unsigned int port)
{
    std::lock_guard<std::recursive_mutex> lock(connecting_);

    if (!config || !api_key || !url) {
        Logger::recoverable_error("HttpNetworkClient::initialize() invalid parameters\n");
        return false;
    }

    // Use fixed-size buffers for URL and API key
    wchar_t url_buffer[MAX_URL_LENGTH] = {};  // Zero-initialize
    wchar_t api_key_buffer[MAX_API_KEY_LENGTH] = {};  // Zero-initialize
    
    wcsncpy_s(url_buffer, url, MAX_URL_LENGTH - 1);
    url_buffer[MAX_URL_LENGTH - 1] = L'\0';  // Ensure null termination
    
    wcsncpy_s(api_key_buffer, api_key, MAX_API_KEY_LENGTH - 1);
    api_key_buffer[MAX_API_KEY_LENGTH - 1] = L'\0';  // Ensure null termination
    
    use_ssl_ = use_ssl;
    port_ = port;

    // Parse URL into host and path using fixed buffers
    wchar_t host_buffer[MAX_URL_LENGTH] = {};  // Zero-initialize
    wchar_t path_buffer[MAX_PATH_LENGTH] = {};  // Zero-initialize
    
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.lpszHostName = host_buffer;
    urlComp.dwHostNameLength = MAX_URL_LENGTH;
    urlComp.lpszUrlPath = path_buffer;
    urlComp.dwUrlPathLength = MAX_PATH_LENGTH;

    if (!WinHttpCrackUrl(url_buffer, 0, 0, &urlComp)) {
        Logger::recoverable_error("HttpNetworkClient::initialize() failed to parse URL: %d\n", GetLastError());
        return false;
    }

    if (port_ < 1) {
        port_ = urlComp.nPort;
        if (port_ == 0) {  // If no port in URL, use default based on scheme
            port_ = use_ssl_ ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        }
    }

    // Store the parsed components with explicit null termination
    wcsncpy_s(url_, url_buffer, _countof(url_) - 1);
    url_[_countof(url_) - 1] = L'\0';
    
    wcsncpy_s(host_, host_buffer, _countof(host_) - 1);
    host_[_countof(host_) - 1] = L'\0';
    
    wcsncpy_s(path_, path_buffer, _countof(path_) - 1);
    path_[_countof(path_) - 1] = L'\0';
    
    wcsncpy_s(api_key_, api_key_buffer, _countof(api_key_) - 1);
    api_key_[_countof(api_key_) - 1] = L'\0';

    return true;
}

bool HttpNetworkClient::connect()
{
    std::lock_guard<std::recursive_mutex> lock(connecting_);

    if (is_connected_) {
        return true;
    }

    hSession_ = WinHttpOpen(L"SyslogAgent/1.0", 
        WINHTTP_ACCESS_TYPE_NO_PROXY,  
        WINHTTP_NO_PROXY_NAME, 
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession_) {
        Logger::recoverable_error("HttpNetworkClient::connect() WinHttpOpen failed: %d\n", GetLastError());
        return false;
    }

    // Set timeouts
    if (!WinHttpSetTimeouts(hSession_, 
        0,                  // DNS resolution timeout
        connect_timeout_,   // Connect timeout
        send_timeout_,      // Send timeout
        receive_timeout_))  // Receive timeout
    {
        Logger::warning("HttpNetworkClient::connect() failed to set timeouts: %d\n", GetLastError());
    }

    Logger::debug2("HttpNetworkClient::connect() connecting to %ls:%d\n", host_, port_);
    INTERNET_PORT port = port_ ? (INTERNET_PORT)port_ : (use_ssl_ ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
    hConnection_ = WinHttpConnect(hSession_, host_, port, 0);
    if (!hConnection_) {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::connect() WinHttpConnect failed: %d\n", error);
        close();
        return false;
    }

    is_connected_ = true;
    return true;
}

HttpNetworkClient::RESULT_TYPE HttpNetworkClient::post(const char* buf, uint32_t length)
{
    std::lock_guard<std::recursive_mutex> lock(connecting_);

    if (!is_connected_ || !hConnection_) {
        Logger::debug2("HttpNetworkClient::post() Not connected, connection handle: %p, is_connected: %d\n", 
            hConnection_, is_connected_);
        return NetworkResult(ERROR_NOT_CONNECTED, "Not connected to server (http 0)");
    }

    Logger::debug2("HttpNetworkClient::post() Starting post operation - Length: %d bytes\n", length);
    
    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (use_ssl_) {
        flags |= WINHTTP_FLAG_SECURE;
        Logger::debug2("HttpNetworkClient::post() Using SSL\n");
    }

    // Create request handle
    hRequest_ = WinHttpOpenRequest(hConnection_,
        L"POST",
        path_,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest_) {
        DWORD error = GetLastError();
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to open HTTP request: error %lu (http 0)", error);
        return NetworkResult(error, msg);
    }

    // Set timeouts for this request
    if (!applyTimeouts(hRequest_)) {
        Logger::warning("HttpNetworkClient::post() Failed to set request timeouts\n");
    }

    // Add headers
    std::wstring headers = L"Content-Type: application/json\r\n";
    headers += L"Authorization: token ";
    headers += api_key_;
    headers += L"\r\n";

    if (use_compression_) {
        headers += L"Accept-Encoding: gzip, deflate\r\n";
    }

    if (!WinHttpAddRequestHeaders(hRequest_,
        headers.c_str(),
        -1L,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
    {
        DWORD error = GetLastError();
        cleanup_request();
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to add request headers: error %lu (http 0)", error);
        return NetworkResult(error, msg);
    }

    // Send the request
    if (!WinHttpSendRequest(hRequest_,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        (LPVOID)buf,
        length,
        length,
        0))
    {
        DWORD error = GetLastError();
        cleanup_request();
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to send request: error %lu (http 0)", error);
        return NetworkResult(error, msg);
    }

    // End the request
    if (!WinHttpReceiveResponse(hRequest_, NULL)) {
        DWORD error = GetLastError();
        cleanup_request();
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to receive response: error %lu (http 0)", error);
        return NetworkResult(error, msg);
    }

    // Check status code
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    if (!WinHttpQueryHeaders(hRequest_,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &size,
        WINHTTP_NO_HEADER_INDEX))
    {
        DWORD error = GetLastError();
        cleanup_request();
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to query status code: error %lu (http 0)", error);
        return NetworkResult(error, msg);
    }

    // Read response body
    char response_buffer[1024] = { 0 };
    size_t total_read = 0;
    DWORD bytes_available = 0;
    DWORD bytes_read = 0;
    
    while (WinHttpQueryDataAvailable(hRequest_, &bytes_available)) {
        if (bytes_available == 0) break;

        // Ensure we don't overflow our buffer
        if (total_read >= sizeof(response_buffer) - 1) {
            Logger::warning("HttpNetworkClient::post() Response exceeds buffer size, truncating\n");
            break;
        }

        // Calculate remaining buffer space
        size_t remaining = sizeof(response_buffer) - total_read - 1;  // -1 for null terminator
        DWORD to_read = (bytes_available > remaining) ? static_cast<DWORD>(remaining) : bytes_available;

        if (!WinHttpReadData(hRequest_, 
            response_buffer + total_read,
            to_read,
            &bytes_read) || bytes_read == 0)
        {
            break;
        }

        total_read += bytes_read;
    }
    response_buffer[total_read] = '\0';  // Ensure null termination

    cleanup_request();

    // Format the result message with both status and response body
    char msg[1024 + 256];  // Large enough for status line + response
    if (status_code != 200 && status_code != 201 && status_code != 202) {
        snprintf(msg, sizeof(msg), "Server returned error (http %lu)\n%s", 
            status_code, 
            total_read > 0 ? response_buffer : "No response body");
        return NetworkResult(ERROR_BAD_ARGUMENTS, msg);
    }

    snprintf(msg, sizeof(msg), "Send succeeded (http %lu)\n%s", 
        status_code,
        total_read > 0 ? response_buffer : "No response body");
    return NetworkResult(ERROR_SUCCESS, msg);
}

void HttpNetworkClient::cleanup_request() {
    if (hRequest_) {
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
    }
}

void HttpNetworkClient::close()
{
    std::lock_guard<std::recursive_mutex> lock(connecting_);

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

bool HttpNetworkClient::getLogzillaVersion(char* version_buf, size_t max_length, size_t& bytes_written)
{
    std::lock_guard<std::recursive_mutex> lock(connecting_);

    // Always attempt to connect if not connected
    if (!is_connected_ || !hConnection_) {
        Logger::debug2("HttpNetworkClient::getLogzillaVersion() not connected, attempting to connect\n");
        if (!connect()) {
            Logger::recoverable_error("HttpNetworkClient::getLogzillaVersion() connection attempt failed\n");
            return false;
        }
    }

    // Build version URL
    wchar_t version_url[MAX_URL_LENGTH];
    wcscpy_s(version_url, SharedConstants::LOGZILLA_VERSION_PATH);

    Logger::debug2("HttpNetworkClient::getLogzillaVersion() requesting URL: %ls\n", version_url);

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (use_ssl_) {
        flags |= WINHTTP_FLAG_SECURE;
    }

    hRequest_ = WinHttpOpenRequest(hConnection_,
        L"GET",
        version_url,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest_) {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::getLogzillaVersion() WinHttpOpenRequest failed: %d\n", error);
        return false;
    }

    // Set timeouts on the request handle
    if (!applyTimeouts(hRequest_)) {
        Logger::warning("HttpNetworkClient::getLogzillaVersion() failed to set request timeouts\n");
    }

    // Send request without API key header since version endpoint doesn't require auth
    Logger::debug2("HttpNetworkClient::getLogzillaVersion() sending request\n");
    if (!WinHttpSendRequest(hRequest_,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0))
    {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::getLogzillaVersion() WinHttpSendRequest failed: %d\n", error);
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    Logger::debug2("HttpNetworkClient::getLogzillaVersion() receiving response\n");
    if (!WinHttpReceiveResponse(hRequest_, NULL)) {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::getLogzillaVersion() WinHttpReceiveResponse failed: %d\n", error);
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    DWORD size = 0;
    if (!WinHttpQueryDataAvailable(hRequest_, &size)) {
        Logger::recoverable_error("HttpNetworkClient::getLogzillaVersion() WinHttpQueryDataAvailable failed: %d\n", GetLastError());
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    if (size >= max_length) {
        Logger::recoverable_error("HttpNetworkClient::getLogzillaVersion() response too large\n");
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    DWORD downloaded = 0;
    if (!WinHttpReadData(hRequest_,
        version_buf,
        size,
        &downloaded))
    {
        Logger::recoverable_error("HttpNetworkClient::getLogzillaVersion() WinHttpReadData failed: %d\n", GetLastError());
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    WinHttpCloseHandle(hRequest_);
    hRequest_ = NULL;

    bytes_written = downloaded;
    version_buf[downloaded] = '\0';
    return true;
}

bool HttpNetworkClient::loadCertificate(const wchar_t* cert_path)
{
    std::lock_guard<std::recursive_mutex> lock(connecting_);

    if (!is_connected_ || !hRequest_) {
        Logger::recoverable_error("HttpNetworkClient::loadCertificate() not connected or no request\n");
        return false;
    }

    DWORD securityFlags = 0;
    DWORD securityFlagsSize = sizeof(securityFlags);
    
    // Get the current security flags
    if (!WinHttpQueryOption(hRequest_,
        WINHTTP_OPTION_SECURITY_FLAGS,
        &securityFlags,
        &securityFlagsSize))
    {
        Logger::recoverable_error("HttpNetworkClient::loadCertificate() WinHttpQueryOption failed: %d\n", GetLastError());
        return false;
    }

    // Add the certificate flags
    securityFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                    SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                    SECURITY_FLAG_IGNORE_CERT_CN_INVALID;

    // Set the updated security flags
    if (!WinHttpSetOption(hRequest_,
        WINHTTP_OPTION_SECURITY_FLAGS,
        &securityFlags,
        sizeof(securityFlags)))
    {
        Logger::recoverable_error("HttpNetworkClient::loadCertificate() WinHttpSetOption failed: %d\n", GetLastError());
        return false;
    }

    return true;
}

bool HttpNetworkClient::get(const wchar_t* url, char* response_buffer, size_t max_length, size_t& bytes_written)
{
    std::lock_guard<std::recursive_mutex> lock(connecting_);

    if (!is_connected_ || !hConnection_) {
        Logger::recoverable_error("HttpNetworkClient::get() not connected\n");
        return false;
    }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (use_ssl_) {
        flags |= WINHTTP_FLAG_SECURE;
    }

    hRequest_ = WinHttpOpenRequest(hConnection_,
        L"GET",
        url,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest_) {
        Logger::recoverable_error("HttpNetworkClient::get() WinHttpOpenRequest failed: %d\n", GetLastError());
        return false;
    }

    // Initialize headers buffer and ensure null termination
    wchar_t headers[MAX_HEADERS_LENGTH] = {};
    
    // Format headers safely with size limit and ensure null termination
    int header_length = _snwprintf_s(headers, _countof(headers) - 1, _TRUNCATE, L"X-API-KEY: %s\r\n", api_key_);
    if (header_length < 0 || header_length >= _countof(headers)) {
        Logger::recoverable_error("HttpNetworkClient::get() Header formatting failed or truncated\n");
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }
    headers[_countof(headers) - 1] = L'\0';  // Ensure null termination
    
    if (!WinHttpSendRequest(hRequest_,
        headers,
        static_cast<DWORD>(wcslen(headers)),  // Use actual length instead of -1
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0))
    {
        Logger::recoverable_error("HttpNetworkClient::get() WinHttpSendRequest failed: %d\n", GetLastError());
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest_, NULL)) {
        Logger::recoverable_error("HttpNetworkClient::get() WinHttpReceiveResponse failed: %d\n", GetLastError());
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    DWORD size = 0;
    if (!WinHttpQueryDataAvailable(hRequest_, &size)) {
        Logger::recoverable_error("HttpNetworkClient::get() WinHttpQueryDataAvailable failed: %d\n", GetLastError());
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    if (size >= max_length) {
        Logger::recoverable_error("HttpNetworkClient::get() response too large\n");
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    DWORD downloaded = 0;
    if (!WinHttpReadData(hRequest_,
        response_buffer,
        size,
        &downloaded))
    {
        Logger::recoverable_error("HttpNetworkClient::get() WinHttpReadData failed: %d\n", GetLastError());
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return false;
    }

    WinHttpCloseHandle(hRequest_);
    hRequest_ = NULL;

    bytes_written = downloaded;
    response_buffer[downloaded] = '\0';
    return true;
}

bool HttpNetworkClient::applyTimeouts(HINTERNET handle)
{
    if (!handle) {
        return false;
    }

    DWORD timeout = connect_timeout_;
    if (!WinHttpSetOption(handle,
        WINHTTP_OPTION_CONNECT_TIMEOUT,
        &timeout,
        sizeof(timeout)))
    {
        return false;
    }

    timeout = send_timeout_;
    if (!WinHttpSetOption(handle,
        WINHTTP_OPTION_SEND_TIMEOUT,
        &timeout,
        sizeof(timeout)))
    {
        return false;
    }

    timeout = receive_timeout_;
    if (!WinHttpSetOption(handle,
        WINHTTP_OPTION_RECEIVE_TIMEOUT,
        &timeout,
        sizeof(timeout)))
    {
        return false;
    }

    return true;
}

bool HttpNetworkClient::negotiateCompression()
{
    if (!hRequest_) {
        return false;
    }

    DWORD encodings = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    if (!WinHttpSetOption(hRequest_,
        WINHTTP_OPTION_DECOMPRESSION,
        &encodings,
        sizeof(encodings)))
    {
        return false;
    }

    use_compression_ = true;
    return true;
}

void HttpNetworkClient::drainConnection()
{
    if (!hRequest_) {
        return;
    }

    char buffer[4096];
    DWORD bytesRead;
    DWORD totalTime = 0;
    const DWORD sleepTime = 100;

    while (WinHttpQueryDataAvailable(hRequest_, &bytesRead) && bytesRead > 0) {
        if (totalTime >= MAX_DRAIN_TIME_MS) {
            break;
        }

        if (!WinHttpReadData(hRequest_,
            buffer,
            min(sizeof(buffer), bytesRead),
            &bytesRead))
        {
            break;
        }

        Sleep(sleepTime);
        totalTime += sleepTime;
    }
}

bool HttpNetworkClient::checkServerCert()
{
    if (!hRequest_) {
        return false;
    }

    PCCERT_CONTEXT certContext = NULL;
    DWORD certContextSize = sizeof(certContext);

    if (!WinHttpQueryOption(hRequest_,
        WINHTTP_OPTION_SERVER_CERT_CONTEXT,
        &certContext,
        &certContextSize))
    {
        return false;
    }

    if (!certContext) {
        return false;
    }

    // Verify certificate chain
    CERT_CHAIN_PARA chainPara = { sizeof(CERT_CHAIN_PARA) };
    PCCERT_CHAIN_CONTEXT chainContext = NULL;

    if (!CertGetCertificateChain(
        NULL,
        certContext,
        NULL,
        NULL,
        &chainPara,
        0,
        NULL,
        &chainContext))
    {
        CertFreeCertificateContext(certContext);
        return false;
    }

    CertFreeCertificateChain(chainContext);
    CertFreeCertificateContext(certContext);
    return true;
}

bool HttpNetworkClient::followRedirect(wchar_t* redirect_buffer, size_t buffer_size)
{
    if (buffer_size > MAXDWORD) {
        Logger::recoverable_error("HttpNetworkClient::followRedirect()> "
            "Buffer size exceeds DWORD maximum\n");
        return false;
    }

    if (!hRequest_) {
        return false;
    }

    DWORD size = static_cast<DWORD>(buffer_size);
    DWORD index = WINHTTP_NO_HEADER_INDEX;
    
    if (!WinHttpQueryHeaders(hRequest_,
        WINHTTP_QUERY_LOCATION,
        WINHTTP_HEADER_NAME_BY_INDEX,
        redirect_buffer,
        &size,
        &index))  // Pass address of index
    {
        return false;
    }
    return true;
}
