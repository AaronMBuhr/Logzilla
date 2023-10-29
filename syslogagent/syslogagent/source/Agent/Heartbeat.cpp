#include "stdafx.h"
#include <chrono>
#include "Logger.h"
#include "Heartbeat.h"

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

HeartbeatHeart::HeartbeatHeart(const char* heart_name, int num_beats_to_track) : 
	heart_name_(heart_name),
	num_beats_to_track_(num_beats_to_track),
	last_heartbeat_msec_(0),
	current_time_idx_(-1) {
	heartbeat_times_ = make_unique<uint64_t[]>(num_beats_to_track);
	ZeroMemory(heartbeat_times_.get(), sizeof(heartbeat_times_.get()) * num_beats_to_track);
}

uint64_t HeartbeatHeart::currentTimeMilliseconds() {
	return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

void HeartbeatHeart::beat() {
	uint64_t current_time = currentTimeMilliseconds();
	Logger::logUnittest("HeartbeatHeart::beat()> #1 current time==%llu, last_heartbeat_msec_==%llu\n", (unsigned long long) current_time, (unsigned long long) last_heartbeat_msec_);
	if (last_heartbeat_msec_ != 0) {
		uint64_t period_msec = current_time - last_heartbeat_msec_;
		current_time_idx_ = (current_time_idx_ + 1) % num_beats_to_track_;
		heartbeat_times_[current_time_idx_] = period_msec;
		Logger::logUnittest("HeartbeatHeart::beat()> #2 period_msec==%llu\n", (unsigned long long) period_msec);
	}
	last_heartbeat_msec_ = current_time;
}


uint64_t HeartbeatHeart::millisecondsSinceLastBeat() {
	if (last_heartbeat_msec_ == 0) {
		return 0;
	}
	return currentTimeMilliseconds() - last_heartbeat_msec_;
}



void monitorThreadStart(Heartbeat* heartbeat) {
	heartbeat->monitorThread();
}

void Heartbeat::addHeart(const char* heart_name) {
	shared_ptr<HeartbeatHeart> heart_ptr = make_shared<HeartbeatHeart>(heart_name, NUM_BEATS_TO_TRACK);
	hearts_.push_back(heart_ptr);
}

HeartbeatHeart* Heartbeat::getHeart(const char* heart_name) {
	bool found = false;
	int i;
	for (i = 0; i < hearts_.size(); ++i) {
		if (!strcmp(hearts_[i]->getName(), heart_name)) {
			found = true;
			break;
		}
	}
	return (!found ? nullptr : hearts_[i].get());
}

void Heartbeat::heartbeat(const char* which_heart_name) {
	getHeart(which_heart_name)->beat();
}

void Heartbeat::monitorTickFunction() {
#ifdef _UNITTEST
	Logger::logUnittest("in monitorTickFunction()\n");
#endif

	for (auto& heart : hearts_) {
#ifdef _UNITTEST
		//printf("beat due multiplier == %d\n", beat_due_multiplier);
		Logger::logUnittest("beat due multiplier == %f\n", beat_due_multiplier);
#endif
		auto msecSinceLastBeat = heart->millisecondsSinceLastBeat();
		if ((msecSinceLastBeat != 0) && (msecSinceLastBeat / 1000 > heartbeat_failed_seconds_)) {
			heartbeat_failure_function_(heart->getName());
		}
	}

}

void Heartbeat::monitorThread() {
	while (!stop_requested_) {
		monitorTickFunction();
		stop_event_.wait(monitor_tick_seconds_ * 1000);
	}
	stopped_ = true;
}

void Heartbeat::startMonitor() {
	if (monitor_thread_ != nullptr) {
		return;
	}
	monitor_thread_ = make_unique<thread>(monitorThreadStart, this);
	stopped_ = false;
	stop_requested_ = false;
}

void Heartbeat::stopMonitor() {
	stop_requested_ = true;
	stop_event_.signal();
}

void Heartbeat::waitForEnd() {
	if (!stopped_)
	{
		stop_requested_ = true;
		monitor_thread_->join();
	}
	monitor_thread_.reset();
}

Heartbeat::~Heartbeat() {
	stopMonitor();
	monitor_thread_.reset();
}