#pragma once
#include "winhttp.h"
#include "windows.h"
#include <string>
#include "Configuration.h"

namespace Syslog_agent {

    class NetworkClient
    {
    public:
        const int MESSAGE_BUFFER_SIZE = 64 * 1024 * 1024;
        NetworkClient::NetworkClient()
            : use_ssl_(false),
            url_(L""),
            hSession_(NULL),
            hConnect_(NULL),
            hRequest_(NULL),
            pCertContext_(NULL),
            hCertStore_(NULL),
            pfxBuffer_(NULL),
            config_(NULL),
            port_(0),
            server_cert_checked_(false)
        { }
        ~NetworkClient();

        bool initialize(const Configuration* config, const wstring api_key,const std::wstring& url, bool use_ssl, int port = 0);
        bool initialize(const Configuration* config, const wstring api_key, const std::wstring& url);
        bool loadCertificate(const std::wstring& cert_path);
        bool connect();
        bool post(const char* buf, size_t length);
        bool post(const std::wstring& data);
        bool post(const std::string & data);
        bool checkServerCert();
        bool readResponse(std::string& response);
        void close();

    private:
        wstring api_key_;
        const Configuration *config_;
        bool use_ssl_;
        std::wstring url_;
        int port_;
        HINTERNET hSession_;
        HINTERNET hConnect_;
        HINTERNET hRequest_;
        PCCERT_CONTEXT pCertContext_;
        HCERTSTORE hCertStore_;
        BYTE* pfxBuffer_;
        bool server_cert_checked_;
    };

}