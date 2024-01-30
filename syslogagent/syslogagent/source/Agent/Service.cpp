/*
SyslogAgent: a syslog agent for Windows
Copyright © 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include <conio.h>
#include <fileapi.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <tchar.h>
#include <time.h>
#include <vector>
#include <windows.h>

#include "Logger.h"
#include "Configuration.h"
#include "Debug.h"
#include "EventHandlerMessageQueuer.h"
#include "EventLogEvent.h"
#include "EventLogSubscription.h"
#include "FileWatcher.h"
#include "MessageQueueLogMessageSender.h"
#include "Service.h"
#include "SyslogAgentSharedConstants.h"
#include "SyslogSender.h"
#include "Util.h"

using namespace Syslog_agent;

unique_ptr<thread> Service::send_thread_ = nullptr;
MessageQueue Service::message_queue_(Service::MESSAGE_QUEUE_SIZE, Service::MESSAGE_BUFFERS_CHUNK_SIZE);
Configuration Service::config_;
shared_ptr<NetworkClient> Service::primary_network_client_ = nullptr;
shared_ptr<NetworkClient> Service::secondary_network_client_ = nullptr;
shared_ptr<MessageQueueLogMessageSender> Service::log_msg_sender_ = nullptr;

volatile bool Service::shutdown_requested_ = false;
volatile bool Service::service_shutdown_requested_ = false;
WindowsEvent Service::shutdown_event_{ L"LogZilla_SyslogAgent_Service_Shutdown" };
shared_ptr<FileWatcher> Service::filewatcher_ = nullptr;
vector<EventLogSubscription> Service::subscriptions_;

void sendMessagesThread() {
	try {
		SyslogSender sender(
			Service::message_queue_,
			Service::config_,
			Service::primary_network_client_,
			Service::secondary_network_client_);
		sender.run();
	}
	catch (std::exception& exception) {
		Logger::critical("%s\n", exception.what());
	}
}


void Service::run(bool running_as_console) {

	Registry::loadSetupFile();

	config_.use_log_agent_ = true;

	if (config_.tail_filename_ != L"") {
		log_msg_sender_ = make_shared<MessageQueueLogMessageSender>(message_queue_);
		string program_name = Util::wstr2str(config_.tail_program_name_);
		log_msg_sender_ = make_shared<MessageQueueLogMessageSender>(message_queue_);
		filewatcher_ = make_shared<FileWatcher>(
			static_cast<shared_ptr<JsonLogMessageHandler>>(log_msg_sender_),
			config_.tail_filename_.c_str(),
			config_.MAX_TAIL_FILE_LINE_LENGTH,
			program_name.c_str(),
			config_.host_name_,
			(config_.severity_ == SYSLOGAGENT_SEVERITY_DYNAMIC ? SYSLOGAGENT_SEVERITY_INFORMATIONAL : config_.severity_),
			config_.facility_
			);
	}


	Service::primary_network_client_ = make_shared<NetworkClient>();
	if (!Service::primary_network_client_->initialize(&config_, config_.primary_api_key, config_.primary_host_)) {
		Logger::fatal("Could not initialize primary network client\n");
		exit(1);
	}

	// read primary cert file
	if (config_.primary_use_tls_) {
		wstring primary_cert_path = Util::getThisPath(true) + config_.PRIMARY_CERT_FILENAME;
		if (!primary_network_client_->loadCertificate(primary_cert_path)) {
			Logger::fatal("Could not read primary cert from %s\n", Util::wstr2str(primary_cert_path).c_str());
			exit(1);
		}
	}

	if (config_.hasSecondaryHost()) {
		Service::secondary_network_client_ = make_shared<NetworkClient>();
		if (!Service::secondary_network_client_->initialize(&config_, config_.secondary_api_key, config_.secondary_host_))
			Logger::fatal("Could not initialize secondary network client\n");
		exit(1);

		// read secondary cert file
		if (config_.secondary_use_tls_) {
			wstring secondary_cert_path = Util::getThisPath(true) + config_.SECONDARY_CERT_FILENAME;
			if (!secondary_network_client_->loadCertificate(secondary_cert_path)) {
                Logger::fatal("Could not read secondary cert from %s\n", Util::wstr2str(secondary_cert_path).c_str());
                exit(1);
            }
		}
	}
	else {
		Service::secondary_network_client_ = nullptr;
	}

	vector<shared_ptr<NetworkClient>> clients;
	clients.push_back(primary_network_client_);
	if (secondary_network_client_) {
		clients.push_back(secondary_network_client_);
	}

	thread sender(sendMessagesThread);
	bool first_loop = true;
	int restart_needed = 0;
	Debug::startHeartbeatMonitoring();

	if (config_.use_log_agent_) {
		subscriptions_.reserve(config_.logs_.size());
		for (auto& log : config_.logs_) {
			unique_ptr<EventHandlerMessageQueuer> handler
				= make_unique<EventHandlerMessageQueuer>(
					config_,
					message_queue_,
					const_cast<const wchar_t*>(log.name_.c_str()));
			EventLogSubscription subscription(
				log.name_,
				log.channel_,
				wstring(L"*"),
				(config_.only_while_running_ ? NULL : log.bookmark_),
				std::move(handler));
			subscriptions_.push_back(std::move(subscription));
			auto bookmark = Registry::readBookmark(log.channel_.c_str());
			subscriptions_.back().subscribe(bookmark.c_str());
		}
	}

	while (!shutdown_requested_) {
		Logger::debug2("service run first !shutdown_requested\n");
		auto this_run_time = time(nullptr);

		if (filewatcher_ != nullptr) {
			Result tail_result = filewatcher_->process();
			if (!tail_result.isSuccess() && tail_result.statusCode() != FileWatcher::NoNewData) {
				Logger::debug("FileWatcher result: %s\n", tail_result.what());
			}
		}

		first_loop = false;
		Logger::debug("waiting for key hit/shutdown requested...\n");
		for (auto i = 0; i < /* config_.event_log_poll_interval_ */ SYSLOGAGENT_DEFAULT_POLL_INTERVAL; i++) {
			if (shutdown_requested_) {
				break;
			}
			if (message_queue_.length() > 0) {
				Logger::debug("Queue length==%d\n", message_queue_.length());
			}
			if (config_.use_log_agent_ && message_queue_.isEmpty()) {
				//Logger::debug("Saving config to registry");
				config_.saveToRegistry();
			}
			if (_kbhit() || restart_needed) {
				if (restart_needed) {
					Logger::debug("restart needed\n");
				}
				else {
					Logger::debug("key hit\n");
				}
				shutdown_requested_ = true;
				break;
			}
			if (!shutdown_requested_) {
				//Logger::debug2("service run last !shutdown_requested\n");
				shutdown_event_.wait(1000);
				//Logger::debug2("service run shutdown_event_.wait() complete\n");
			}
		}
		Logger::debug("no key hit & no shutdown requested...\n");
	}

	for (auto& sub : subscriptions_) {
		sub.cancelSubscription();
		auto channel = sub.getChannel();
		auto bookmark = sub.getBookmark();
		Registry::writeBookmark(channel.c_str(), bookmark.c_str());
	}

	Logger::debug("service run joining sender\n");
	SyslogSender::stop();
	sender.join();

	// handle restart only for automated restarts, not for manual shutdown...
	if (!shutdown_requested_ && (!service_shutdown_requested_) && restart_needed) {
		if (running_as_console) {
			Logger::debug("restart needed but running as console, exiting\n");
		}
		Logger::debug("attempting to restart\n");
		Sleep(5000);
		exit(1);
	}
	else {
		Logger::debug("LZ Syslog Agent exiting\n");
	}

	Sleep(2000); // to allow any overlapping callbacks to finish
	Logger::debug("service run exiting\n");

}

void Service::shutdown() {
	Logger::info("Service shutdown requested\n");
	service_shutdown_requested_ = true;
	shutdown_requested_ = true;
	shutdown_event_.signal();
}
