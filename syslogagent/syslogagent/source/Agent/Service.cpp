/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"

#define WIN32_LEAN_AND_MEAN
#include <chrono>
#include <conio.h>
#include <fileapi.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <time.h>
#include <vector>
#include <windows.h>
#include <atomic>
#include <Windows.h>

#include "Configuration.h"
#include "EventHandlerMessageQueuer.h"
#include "EventLogEvent.h"
#include "EventLogSubscription.h"
#include "FileWatcher.h"
#include "Globals.h"
#include "HttpNetworkClient.h"
#include "INetworkClient.h"
#include "JsonNetworkClient.h"
#include "Logger.h"
#include "Service.h"
#include "SlidingWindowMetrics.h"
#include "SyslogAgentSharedConstants.h"
#include "SyslogSender.h"
#include "Util.h"

using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::string;
using std::thread;
using std::make_shared;
using std::atomic;

namespace Syslog_agent {

// Define static members
Configuration Service::config_;
shared_ptr<MessageQueue> Service::primary_message_queue_;
shared_ptr<MessageQueue> Service::secondary_message_queue_;
shared_ptr<INetworkClient> Service::primary_network_client_;
shared_ptr<INetworkClient> Service::secondary_network_client_;
shared_ptr<MessageBatcher> Service::primary_batcher_;
shared_ptr<MessageBatcher> Service::secondary_batcher_;
unique_ptr<SyslogSender> Service::sender_;
std::atomic<bool> Service::fatal_shutdown_in_progress = false;
unique_ptr<thread> Service::send_thread_ = nullptr;
volatile bool Service::shutdown_requested_ = false;
volatile bool Service::service_shutdown_requested_ = false;
WindowsEvent Service::shutdown_event_(L"LogZilla_SyslogAgent_Service_Shutdown");
shared_ptr<FileWatcher> Service::filewatcher_;
vector<EventLogSubscription> Service::subscriptions_;
static SERVICE_STATUS_HANDLE service_status_handle_ = nullptr;
HANDLE Service::g_StopEvent = nullptr;
HANDLE Service::g_ShutdownCompleteEvent = nullptr;

namespace {
    // Helper function to safely clean up network clients
    void cleanupNetworkClient(shared_ptr<INetworkClient>& client) {
        if (client) {
            client->close();
            client.reset();
        }
    }

    // Helper function to safely clean up message queues
    void cleanupMessageQueue(shared_ptr<MessageQueue>& queue) {
        if (queue) {
            queue->beginShutdown();  // Signal shutdown to all threads
            // Give a small delay for other threads to notice shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            while (!queue->isEmpty()) {
                queue->removeFront();
            }
            queue.reset();
        }
    }
} // end anonymous namespace

void Service::loadConfiguration(bool running_from_console, bool override_log_level, Logger::LogLevel override_log_level_setting) {
    config_.loadFromRegistry(running_from_console, override_log_level, override_log_level_setting);
}

void sendMessagesThread() {
    auto logger = LOG_THIS;
    logger->debug2("sendMessagesThread() starting\n");
    Service::sender_ = make_unique<SyslogSender>(
        Service::primary_message_queue_,
        Service::secondary_message_queue_,
        Service::primary_network_client_,
        Service::secondary_network_client_,
        Service::primary_batcher_,
        Service::secondary_batcher_,
        Service::config_.getMaxBatchCount(),
        Service::config_.getMaxBatchAge()
    );

    try {
        Service::sender_->run();
    }
    catch (const std::exception& e) {
        logger->critical("Exception in sendMessagesThread: %s\n", e.what());
        Service::fatalErrorHandler("Fatal error in send thread");
    }

    logger->debug2("sendMessagesThread() ending\n");
}

void Service::RegisterServiceCtrlHandler() {
    auto logger = LOG_THIS;
    service_status_handle_ = ::RegisterServiceCtrlHandlerExW(SERVICE_NAME, ServiceHandlerEx, nullptr);
    if (!service_status_handle_) {
        logger->fatal("Failed to register service control handler. Error: %d\n", GetLastError());
        throw std::runtime_error("Failed to register service control handler");
    }
}

DWORD WINAPI Service::ServiceHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) {
    auto logger = LOG_THIS;
    switch (dwControl) {
        case SERVICE_CONTROL_STOP:
            // Signal main thread to stop
            if (g_StopEvent) {
                SetEvent(g_StopEvent);
                
                // Wait max 10 seconds for clean shutdown
                if (WaitForSingleObject(g_ShutdownCompleteEvent, 10000) != WAIT_OBJECT_0) {
                    logger->warning("Timer expired, forcing termination\n");
                    // Force terminate if clean shutdown fails
                    ExitProcess(0);
                }
            }
            break;
    }
    return NO_ERROR;
}

void Service::run(bool running_as_console) {
    auto logger = LOG_THIS;
    try {
        // Create shutdown event handles
        g_StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        g_ShutdownCompleteEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        if (!g_StopEvent || !g_ShutdownCompleteEvent) {
            logger->fatal("Failed to create shutdown event handles\n");
            throw std::runtime_error("Failed to create shutdown event handles");
        }

#if ONLY_FOR_DEBUGGING_CURRENTLY_DISABLED
        SlidingWindowMetrics::instance().setWindowDuration(Service::RATE_CHECK_INTERVAL_SEC);
#endif
        // Register service control handler if running as service
        if (!running_as_console) {
            RegisterServiceCtrlHandler();
        }

        logger->setFatalErrorHandler(Service::fatalErrorHandler);

        logger->debug("Service::run()> loading setup file (if present)\n");
        Registry::loadSetupFile();

        config_.setUseLogAgent(true);

        // Initialize file watcher if configured
        if (!config_.getTailFilename().empty()) {
            char program_name_buf[1024];
            char filename_buf[1024];
            Util::wstr2str(program_name_buf, sizeof(program_name_buf), config_.getTailProgramName().c_str());
            Util::wstr2str(filename_buf, sizeof(filename_buf), config_.getTailFilename().c_str());
            
            // Convert filename to wide string
            wstring wfilename;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, filename_buf, -1, nullptr, 0);
            if (wlen > 0) {
                vector<wchar_t> wbuf(wlen);
                MultiByteToWideChar(CP_UTF8, 0, filename_buf, -1, wbuf.data(), wlen);
                wfilename = wbuf.data();
            }

            string program_name = program_name_buf;
            if (program_name.length() == 0) {
                logger->info("Service::run()> starting file tail on %s\n",
                    filename_buf);
            } else {
                logger->info("Service::run()> starting file tail on %s for program %s\n",
                    filename_buf, program_name.c_str());
            }
            filewatcher_ = make_shared<FileWatcher>(
                config_,
                wfilename.c_str(),
                config_.MAX_TAIL_FILE_LINE_LENGTH,
                program_name.c_str(),
                config_.getHostName().c_str(),
                (config_.getSeverity() == SharedConstants::Severities::DYNAMIC 
                    ? SharedConstants::Severities::NOTICE 
                    : config_.getSeverity()),
                config_.getFacility()
            );
        }

        // Initialize network components
        if (!initializeNetworkComponents()) {
            logger->fatal("Failed to initialize network components\n");
            throw std::runtime_error("Failed to initialize network components");
        }

        // Start the send thread
        send_thread_ = make_unique<thread>(sendMessagesThread);

        bool first_loop = true;
        int restart_needed = 0;

		initializeEventLogSubscriptions(config_.getLogs());

        mainLoop(running_as_console, first_loop, restart_needed);
        cleanupAndShutdown(running_as_console, restart_needed);
    }
    catch (const std::exception& e) {
        logger->fatal("Service::run()> Fatal error: %s\n", e.what());
        throw;
    }
}

bool Service::initializeNetworkComponents() {
    auto logger = LOG_THIS;
    // Initialize message queues first
    primary_message_queue_ = make_shared<MessageQueue>(MESSAGE_QUEUE_SIZE, MESSAGE_BUFFERS_CHUNK_SIZE);
    logger->debug2("Service::initializeNetworkComponents()> initialized primary message queue\n");

    bool isJsonPort = false;
    // For version check, always use HTTP ports
    unsigned int port = config_.getPrimaryUseTls() ? 443 : 80;

    // First create a temporary HTTP client just for version checking
    shared_ptr<HttpNetworkClient> temp_client = make_shared<HttpNetworkClient>();
    if (!temp_client->initialize(&config_, 
        config_.getPrimaryApiKey().c_str(),
        config_.getPrimaryHost().c_str(),
        config_.getPrimaryUseTls(),
        port)) {
        logger->fatal("Failed to initialize temporary primary network client\n");
        return false;
    }

    // Connect the temporary client before version check
    logger->debug2("Service::run()> connecting temporary client for version check\n");
    if (!temp_client->connect()) {
        logger->fatal("Failed to connect temporary client for version check\n");
        return false;
    }

    // Get version and determine format
    if (!getAndSetLogZillaVersion(temp_client, true)) {
        logger->fatal("Failed to get version from primary server\n");
        return false;
    }

    // Now create the actual client based on the format
    int format = config_.getPrimaryLogformat();
    if (format == SharedConstants::LOGFORMAT_JSONPORT) {
        // Parse the URL to get hostname
        Util::UrlComponents urlComponents;
        if (!Util::ParseUrl(config_.getPrimaryHost().c_str(), urlComponents)) {
            logger->fatal("Failed to parse primary host URL\n");
            return false;
        }

        port = config_.getPrimaryPort();
        if (port < 1) {
            port = urlComponents.port;
            if (!urlComponents.hasExplicitPort || port < 1) {
                port = SharedConstants::LZ_JSON_PORT;
            }
        }
        logger->debug2("Using JSON client for port %d\n", port);
        
        primary_network_client_ = std::static_pointer_cast<INetworkClient>(
            make_shared<JsonNetworkClient>(urlComponents.hostName, port)
        );
        primary_batcher_ = make_shared<JSONMessageBatcher>(
            config_.getMaxBatchCount(),
            config_.getMaxBatchAge()
        );
        isJsonPort = true;
    } else {
        logger->debug2("Using HTTP client for port %d\n", port);
        primary_network_client_ = make_shared<HttpNetworkClient>();
        primary_batcher_ = make_shared<HTTPMessageBatcher>(
            config_.getMaxBatchCount(),
            config_.getMaxBatchAge()
        );
    }
        
    logger->debug2("Service::run()> initializing primary_network_client\n");

    // Create URL with /incoming path for sending events
    wstring url = config_.getPrimaryHost() + SharedConstants::HTTP_API_PATH;
    if (!primary_network_client_->initialize(&config_, 
        config_.getPrimaryApiKey().c_str(),
        url.c_str(),
        config_.getPrimaryUseTls(),
        config_.getPrimaryPort())) {
        logger->fatal("Failed to initialize primary network client\n");
        return false;
    }

    logger->debug2("Service::run()> connecting primary_network_client\n");
    if (!primary_network_client_->connect()) {
        logger->fatal("Failed to connect primary network client\n");
        return false;
    }

    // Initialize primary certificate if needed
    if (config_.getPrimaryUseTls() && !initializePrimaryCertificate()) {
        logger->fatal("Failed to initialize primary certificate\n");
        return false;
    }

    // Initialize secondary components if configured
    if (config_.hasSecondaryHost()) {
        if (!initializeSecondaryComponents()) {
            logger->fatal("Failed to initialize secondary components\n");
            return false;
        }
    }

    return true;
}

bool Service::initializeSecondaryComponents() {
    auto logger = LOG_THIS;
    if (!config_.hasSecondaryHost()) {
        logger->debug2("No secondary host configured\n");
        return true;
    }

    // Initialize secondary message queue
    secondary_message_queue_ = make_shared<MessageQueue>(MESSAGE_QUEUE_SIZE, MESSAGE_BUFFERS_CHUNK_SIZE);
    logger->debug2("Service::initializeSecondaryComponents()> initialized secondary message queue\n");

    bool isJsonPort = false;
    unsigned int port = config_.getSecondaryPort();
    if (port == 0) {
        logger->fatal("Secondary port is not configured\n");
        return false;
    }

    // First create a temporary HTTP client just for version checking
    shared_ptr<HttpNetworkClient> temp_client = make_shared<HttpNetworkClient>();
    if (!temp_client->initialize(&config_, 
        config_.getSecondaryApiKey().c_str(),
        config_.getSecondaryHost().c_str(),
        config_.getSecondaryUseTls(),
        port)) {
        logger->fatal("Failed to initialize temporary secondary network client\n");
        return false;
    }

    // Connect the temporary client before version check
    logger->debug2("Service::run()> connecting temporary client for version check\n");
    if (!temp_client->connect()) {
        logger->fatal("Failed to connect temporary client for version check\n");
        return false;
    }

    // Get version and determine format
    if (!getAndSetLogZillaVersion(temp_client, false)) {
        logger->fatal("Failed to get version from secondary server\n");
        return false;
    }

    // Now create the actual client based on the format
    int format = config_.getSecondaryLogformat();
    if (format == SharedConstants::LOGFORMAT_JSONPORT) {
        logger->debug2("Using JSON client for secondary port %d\n", SharedConstants::LZ_JSON_PORT);
        
        // Parse the URL to get hostname
        Util::UrlComponents urlComponents;
        if (!Util::ParseUrl(config_.getSecondaryHost().c_str(), urlComponents)) {
            logger->fatal("Failed to parse secondary host URL\n");
            return false;
        }

        port = config_.getSecondaryPort();
        if (port < 1) {
            port = urlComponents.port;
            if (!urlComponents.hasExplicitPort || port < 1) {
                port = SharedConstants::LZ_JSON_PORT;
            }
        }

        secondary_network_client_ = std::static_pointer_cast<INetworkClient>(
            make_shared<JsonNetworkClient>(urlComponents.hostName, port)
        );
        secondary_batcher_ = make_shared<JSONMessageBatcher>(
            config_.getMaxBatchCount(),
            config_.getMaxBatchAge()
        );
        isJsonPort = true;
    } else {
        logger->debug2("Using HTTP client for secondary port %d\n", port);
        secondary_network_client_ = make_shared<HttpNetworkClient>();
        secondary_batcher_ = make_shared<HTTPMessageBatcher>(
            config_.getMaxBatchCount(),
            config_.getMaxBatchAge()
        );
    }
        
    logger->debug2("Service::run()> initializing secondary_network_client\n");

    // Create URL with /incoming path for sending events
    wstring url = config_.getSecondaryHost() + SharedConstants::HTTP_API_PATH;
    if (!secondary_network_client_->initialize(&config_, 
        config_.getSecondaryApiKey().c_str(),
        url.c_str(),
        config_.getSecondaryUseTls(),
        config_.getSecondaryPort())) {
        logger->fatal("Failed to initialize secondary network client\n");
        return false;
    }

    logger->debug2("Service::run()> connecting secondary_network_client\n");
    if (!secondary_network_client_->connect()) {
        logger->fatal("Failed to connect secondary network client\n");
        return false;
    }

    return true;
}

bool Service::initializePrimaryCertificate() {
    auto logger = LOG_THIS;
    if (config_.getPrimaryUseTls()) {
        wstring primary_cert_path = Util::getThisPath(true) 
            + Configuration::PRIMARY_CERT_FILENAME;
        logger->info("Service::onStart()> using primary cert path %ls\n",
            primary_cert_path.c_str());

        // Only HTTP clients support certificates
        if (auto http_client = std::dynamic_pointer_cast<HttpNetworkClient>(primary_network_client_)) {
            if (!http_client->loadCertificate(primary_cert_path.c_str())) {
                logger->fatal("Could not read primary cert from %ls\n", 
                    primary_cert_path.c_str());
                return false;
            }
        } else {
            logger->fatal("TLS requested but client does not support certificates\n");
            return false;
        }
    }
    return true;
}

bool Service::getAndSetLogZillaVersion(const shared_ptr<INetworkClient>& client, bool isPrimary) {
    auto logger = LOG_THIS;
    // Use stack buffer for version and avoid string allocations
    char version_buffer[256];
    size_t bytes_written = 0;

    logger->info("Service::run()> getting %s LogZilla version...\n", isPrimary ? "primary" : "secondary");
    if (!client->getLogzillaVersion(version_buffer, sizeof(version_buffer), bytes_written)) {
        logger->fatal("Could not get %s LogZilla version\n", isPrimary ? "primary" : "secondary");
        return false;
    }

    // Ensure null termination for safe string operations
    if (bytes_written >= sizeof(version_buffer)) {
        bytes_written = sizeof(version_buffer) - 1;
    }
    version_buffer[bytes_written] = '\0';

    logger->info("LogZilla version %s\n", version_buffer);
    
    // Skip 'v' prefix if present
    const char* version_str = version_buffer;
    if (version_buffer[0] == 'v') {
        version_str++;
    }

    if (isPrimary) {
        config_.setPrimaryLogzillaVersion(version_str);
    } else {
        config_.setSecondaryLogzillaVersion(version_str);
    }

    return true;
}

void Service::initializeEventLogSubscriptions(const vector<LogConfiguration>& logs) {
    auto logger = LOG_THIS;
    try {
        for (auto& log : logs) {
            // Validate log name before conversion
            if (log.name_.empty()) {
                logger->fatal("Invalid event log configuration: empty log name\n");
                throw std::runtime_error("Empty log name");
            }

            // Use a larger buffer for conversion
            char log_name_buf[4096];
            size_t bytes_written = Util::wstr2str(log_name_buf, sizeof(log_name_buf), log.name_.c_str());
            
            if (bytes_written == 0) {
                logger->fatal("Failed to convert log name to UTF-8: %s\n", 
                    GetLastError() == ERROR_INSUFFICIENT_BUFFER ? "buffer too small" : "conversion error");
                throw std::runtime_error("Failed to convert log name to UTF-8");
            }

            unique_ptr<EventHandlerMessageQueuer> handler
                = make_unique<EventHandlerMessageQueuer>(
                    config_,
                    primary_message_queue_,
                    secondary_message_queue_,
                    const_cast<const wchar_t*>(log.name_.c_str()));

            // Get bookmark - try registry first, then config, then use empty string
            wstring bookmark = Registry::readBookmark(log.channel_.c_str());
            if (bookmark.empty() && !config_.getOnlyWhileRunning()) {
                bookmark = log.bookmark_;
            }

            const wstring query(L"*");
            const wstring log_name(log.name_);
            const wstring log_channel(log.channel_);
            EventLogSubscription subscription(
                log_name,
                log_channel,
                query,
                bookmark,
                config_.getOnlyWhileRunning(),
                std::move(handler));

            subscriptions_.push_back(std::move(subscription));
            try {
                // Subscribe with the appropriate flag based on whether we have a bookmark
                subscriptions_.back().subscribe(bookmark, config_.getOnlyWhileRunning());
            }
            catch (const ::std::exception& e) {
                logger->recoverable_error("Failed to subscribe to event log '%s': %s\n", 
                    log_name_buf, e.what());
            }
        }
    }
    catch (const ::std::exception& e) {
        logger->fatal("Error initializing event log subscriptions: %s\n", e.what());
        throw;
    }
}

void Service::handleQueueStatusAndConfig() {
    auto logger = LOG_THIS;
    if (primary_message_queue_->length() > 0) {
        logger->debug("Primary Queue length==%d\n", primary_message_queue_->length());
    }

    if (config_.getUseLogAgent() && primary_message_queue_->isEmpty() 
        && (secondary_message_queue_ == nullptr || secondary_message_queue_->isEmpty())) {
        // logger->debug3("Saving config to registry\n");
        config_.saveToRegistry();
    }
}

void Service::mainLoop(bool running_as_console, bool& first_loop, int& restart_needed)
{
    auto logger = LOG_THIS;
    logger->debug2("Service::mainLoop()> Starting main loop\n");

    int loop_count = 0;
    auto rateCheckStart = std::chrono::steady_clock::now();

    while (!checkForShutdown(running_as_console, restart_needed)) {
        try {
            if (first_loop) {
                first_loop = false;
                logger->info("Service::mainLoop()> Service is running\n");
            }

            handleQueueStatusAndConfig();
            Sleep(100);  // Small sleep to prevent tight loop
            if (++loop_count % 10 == 0) {
                for (auto& subscription : subscriptions_)
                    subscription.saveBookmark();
            }
            if (loop_count >= 100) {
                logger->debug("Service::mainLoop()> heartbeat: 100 loops\n");
                loop_count = 0;
            }
#if ONLY_FOR_DEBUGGING_CURRENTLY_DISABLED
            if (std::chrono::steady_clock::now() - rateCheckStart >= std::chrono::seconds(Service::RATE_CHECK_INTERVAL_SEC)) {
				rateCheckStart = std::chrono::steady_clock::now();
                auto& metrics = SlidingWindowMetrics::instance();
                double inRate = metrics.incomingRate();
                double outRate = metrics.outgoingRate();
				if (metrics.checkRates(Service::RATE_THRESHOLD_RATIO)) {
					logger->debug("SlidingWindowMetrics: Incoming rate %.2f events/s exceeds outgoing rate %.2f events/s (threshold ratio %.2f)",
						inRate, outRate, Service::RATE_THRESHOLD_RATIO);
				}
				else {
					logger->debug("SlidingWindowMetrics: Incoming rate %.2f events/s, outgoing rate %.2f events (%.2f)/s",
                        inRate, outRate, inRate / outRate);
                }
			}
#endif
        }
        catch (const std::exception& e) {
            logger->recoverable_error("Service::mainLoop()> Exception: %s\n", e.what());
        }
    }
}

bool Service::checkForShutdown(bool running_as_console, int& restart_needed) {
    auto logger = LOG_THIS;
    // Check if shutdown was requested through service control
    if (g_StopEvent && WaitForSingleObject(g_StopEvent, 0) == WAIT_OBJECT_0) {
        shutdown_requested_ = true;
        service_shutdown_requested_ = true;
        return true;
    }

    // Check other shutdown conditions
    if (shutdown_requested_) {
        return true;
    }

    if (restart_needed || (running_as_console && _kbhit())) {
        if (restart_needed) {
            logger->debug("restart needed\n");
        }
        else {
            logger->debug("key hit\n");
        }
        shutdown_requested_ = true;
        return true;
    }

    return false;
}

void Service::cleanupAndShutdown(bool running_as_console, int restart_needed) {
    auto logger = LOG_THIS;
    if (restart_needed) {
        logger->info("Restarting service...\n");
    }
    else {
        logger->info("Shutting down service...\n");
    }

    try {
        // First signal queues to stop accepting new messages
        if (primary_message_queue_) {
            primary_message_queue_->beginShutdown();
        }
        if (secondary_message_queue_) {
            secondary_message_queue_->beginShutdown();
        }

        // Close all subscriptions to stop incoming messages
        for (auto& subscription : subscriptions_) {
            subscription.cancelSubscription();
        }
        subscriptions_.clear();

        // Clean up file watcher if active
        if (filewatcher_) {
            filewatcher_.reset();
        }

        // Close network connections before waiting for send thread
        cleanupNetworkClient(primary_network_client_);
        cleanupNetworkClient(secondary_network_client_);

        Service::sender_->requestStop();

        // Now wait for send thread to complete
        if (send_thread_ && send_thread_->joinable()) {
            logger->debug2("Service::cleanupAndShutdown()> Waiting for send thread to complete\n");
            send_thread_->join();
        }

        // Finally cleanup message queues
        cleanupMessageQueue(primary_message_queue_);
        cleanupMessageQueue(secondary_message_queue_);

        // Signal that shutdown is complete
        if (g_ShutdownCompleteEvent) {
            SetEvent(g_ShutdownCompleteEvent);
        }

        // Cleanup event handles
        if (g_StopEvent) {
            CloseHandle(g_StopEvent);
            g_StopEvent = nullptr;
        }
        if (g_ShutdownCompleteEvent) {
            CloseHandle(g_ShutdownCompleteEvent);
            g_ShutdownCompleteEvent = nullptr;
        }

        if (!running_as_console) {
            // Update service status
            if (service_status_handle_) {
                SERVICE_STATUS status = { 0 };
                status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
                status.dwCurrentState = SERVICE_STOPPED;
                status.dwControlsAccepted = 0;
                status.dwWin32ExitCode = restart_needed ? ERROR_SUCCESS_REBOOT_REQUIRED : NO_ERROR;
                status.dwServiceSpecificExitCode = 0;
                status.dwCheckPoint = 0;
                status.dwWaitHint = 0;

                if (!SetServiceStatus(service_status_handle_, &status)) {
                    logger->recoverable_error("SetServiceStatus failed %d\n", GetLastError());
                }
            }
        }

        if (restart_needed) {
            logger->info("Service restart complete\n");
        }
        else {
            logger->info("Service shutdown complete\n");
        }
        logger->debug2("Service::cleanupAndShutdown()> complete\n");
    }
    catch (const std::exception& e) {
        logger->fatal("Exception during cleanup: %s\n", e.what());
    }
}

void Service::shutdown() {
    auto logger = LOG_THIS;
    logger->info("Service shutdown requested\n");
    service_shutdown_requested_ = true;
    shutdown_requested_ = true;
    shutdown_event_.signal();
}

void Service::fatalErrorHandler(const char* msg) {
    auto logger = LOG_THIS;
    shutdown_requested_ = true;
    shutdown_event_.signal();
}

} // namespace Syslog_agent