#pragma once
#include "stdafx.h"
#include <memory>
#include <string>
#include <Windows.h>

#include "TLS.h"

using namespace std;

class NetworkClient
{
public:
	void enableTls(const char* server_cert_pem);
	virtual int connect() = 0;
	virtual int send(const char* buffer, const int len) = 0;
	virtual int send(const char* buffer, const int len, int& error_code) = 0;
	virtual void close() = 0;
	virtual bool isConnected() = 0;
	virtual SOCKET getSocket() = 0;
	virtual wstring connectionName() = 0;
	virtual string connectionNameUtf8() = 0;

protected:
	unique_ptr<char[]> server_cert_pem_ = nullptr;
	unique_ptr<TLS> tls_config_ = nullptr;
};


