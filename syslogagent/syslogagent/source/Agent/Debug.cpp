#include "stdafx.h"
#include "Debug.h"
#include "Logger.h"

Debug* Debug::singleton_ = nullptr;

const Debug* Debug::singleton() {
	if (singleton_ == nullptr) {
		singleton_ = new Debug();
	}
	return singleton_;
}

Debug::Debug() {
	Logger::debug("Debug::Debug() making hearts for sender and reader\n");
	heartbeat_ = make_unique<Heartbeat>(HEARTBEAT_TICK_SECONDS, HEARTBEAT_FAILED_SECONDS);
	heartbeat_->addHeart(SENDER_HEARTNAME);
	heartbeat_->addHeart(READER_HEARTNAME);
	heartbeat_->registerHeartbeatFailure(heartbeatFailure);
}

void Debug::startHeartbeatMonitoring() {
	Logger::debug("Debug::Debug() starting heartbeat monitoring\n");
	singleton()->heartbeat_->startMonitor();
}

void Debug::stopHeartbeatMonitoring() {
	Logger::debug("Debug::Debug() stopping heartbeat monitoring\n");
	singleton()->heartbeat_->stopMonitor();
}

void Debug::senderHeartbeat() {
	Logger::debug2("Debug::Debug() sender heart beating\n");
	singleton()->heartbeat_->heartbeat(SENDER_HEARTNAME);
}

void Debug::readerHeartbeat() {
	Logger::debug2("Debug::Debug() reader heart beating\n");
	singleton()->heartbeat_->heartbeat(READER_HEARTNAME);
}

void Debug::heartbeatFailure(const char* heartname) {
	Logger::fatal("Debug::Debug() %s heart has stopped\n", heartname);
	Logger::fatal("Exiting with error code 1\n", heartname);
	exit(1);
}
