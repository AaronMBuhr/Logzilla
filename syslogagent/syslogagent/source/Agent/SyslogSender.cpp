/*
SyslogAgent: a syslog agent for Windows
Copyright © 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include <stdio.h>
#include <ws2tcpip.h>
#include "Debug.h"
#include "Globals.h"
#include "Result.h"
#include "SyslogSender.h"

using namespace Syslog_agent;

#define IDLE_INTERVAL 20000

WindowsEvent SyslogSender::enqueue_event_(L"SyslogAgentEnqueueEvent");
volatile bool SyslogSender::stop_requested_ = false;

SyslogSender::SyslogSender(
	MessageQueue& queue,
	Configuration& config,
	shared_ptr<NetworkClient> primary_network_client,
	shared_ptr<NetworkClient> secondary_network_client
) :
	queue_(queue),
	config_(config),
	primary_network_client_(primary_network_client),
	secondary_network_client_(secondary_network_client)
{}



void SyslogSender::run() const {

	Logger::debug2("Syslog_sender::run() starting\n");
	char* buf = Globals::instance()->getMessageBuffer("SyslogSender::run()");
	while (!SyslogSender::stop_requested_) {

		Debug::senderHeartbeat();
		if (!SyslogSender::stop_requested_) {
			if (enqueue_event_.wait(5000)) {
				enqueue_event_.reset();
			}
		}

		while (!SyslogSender::stop_requested_ && primary_network_client_->isConnected() && !queue_.isEmpty()) {
			Debug::senderHeartbeat();
			if (primary_network_client_->isConnected() && !queue_.isEmpty()) {
				queue_.lock();
				int msg_size = queue_.peek(buf, Globals::MESSAGE_BUFFER_SIZE);
				int error_code = 0;
				Logger::debug2("SyslogSender::run() sending msg %d bytes\n", msg_size);

				int num_bytes = primary_network_client_->send(buf, msg_size, error_code);
				Logger::debug2("SyslogSender::run() sent %d bytes\n", num_bytes);
				if (num_bytes == SOCKET_ERROR || num_bytes < msg_size) {
					error_code = WSAGetLastError();
					Logger::recoverable_error("Syslog_sender::run() : send primary msg size %d, sent %d bytes, error %d, closing connection\n", msg_size, num_bytes, error_code);
					//if (error_code == WSAECONNABORTED || error_code == WSAECONNRESET) {
					//	primary_network_client_->close();
					//}
					primary_network_client_->close();
				}
				else {
					Logger::debug("SyslogSender::run() message sent to primary server (bytes %d)\n", num_bytes);
					if (secondary_network_client_ && secondary_network_client_->isConnected()) {
						if (int num_bytes_secondary = secondary_network_client_->send(buf, msg_size, error_code) < msg_size) {
							Logger::recoverable_error("Syslog_sender::run() : send secondary sent %d bytes, error %d\n", num_bytes, error_code);
						}
						else {
							Logger::debug("  also sent to secondary server\n");
						}
					}
					//Logger::force("Syslog_sender::run() queue size before: %d\n", queue_.length());
					queue_.removeFront();
					//Logger::force("Syslog_sender::run() queue size after: %d\n", queue_.length());
				}
				queue_.unlock();
			}
		}
	}
	Globals::instance()->releaseMessageBuffer("SyslogSender::run()", buf);
	Logger::debug2("Syslog_sender::run() ending\n");

}
