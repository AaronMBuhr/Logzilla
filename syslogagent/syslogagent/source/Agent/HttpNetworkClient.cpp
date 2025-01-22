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
    wchar_t url_buffer[MAX_URL_LENGTH];
    wchar_t api_key_buffer[MAX_API_KEY_LENGTH];
    
    wcsncpy_s(url_buffer, url, MAX_URL_LENGTH - 1);
    wcsncpy_s(api_key_buffer, api_key, MAX_API_KEY_LENGTH - 1);
    
    use_ssl_ = use_ssl;
    port_ = port;

    // Parse URL into host and path using fixed buffers
    wchar_t host_buffer[MAX_URL_LENGTH];
    wchar_t path_buffer[MAX_PATH_LENGTH];
    
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

    // Store the parsed components
    wcsncpy_s(host_, host_buffer, _countof(host_));
    wcsncpy_s(path_, path_buffer, _countof(path_));
    wcsncpy_s(api_key_, api_key_buffer, _countof(api_key_));

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
        Logger::warn("HttpNetworkClient::connect() failed to set timeouts: %d\n", GetLastError());
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
        return ERROR_NOT_CONNECTED;
    }

    Logger::debug2("HttpNetworkClient::post() Starting post operation - Length: %d bytes\n", length);
    
    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (use_ssl_) {
        flags |= WINHTTP_FLAG_SECURE;
        Logger::debug2("HttpNetworkClient::post() Using SSL\n");
    }

    // Log the request URL and data
    Logger::debug2("HttpNetworkClient::post() URL: http%s://%ls:%d%ls\n", 
        use_ssl_ ? "s" : "", host_, port_, path_);

    hRequest_ = WinHttpOpenRequest(hConnection_,
        L"POST",
        path_,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest_) {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::post() Failed to open request: %d\n", error);
        return error;
    }
    Logger::debug2("HttpNetworkClient::post() Successfully opened request\n");

    // Set timeouts on the request handle
    if (!applyTimeouts(hRequest_)) {
        Logger::warn("HttpNetworkClient::post() Failed to set request timeouts\n");
    }

    // Build headers using fixed buffer
    wchar_t headers_buffer[MAX_HEADERS_LENGTH];
    int written = _snwprintf_s(headers_buffer, _countof(headers_buffer), _TRUNCATE,
        L"Authorization: token %s",
        api_key_);
    
    Logger::debug2("HttpNetworkClient::post() Adding headers\n");

    // Add authorization header
    if (!WinHttpAddRequestHeaders(hRequest_, 
        headers_buffer,
        -1,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) 
    {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::post() Failed to add authorization header: %d\n", error);
        cleanup_request();
        return error;
    }

    // Add content type header
    if (!WinHttpAddRequestHeaders(hRequest_,
        L"Content-Type: application/json",
        -1,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
    {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::post() Failed to add content type header: %d\n", error);
        cleanup_request();
        return error;
    }

    Logger::debug2("HttpNetworkClient::post() Sending request\n");
    if (!WinHttpSendRequest(hRequest_,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        (LPVOID)buf,
        length,
        length,
        0))
    {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::post() Send request failed: %d\n", error);
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return error;
    }

    Logger::debug2("HttpNetworkClient::post() Waiting for response\n");
    if (!WinHttpReceiveResponse(hRequest_, NULL)) {
        DWORD error = GetLastError();
        Logger::recoverable_error("HttpNetworkClient::post() Receive response failed: %d\n", error);
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
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
        Logger::recoverable_error("HttpNetworkClient::post() Query headers failed: %d\n", error);
        WinHttpCloseHandle(hRequest_);
        hRequest_ = NULL;
        return error;
    }

    Logger::debug2("HttpNetworkClient::post() Response status code: %d\n", statusCode);

    // Use a single buffer for both success and error responses
    char response_buffer[4096] = { 0 };
    size_t total_read = 0;
    
    // Read response body regardless of status code
    {
        DWORD bytes_available = 0;
        DWORD bytes_read = 0;
        BOOL result = TRUE;

        while (result && WinHttpQueryDataAvailable(hRequest_, &bytes_available)) {
            if (bytes_available > 0) {
                if (total_read >= sizeof(response_buffer)) {
                    Logger::debug2("HttpNetworkClient::post() Response exceeds buffer size\n");
                    break;
                }

                size_t remaining = sizeof(response_buffer) - total_read;
                bytes_available = bytes_available > remaining ? static_cast<DWORD>(remaining) : bytes_available;

                result = WinHttpReadData(hRequest_,
                    response_buffer + total_read,
                    bytes_available,
                    &bytes_read);

                if (!result || bytes_read == 0) {
                    break;
                }

                total_read += bytes_read;
            } else {
                break;
            }
        }
    }

    // Log response based on status code
    if (total_read > 0) {
        if (statusCode == 200 || statusCode == 202) {
            Logger::debug2("HttpNetworkClient::post() Success response body:\n%.*s\n", 
                static_cast<int>(total_read), response_buffer);
        } else {
            Logger::debug2("HttpNetworkClient::post() Error response body:\n%.*s\n", 
                static_cast<int>(total_read), response_buffer);
        }
    }

    cleanup_request();
    return (statusCode == 200 || statusCode == 202) ? RESULT_SUCCESS : ERROR_INVALID_DATA;
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
        Logger::warn("HttpNetworkClient::getLogzillaVersion() failed to set request timeouts\n");
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

    wchar_t headers[MAX_HEADERS_LENGTH];
    _snwprintf_s(headers, MAX_HEADERS_LENGTH, _TRUNCATE, L"X-API-KEY: %s\r\n", api_key_);
    
    if (!WinHttpSendRequest(hRequest_,
        headers,
        -1,
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
