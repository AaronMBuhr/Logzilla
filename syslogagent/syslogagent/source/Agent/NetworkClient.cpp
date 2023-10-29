#include "stdafx.h"
#include "NetworkClient.h"

void NetworkClient::enableTls(const char* server_cert_pem) {
	server_cert_pem_ = make_unique<char[]>(strlen(server_cert_pem) + 1);
	memcpy(server_cert_pem_.get(), server_cert_pem, strlen(server_cert_pem) + 1);
	tls_config_ = make_unique<TLS>();
}
