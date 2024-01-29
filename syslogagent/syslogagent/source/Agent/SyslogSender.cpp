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

        while (!SyslogSender::stop_requested_ && !queue_.isEmpty()) {
            Debug::senderHeartbeat();
            queue_.lock();
            int msg_size = queue_.peek(buf, Globals::MESSAGE_BUFFER_SIZE);
            Logger::debug2("SyslogSender::run() sending msg %d bytes\n", msg_size);

            bool connected = primary_network_client_->connect();
            bool posted = false;
            if (!connected) {
                Logger::recoverable_error("SyslogSender::run() primary server not connected, error: %u\n", GetLastError());
                // queue_.unlock();
            }
            else {
                posted = primary_network_client_->post(reinterpret_cast<wchar_t*>(buf), msg_size);
                if (posted) {
                    Logger::debug2("SyslogSender::run() message sent to primary server (bytes %d)\n", msg_size);
                }
                else {
                    Logger::debug("SyslogSender::run() error: message not sent to primary server (bytes %d), error: %u\n", msg_size, GetLastError());
                }
                string response;
                primary_network_client_->readResponse(response);
                primary_network_client_->close();
            }
            if (secondary_network_client_) {
                connected = secondary_network_client_->connect();
                if (!connected) {
                    Logger::recoverable_error("SyslogSender::run() secondary server not connected, error: %u\n", GetLastError());
                    // queue_.unlock();
                }
                else {
                    posted = secondary_network_client_->post(reinterpret_cast<wchar_t*>(buf), msg_size);
                    if (posted) {
                        Logger::debug2("SyslogSender::run() message sent to secondary server (bytes %d)\n", msg_size);
                    }
                    else {
                        Logger::debug("SyslogSender::run() error: message not sent to secondary server (bytes %d), error: %u\n", msg_size, GetLastError());
                    }
                }
                secondary_network_client_->close();
            }
            //Logger::force("Syslog_sender::run() queue size before: %d\n", queue_.length());
            queue_.removeFront();
            //Logger::force("Syslog_sender::run() queue size after: %d\n", queue_.length());
            queue_.unlock();
        }
    }
    Globals::instance()->releaseMessageBuffer("SyslogSender::run()", buf);
    Logger::debug2("Syslog_sender::run() ending\n");
}
