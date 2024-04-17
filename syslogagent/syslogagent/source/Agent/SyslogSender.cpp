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
#include "Util.h"

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
    int batch_count;
    int64_t last_start_loop_time = Util::GetUnixTimeMilliseconds();

    while (!SyslogSender::stop_requested_) {

        Debug::senderHeartbeat();
        //bool shouldWait = !primary_queue_->isEmpty() || (secondary_queue_ != nullptr && !secondary_queue_->isEmpty()); 
        //if (shouldWait) {
        //    // Wait for the timer or until the SYSLOGAGENT_MAINLOOP_WAIT_INTERVAL elapses
        //    if (enqueue_timer_.wait(SYSLOGAGENT_MAINLOOP_WAIT_INTERVAL)) {
        //        enqueue_timer_.reset();
        //    }
        //}
        int64_t current_time = Util::GetUnixTimeMilliseconds();
        int elapsed_time = current_time - last_start_loop_time;
        if (elapsed_time < 0) { 
            Logger::warn("SyslogSender::run() elapsed time < 0\n");
            elapsed_time = 0;
        }
        int wait_time = SYSLOGAGENT_SENDER_MAINLOOP_DURATION - elapsed_time;
        Sleep(wait_time);

        last_start_loop_time = Util::GetUnixTimeMilliseconds();

        int msg_size;
        bool connected;
        bool posted;
        char* sep;
        string response;
        Debug::senderHeartbeat();
        if (!primary_queue_->isEmpty()) {
            batch_count = 0;
            memcpy(message_buffer_.get(), message_header_, strlen(message_header_));
            int message_buffer_length = strlen(message_header_);
            sep = "";
            while (batch_count < primary_queue_->length()) {
                msg_size = primary_queue_->peek(buf, Globals::MESSAGE_BUFFER_SIZE, batch_count);
                if (msg_size + message_buffer_length + strlen(sep) + strlen(message_trailer_) < MAX_MESSAGE_SIZE) {
                    memcpy(message_buffer_.get() + message_buffer_length, sep, strlen(sep));
                    message_buffer_length += strlen(sep);
                    memcpy(message_buffer_.get() + message_buffer_length, buf, msg_size);
                    message_buffer_length += msg_size;
                    batch_count++;
                }
                else {
                    break;
                }
                sep = const_cast<char*>(message_separator_);
            }
            memcpy(message_buffer_.get() + message_buffer_length, message_trailer_, strlen(message_trailer_));
            message_buffer_length += strlen(message_trailer_);
            Logger::debug2("SyslogSender::run() sending msg %d bytes to primary\n", message_buffer_length);

            connected = primary_network_client_->connect();
            if (!connected) {
                Logger::recoverable_error("SyslogSender::run() primary server not connected, error: %u\n", GetLastError());
            }
            else {
                posted = primary_network_client_->post(message_buffer_.get(), message_buffer_length);
                if (posted == NetworkClient::RESULT_SUCCESS) {
                    Logger::debug2("SyslogSender::run() message sent to primary server (bytes %d)\n", msg_size);
                    primary_queue_->lock();
                    for (int i = 0; i < batch_count; i++) {
                        primary_queue_->removeFront();
                    }
                    primary_queue_->unlock();

                }
                else {
                    Logger::debug("SyslogSender::run() error: message not sent to primary server (bytes %d), error: %u\n", msg_size, posted);
                }
                primary_network_client_->readResponse(response);
                primary_network_client_->close();
            }
            //Logger::force("Syslog_sender::run() queue size after: %d\n", queue_.length());
        }

        if (secondary_queue_ != nullptr && !secondary_queue_->isEmpty()) {
            batch_count = 0;
            memcpy(message_buffer_.get(), message_header_, strlen(message_header_));
            int message_buffer_length = strlen(message_header_);
            sep = "";
            while (batch_count < secondary_queue_->length()) {
                msg_size = secondary_queue_->peek(buf, Globals::MESSAGE_BUFFER_SIZE, batch_count);
                if (msg_size + message_buffer_length + strlen(sep) + strlen(message_trailer_) - 1 < MAX_MESSAGE_SIZE) {
                    memcpy(message_buffer_.get() + message_buffer_length, sep, strlen(sep));
                    message_buffer_length += strlen(sep);
                    memcpy(message_buffer_.get() + message_buffer_length, buf, msg_size);
                    message_buffer_length += msg_size;
                    batch_count++;
                }
                else {
                    break;
                }
                sep = const_cast<char*>(message_separator_);
                // break;
            }
            memcpy(message_buffer_.get() + message_buffer_length, message_trailer_, strlen(message_trailer_));
            message_buffer_length += strlen(message_trailer_);
            Logger::debug2("SyslogSender::run() sending msg %d bytes to secondary\n", message_buffer_length);

            connected = secondary_network_client_->connect();
            posted = false;
            if (!connected) {
                Logger::recoverable_error("SyslogSender::run() secondary server not connected, error: %u\n", GetLastError());
            }
            else {
                posted = secondary_network_client_->post(message_buffer_.get(), message_buffer_length);
                if (posted == NetworkClient::RESULT_SUCCESS) {
                    Logger::debug2("SyslogSender::run() message sent to secondary server (bytes %d)\n", msg_size);
                    secondary_queue_->lock();
                    for (int i = 0; i < batch_count; i++) {
                        secondary_queue_->removeFront();
                    }
                    secondary_queue_->unlock();
                }
                else {
                    Logger::debug("SyslogSender::run() error: message not sent to secondary server (bytes %d), error: %u\n", msg_size, posted);
                }
                secondary_network_client_->readResponse(response);
                secondary_network_client_->close();
            }
            //Logger::force("Syslog_sender::run() queue size before: %d\n", queue_.length());
            //Logger::force("Syslog_sender::run() queue size after: %d\n", queue_.length());
        }
    }
    Globals::instance()->releaseMessageBuffer("SyslogSender::run()", buf);
    Logger::debug2("Syslog_sender::run() ending\n");
}
