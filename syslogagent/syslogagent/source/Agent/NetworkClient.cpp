#include "stdafx.h"
#include "Windows.h"
#include "wincrypt.h"
#include "WinHttp.h"
#include <regex>
#include <string>
#include "Logger.h"
#include "NetworkClient.h"
#include "SyslogAgentSharedConstants.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment (lib, "crypt32")



namespace Syslog_agent {
    NetworkClient::~NetworkClient()
    {
        if (hConnect_) {
            WinHttpCloseHandle(hConnect_);
            hConnect_ = NULL;
        }
        if (use_ssl_)
        {
            if (pCertContext_ != NULL)
            {
                CertFreeCertificateContext(pCertContext_);
            }
            if (hCertStore_ != NULL)
            {
                CertCloseStore(hCertStore_, 0);
            }

            // Don't forget to free the memory allocated for the PFX file
            if (pfxBuffer_ != NULL) {
                delete[] pfxBuffer_;
            }

        }
        if (hSession_) {
            WinHttpCloseHandle(hSession_);
        }
    }


    void CALLBACK NetworkClient::StatusCallback(
        HINTERNET hInternet,
        DWORD_PTR dwContext,
        DWORD dwInternetStatus,
        LPVOID lpvStatusInformation,
        DWORD dwStatusInformationLength
    ) {
        // Cast dwContext back to NetworkClient*
        printf("StatusCallback: %p\n", dwContext);
        NetworkClient* pThis = reinterpret_cast<NetworkClient*>(dwContext);
        printf("StatusCallback: %p\n", pThis);
        if (pThis) {
            // Now you can call instance methods or access instance data
            // pThis->requestCallbackStatus_ = dwInternetStatus;
            printf("StatusCallback: %d\n", dwInternetStatus);
            pThis->setRequestCallbackStatus(dwInternetStatus);
        }

        //if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE) {
        //    DWORD dwStatusFlags = *(LPDWORD)lpvStatusInformation;

        //    // Check which security errors occurred
        //    if (dwStatusFlags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID) {
        //        // The server's certificate common name doesn't match the requested host
        //        // Additional handling can be done here
        //    }

        //    if (dwStatusFlags & WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT) {
        //        // The server's certificate is invalid
        //        // Additional handling can be done here
        //    }
        //}
    }



    bool NetworkClient::initialize(const Configuration* config, const wstring api_key, const std::wstring& url, bool use_ssl, int port)
    {
        config_ = config;
        api_key_ = api_key;
        // Regular expression to match URL and optional port (e.g., "http://127.0.0.1:5000")
        std::wregex url_regex(LR"(^(http:\/\/|https:\/\/)?([^\/:]+)(:\d+)?(\/.*)?$)");
        std::wsmatch matches;

        if (std::regex_search(url, matches, url_regex))
        {
            if (matches.size() >= 3)
            {
                // URL without port
                url_ = matches[2].str();

                if (matches.size() >= 4 && matches[3].matched)
                {
                    // Extract port number
                    std::wstring port_str = matches[3].str().substr(1); // Remove ':' from port
                    port_ = std::stoi(port_str);
                }
                else
                {
                    // Use default port if no port is specified in the URL
                    port_ = ((port != 0) ? port // Use port from parameter if specified)
                        : (use_ssl ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT));
                }
            }
            else
            {
                // Invalid URL format
                Logger::critical("NetworkClient::Initialize()> Invalid URL format.\n");
                return false;
            }
        }
        else
        {
            // Regex matching failed
            Logger::critical("NetworkClient::Initialize()> URL parsing failed.\n");
            return false;
        }

        // Rest of the initialization code...
        hSession_ = WinHttpOpen(SYSLOGAGENT_USER_AGENT,
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);

        if (!hSession_)
        {
            Logger::critical("NetworkClient::Initialize()> Error %u in WinHttpOpen.\n",
                GetLastError());
            return false;
        }

        use_ssl_ = use_ssl;
        hConnect_ = NULL;
        hRequest_ = NULL;

        if (use_ssl_) {
            // Set the callback function
            WinHttpSetStatusCallback(
                hSession_,
                (WINHTTP_STATUS_CALLBACK)StatusCallback,
                WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
                NULL
            );
            // Pass 'this' pointer as the context value
            //if (!WinHttpSetOption(
            //    hSession_,
            //    WINHTTP_OPTION_CONTEXT_VALUE,
            //    (LPVOID)this,
            //    sizeof(LPVOID)
            //)) {
            //    Logger::critical("WinHttpSetOption failed with error %u.\n", GetLastError());
            //    return false;
            //}
            LPVOID this_ptr = (LPVOID) this;
            LPVOID test = (LPVOID) &this_ptr;
            if (!WinHttpSetOption(
                hSession_,
                WINHTTP_OPTION_CONTEXT_VALUE,
                (LPVOID) test,
                sizeof(test)
            )) {
                Logger::critical("WinHttpSetOption failed with error %u.\n", GetLastError());
                return false;
            }
            printf("init: %p\n", this);
        }
       
        return true;

    }


    bool NetworkClient::initialize(const Configuration* config, const wstring api_key, const std::wstring& url) {
        // Determine use_ssl based on URL prefix
        bool use_ssl = url.find(L"https://") == 0;

        // Call the original initialize method with determined use_ssl
        // Default port will be set in the original initialize method
        return initialize(config, api_key, url, use_ssl);
    }

    bool NetworkClient::loadCertificate(const std::wstring& cert_path)
    {
        // Open the PFX file
        HANDLE hFile = CreateFile(L"E:\\Source\\Mine\\Logzilla\\syslogagent\\syslogagent\\source\\Config\\bin\\x64\\Debug\\primary.pfx", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            Logger::critical("NetworkClient::LoadCertificate()> Error %u in CreateFile.\n",
                GetLastError());
            return false;
        }
        DWORD dwFileSize = GetFileSize(hFile, NULL);
        pfxBuffer_ = new BYTE[dwFileSize];

        // Read the file into memory
        DWORD dwRead = 0;
        if (!ReadFile(hFile, pfxBuffer_, dwFileSize, &dwRead, NULL)) {
            Logger::critical("NetworkClient::LoadCertificate()> Error %u in ReadFile.\n",
                GetLastError());
            return false;
        }

        // Close the file
        CloseHandle(hFile);

        CRYPT_DATA_BLOB pfxBlob;
        pfxBlob.pbData = pfxBuffer_;
        pfxBlob.cbData = dwFileSize;

        // hCertStore_ = PFXImportCertStore(&pfxBlob, L"pfx-password", 0);
        hCertStore_ = PFXImportCertStore(&pfxBlob, L"", 0);
        if (!hCertStore_) {
            Logger::critical("NetworkClient::LoadCertificate()> Error %u in PFXImportCertStore.\n",
                GetLastError());
            return false;
        }

        pCertContext_ = CertFindCertificateInStore(hCertStore_, X509_ASN_ENCODING, 0, CERT_FIND_ANY, NULL, NULL);
        if (!pCertContext_) {
            Logger::critical("NetworkClient::LoadCertificate()> Error %u in CertFindCertificateInStore.\n",
                GetLastError());
            return false;
        }
        return true;

    }


    bool NetworkClient::connect()
    {
        if (use_ssl_)
        {
            hConnect_ = WinHttpConnect(hSession_, url_.c_str(), port_, 0);
        }
        else
        {
            hConnect_ = WinHttpConnect(hSession_, url_.c_str(), port_, 0);
        }

        if (!hConnect_)
        {
            Logger::recoverable_error("NetworkClient::Connect()> Error %u in WinHttpConnect.\n", GetLastError());
            return false;
        }

        return true;
    }


    bool NetworkClient::post(const wchar_t* buf, size_t length)
    {
        //WINHTTP_CONNECTION_INFO connectionInfo;
        //DWORD dwSize = sizeof(connectionInfo);
        //if (WinHttpQueryOption(hConnect_, WINHTTP_OPTION_CONNECTION_INFO, &connectionInfo, &dwSize))
        //{
        //    Logger::debug("NetworkClient::Post()> got connection Info");
        //}
        // printf("NetworkClient::Post()> %d\n", requestCallbackStatus_);
        printf("post: %p\n", this);


        if (use_ssl_) {
            hRequest_ = WinHttpOpenRequest(hConnect_, L"POST", config_->api_path.c_str(),
                NULL, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE);
        }
        else {
            hRequest_ = WinHttpOpenRequest(hConnect_, L"POST", config_->api_path.c_str(),
                NULL, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                0);
        }
        if (!hRequest_)
        {
            Logger::recoverable_error("NetworkClient::Post()> Error %u in WinHttpOpenRequest.\n",
                GetLastError());
            return false;
        }

        // Additional steps for HTTPS connection
        if (use_ssl_)
        {
            //DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            //    SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
            //    SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            //    SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
            DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
            // DWORD dwFlags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID;

            // This is an example; adjust the flags based on your security requirements
            // Note: Ignoring these errors can make the connection less secure

            if (!WinHttpSetOption(hRequest_, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags)))
            {
                Logger::recoverable_error("NetworkClient::post()> Error %u in WinHttpSetOption.\n", GetLastError());
                WinHttpCloseHandle(hRequest_);
                WinHttpCloseHandle(hConnect_);
                hRequest_ = NULL;
                hConnect_ = NULL;
                return false;
            }
        }

        //if (pCertContext_ != NULL)
        //{
        //    WinHttpSetOption(hRequest_, WINHTTP_OPTION_CLIENT_CERT_CONTEXT, (LPVOID)pCertContext_, sizeof(CERT_CONTEXT));
        //}

        // Add custom headers
        std::wstring headers = L"Authorization: token " + api_key_ + L"\r\n"
            L"Content-Type: application/json\r\n";
        if (!WinHttpAddRequestHeaders(hRequest_, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD))
        {
            Logger::recoverable_error("NetworkClient::Post()> Error %u in WinHttpAddRequestHeaders.\n",
                GetLastError());
            return false;
        }

        if (!WinHttpSendRequest(hRequest_,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)buf, length, length, 0))
        {
            Logger::recoverable_error("NetworkClient::Post()> Error %u in WinHttpSendRequest.\n",
                GetLastError());
            return false;
        }
        if (!WinHttpReceiveResponse(hRequest_, NULL))
        {
            Logger::recoverable_error("NetworkClient::Post()> Error %u in WinHttpReceiveResponse.\n",
                GetLastError());
            return false;
        }

        if (use_ssl_) {
            if (requestCallbackStatus_ != WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID) {
                Logger::recoverable_error("NetworkClient::Post()> Error %u in WinHttpReceiveResponse.\n",
                                       GetLastError());
                return false;
            }
        }

        return true;
    }


    bool NetworkClient::post(const std::wstring& data)
    {
        return post(data.c_str(), data.length());
    }


    bool NetworkClient::readResponse(std::string& response)
    {
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        LPSTR pszOutBuffer;
        BOOL  bResults = FALSE;
        DWORD dwBytesRead = 0;

        do
        {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest_, &dwSize))
            {
                Logger::critical("NetworkClient::ReadResponse()> Error %u in WinHttpQueryDataAvailable.\n",
                    GetLastError());
                return false;
            }

            pszOutBuffer = new char[dwSize + 1];
            if (!pszOutBuffer)
            {
                Logger::critical("NetworkClient::ReadResponse()> Out of memory\n");
                dwSize = 0;
                return false;
            }

            ZeroMemory(pszOutBuffer, dwSize + 1);

            if (!WinHttpReadData(hRequest_, (LPVOID)pszOutBuffer,
                dwSize, &dwDownloaded))
            {
                Logger::critical("NetworkClient::ReadResponse()> Error %u in WinHttpReadData.\n", GetLastError());
                return false;
            }
            else
            {
                response.append(pszOutBuffer, dwDownloaded);
            }

            delete[] pszOutBuffer;

        } while (dwSize > 0);

        return true;
    }


    void NetworkClient::close() {
        if (hRequest_) {
            WinHttpCloseHandle(hRequest_);
            hRequest_ = NULL;
        }
        if (hConnect_) {
            WinHttpCloseHandle(hConnect_);
            hConnect_ = NULL;
        }
    }

}