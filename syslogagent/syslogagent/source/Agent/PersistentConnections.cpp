#include "stdafx.h"
#include "synchapi.h"
#include "PersistentConnections.h"
#include "Logger.h"
#include "SyslogSender.h"

void connectThreadStart(PersistentConnections* pers_connections) {
	pers_connections->connectThread();
}


PersistentConnections::PersistentConnections(vector<shared_ptr<NetworkClient>>& network_clients) :
stop_requested_(false),
connection_thread_(nullptr),
msec_between_retries_(0),
network_clients_(network_clients)
{ }


bool PersistentConnections::start(int msec_between_retries) {
    auto logger = LOG_THIS;
    logger->debug2("PersistentConnections::start() starting\n");
    if (connection_thread_ != nullptr) {
        logger->debug("PersistentConnections::start() attempted to start already started thread\n"); 
        return false;
    }
    msec_between_retries_ = msec_between_retries;
    connection_thread_ = make_unique<thread>(connectThreadStart, this);
    return true;
}

bool PersistentConnections::stop() {
    auto logger = LOG_THIS;
    logger->debug2("PersistentConnections::stop() stopping\n");
    stop_requested_ = true;
    stop_event_.signal();
    return true;
}

void PersistentConnections::connectThread() {
    auto logger = LOG_THIS;
    logger->debug2("PersistentConnections::connectThread() starting\n");
    while (!stop_requested_) {
        bool connected = false;
        for (auto& client_ptr : network_clients_) {
            if (!client_ptr->isConnected()) {
                logger->debug2("PersistentConnections::connectThread(): connection %s is disconnected, attempting reconnect\n", 
                    client_ptr->connectionNameUtf8().c_str());
                int connection_result = client_ptr->connect();
                if (connection_result) {
                    logger->debug("PersistentConnections::connectThread(): connection to %s failed with result %d\n", 
                        client_ptr->connectionNameUtf8().c_str(), connection_result);
                }
                else {
                    logger->debug("PersistentConnections::connectThread(): reconnected to %s\n", 
                        client_ptr->connectionNameUtf8().c_str());
                    connected = true;
                    Syslog_agent::SyslogSender::enqueue_event_.signal();
                }
            }
        }
        stop_event_.wait(msec_between_retries_);
    }
    logger->debug2("PersistentConnections::connectThread() ending\n");
}

void PersistentConnections::waitForEnd() {
    auto logger = LOG_THIS;
    logger->debug2("PersistentConnections::waitForEnd() waiting\n");
    if (connection_thread_) {
        connection_thread_->join();
        connection_thread_ = nullptr;
    }
    logger->debug2("PersistentConnections::waitForEnd() done waiting\n");
}


PersistentConnections::~PersistentConnections() {
    if (!stop_requested_) {
        stop();
    }
    waitForEnd();
}