#pragma once
#include "Heartbeat.h"

#define DEBUG_SETTINGS_SKIP_TIMESTAMP false
#define DEBUG_SETTINGS_SKIP_MESSAGEQUEUE false

class Debug
{
public:
	static const int HEARTBEAT_TICK_SECONDS = 11;
	static const int HEARTBEAT_FAILED_SECONDS = 45;

	static void heartbeatFailure(const char* heartname);
	static void startHeartbeatMonitoring();
	static void stopHeartbeatMonitoring();

	static void senderHeartbeat();
	static void readerHeartbeat();

private:
	static constexpr const char* SENDER_HEARTNAME = "HeartSyslogSender";
	static constexpr const char* READER_HEARTNAME = "HeartEventReader";
	Debug();
	static const Debug* singleton();
	static Debug* singleton_;
	unique_ptr<Heartbeat> heartbeat_;
};

