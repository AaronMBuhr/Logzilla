#pragma once
#include <stdarg.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "WindowsEvent.h"

using namespace std;

class HeartbeatHeart {
public:
	HeartbeatHeart(const char* heart_name, int num_beats_to_track);
	const char* getName() { return heart_name_; }
	void beat();
	uint64_t millisecondsSinceLastBeat();

private:
	const char* heart_name_;
	int num_beats_to_track_;
	uint64_t last_heartbeat_msec_;
	unique_ptr<uint64_t[]> heartbeat_times_;
	int current_time_idx_;

	uint64_t currentTimeMilliseconds();
};

class Heartbeat {
public:
	Heartbeat(const int monitor_tick_seconds, const int heartbeat_failed_seconds) : 
		monitor_tick_seconds_(monitor_tick_seconds),
		heartbeat_failed_seconds_(heartbeat_failed_seconds),
		stop_requested_(false),
		stopped_(false),
		heartbeat_failure_function_(nullptr)
	{}
	~Heartbeat();
	void addHeart(const char* heart_name);
	void heartbeat(const char* which_heart_name);
	void registerHeartbeatFailure(void (*failure_function)(const char*)) { heartbeat_failure_function_ = failure_function; }
	void startMonitor();
	void stopMonitor();
	void waitForEnd();

	const int NUM_BEATS_TO_TRACK = 10;
	const double BEATPERIOD_MULTIPLIER_TO_FAILURE = 4.0;

private:
	int monitor_tick_seconds_;
	int heartbeat_failed_seconds_;
	vector<shared_ptr<HeartbeatHeart>> hearts_;
	void (*heartbeat_failure_function_)(const char*);
	unique_ptr<thread> monitor_thread_;
	WindowsEvent stop_event_{ L"LogZilla_SyslogAgent_Heartbeat" };
	bool stop_requested_;
	bool stopped_;

	HeartbeatHeart* getHeart(const char* heart_name);
	void monitorTickFunction();
	void monitorThread();

	friend void monitorThreadStart(Heartbeat* heartbeat);
};

