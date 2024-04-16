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

WindowsTimer SyslogSender::enqueue_timer_;
volatile bool SyslogSender::stop_requested_ = false;
const char SyslogSender::message_header_[] = "{ \"events\": [ ";
const char SyslogSender::message_separator_[] = ", ";
const char SyslogSender::message_trailer_[] = " ] }";


SyslogSender::SyslogSender(
    Configuration& config,
    shared_ptr<MessageQueue> primary_queue,
    shared_ptr<MessageQueue> secondary_queue,
    shared_ptr<NetworkClient> primary_network_client,
    shared_ptr<NetworkClient> secondary_network_client
) :
    config_(config),
    primary_queue_(primary_queue),
    secondary_queue_(secondary_queue),
    primary_network_client_(primary_network_client),
    secondary_network_client_(secondary_network_client)
{
    message_buffer_ = make_unique<char[]>(MAX_MESSAGE_SIZE);
}



void SyslogSender::run() const {

    Logger::debug2("Syslog_sender::run() starting\n");
    char* buf = Globals::instance()->getMessageBuffer("SyslogSender::run()");
    while (!SyslogSender::stop_requested_) {

        Debug::senderHeartbeat();
        bool shouldWait = !primary_queue_->isEmpty() || (secondary_queue_ != nullptr && !secondary_queue_->isEmpty());
        if (shouldWait) {
            // Wait for the timer or until the SYSLOGAGENT_MAINLOOP_WAIT_INTERVAL elapses
            if (enqueue_timer_.wait(SYSLOGAGENT_MAINLOOP_WAIT_INTERVAL)) {
                enqueue_timer_.reset();
            }
        }
        int last;
        int secondlast;

        while (!SyslogSender::stop_requested_ 
            && (!primary_queue_->isEmpty() || (secondary_queue_ != nullptr && !secondary_queue_->isEmpty()))) {
            int msg_size;
            bool connected;
            bool posted;
            char* sep;
            string response;
            Debug::senderHeartbeat();
            if (!primary_queue_->isEmpty()) {
                memcpy(message_buffer_.get(), message_header_, strlen(message_header_));
                int message_buffer_length = strlen(message_header_);
                primary_queue_->lock();
                sep = "";
                while (!primary_queue_->isEmpty()) {
                    msg_size = primary_queue_->peek(buf, Globals::MESSAGE_BUFFER_SIZE);
                    last = buf[msg_size - 1];
                    secondlast = buf[msg_size - 2];
                    if (msg_size + message_buffer_length + strlen(sep) + strlen(message_trailer_) < MAX_MESSAGE_SIZE) {
                        memcpy(message_buffer_.get() + message_buffer_length, sep, strlen(sep));
                        message_buffer_length += strlen(sep);
                        last = message_buffer_[message_buffer_length - 1];
                        secondlast = message_buffer_[message_buffer_length - 2];
                        memcpy(message_buffer_.get() + message_buffer_length, buf, msg_size);
                        message_buffer_length += msg_size;
                        primary_queue_->removeFront();
                    }
                    else {
                        break;
                    }
                    sep = const_cast<char*>(message_separator_);
                    // break;
                }
                last = message_buffer_[message_buffer_length - 1];
                secondlast = message_buffer_[message_buffer_length - 2];
                int thesize = strlen(message_trailer_);
                memcpy(message_buffer_.get() + message_buffer_length, message_trailer_, strlen(message_trailer_));
                message_buffer_length += strlen(message_trailer_);
                last = message_buffer_[message_buffer_length - 1];
                secondlast = message_buffer_[message_buffer_length - 2];
                Logger::debug2("SyslogSender::run() sending msg %d bytes to primary\n", message_buffer_length);

                connected = primary_network_client_->connect();
                posted = false;
                if (!connected) {
                    Logger::recoverable_error("SyslogSender::run() primary server not connected, error: %u\n", GetLastError());
                    // queue_.unlock();
                }
                else {
                    last = message_buffer_[message_buffer_length - 1];
                    secondlast = message_buffer_[message_buffer_length - 2];

                    posted = primary_network_client_->post(message_buffer_.get(), message_buffer_length);
                    if (posted) {
                        Logger::debug2("SyslogSender::run() message sent to primary server (bytes %d)\n", msg_size);
                        primary_queue_->removeFront();
                    }
                    else {
                        Logger::debug("SyslogSender::run() error: message not sent to primary server (bytes %d), error: %u\n", msg_size, GetLastError());
                    }
                    primary_network_client_->readResponse(response);
                    primary_network_client_->close();
                }
                //Logger::force("Syslog_sender::run() queue size after: %d\n", queue_.length());
                primary_queue_->unlock();
            }
            if (secondary_queue_ != nullptr && !secondary_queue_->isEmpty()) {
                memcpy(message_buffer_.get(), message_header_, strlen(message_header_));
                int message_buffer_length = strlen(message_header_);
                last = message_buffer_[message_buffer_length - 1];
                secondlast = message_buffer_[message_buffer_length - 2];
                sep = "";
                secondary_queue_->lock();
                while (!secondary_queue_->isEmpty()) {
                    msg_size = secondary_queue_->peek(buf, Globals::MESSAGE_BUFFER_SIZE);
                    last = buf[msg_size - 1];
                    secondlast = buf[msg_size - 2];
                    if (msg_size + message_buffer_length + strlen(sep) + strlen(message_trailer_) - 1 < MAX_MESSAGE_SIZE) {
                        memcpy(message_buffer_.get() + message_buffer_length, sep, strlen(sep));
                        message_buffer_length += strlen(sep);
                        last = message_buffer_[message_buffer_length - 1];
                        secondlast = message_buffer_[message_buffer_length - 2];
                        memcpy(message_buffer_.get() + message_buffer_length, buf, msg_size);
                        message_buffer_length += msg_size;
                        last = message_buffer_[message_buffer_length - 1];
                        secondlast = message_buffer_[message_buffer_length - 2];
                        secondary_queue_->removeFront();
                    }
                    else {
                        break;
                    }
                    sep = const_cast<char*>(message_separator_);
                    // break;
                }
                last = message_buffer_[message_buffer_length - 1];
                secondlast = message_buffer_[message_buffer_length - 2];
                memcpy(message_buffer_.get() + message_buffer_length, message_trailer_, strlen(message_trailer_));
                message_buffer_length += strlen(message_trailer_);
                Logger::debug2("SyslogSender::run() sending msg %d bytes to secondary\n", message_buffer_length);

                connected = secondary_network_client_->connect();
                posted = false;
                if (!connected) {
                    Logger::recoverable_error("SyslogSender::run() secondary server not connected, error: %u\n", GetLastError());
                    // queue_.unlock();
                }
                else {
                    last = message_buffer_[message_buffer_length - 1];
                    secondlast = message_buffer_[message_buffer_length - 2];
                    posted = secondary_network_client_->post(message_buffer_.get(), message_buffer_length);
                    if (posted) {
                        Logger::debug2("SyslogSender::run() message sent to secondary server (bytes %d)\n", msg_size);
                        secondary_queue_->removeFront();
                    }
                    else {
                        Logger::debug("SyslogSender::run() error: message not sent to secondary server (bytes %d), error: %u\n", msg_size, GetLastError());
                    }
                    secondary_network_client_->readResponse(response);
                    secondary_network_client_->close();
                }
                //Logger::force("Syslog_sender::run() queue size before: %d\n", queue_.length());
                //Logger::force("Syslog_sender::run() queue size after: %d\n", queue_.length());
                secondary_queue_->unlock();
            }
        }
    }
    Globals::instance()->releaseMessageBuffer("SyslogSender::run()", buf);
    Logger::debug2("Syslog_sender::run() ending\n");
}
