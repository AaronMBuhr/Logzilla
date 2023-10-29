#include "pch.h"
#include <chrono>
#include <string>
#include <thread>
#include "../source/Agent/Logger.h"
#include "../source/Agent/Heartbeat.h"

static void logMessage(const char* msg) {
	cout << msg << std::endl;
}
static void dieFunction(const char* heartname) {
	cout << Logger::getUnitTestLog();
	cout << "heart " << heartname << " has died" << std::endl;
}

TEST(HeartbeatTests, HeartbeatBasicTestSuccess) {
	int monitor_tick_seconds = 2;
	unique_ptr<Heartbeat> beat_ptr = make_unique<Heartbeat>(monitor_tick_seconds);
	beat_ptr->addHeart("test heart #1");
	beat_ptr->registerHeartbeatFailure(dieFunction);
	beat_ptr->startMonitor();

	std::this_thread::sleep_for(std::chrono::seconds(3));
	beat_ptr->heartbeat("test heart #1");
	std::this_thread::sleep_for(std::chrono::seconds(3));
	beat_ptr->heartbeat("test heart #1");
	std::this_thread::sleep_for(std::chrono::seconds(3));
	beat_ptr->heartbeat("test heart #1");
	std::this_thread::sleep_for(std::chrono::seconds(3));
	beat_ptr->heartbeat("test heart #1");

	std::this_thread::sleep_for(std::chrono::seconds(10));
	cout << Logger::getUnitTestLog();
	cout << "done with sleeping" << std::endl;
	beat_ptr->waitForEnd();
	FAIL();
}

TEST(HeartbeatTests, HeartbeatBasicTestFail) {
	int monitor_tick_seconds = 2;
	unique_ptr<Heartbeat> beat_ptr = make_unique<Heartbeat>(monitor_tick_seconds);
	beat_ptr->addHeart("test heart #1");
	beat_ptr->registerHeartbeatFailure(dieFunction);
	beat_ptr->startMonitor();

	std::this_thread::sleep_for(std::chrono::seconds(1));
	beat_ptr->heartbeat("test heart #1");
	std::this_thread::sleep_for(std::chrono::seconds(1));
	beat_ptr->heartbeat("test heart #1");
	std::this_thread::sleep_for(std::chrono::seconds(1));
	beat_ptr->heartbeat("test heart #1");
	std::this_thread::sleep_for(std::chrono::seconds(1));
	beat_ptr->heartbeat("test heart #1");

	std::this_thread::sleep_for(std::chrono::seconds(10));
	cout << "done with sleeping" << std::endl;
	beat_ptr->waitForEnd();
	FAIL();
}
