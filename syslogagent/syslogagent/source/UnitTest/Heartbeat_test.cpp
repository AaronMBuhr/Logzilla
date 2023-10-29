#include "stdafx.h"
#include <chrono>
#include <thread>
#include "CppUnitTest.h"
#include "Heartbeat.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

static void dieFunction(char* heartname);

TEST_CLASS(Heartbeat_test) {

	TEST_METHOD(singleheart_quick_test) {
		
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

	}

public:
	static void testlogMessage(const char* format, ...) {
		char buf[4096];

		va_list args;
		va_start(args, format);
		int num_chars = vsnprintf_s(
			buf,
			sizeof(buf),
			_TRUNCATE,
			format,
			args
		);
		va_end(args);
		Logger::WriteMessage(buf);
	}

};

static void dieFunction(char* heartname) {
	Heartbeat_test::testlogMessage("heart %s died\n", heartname);
}

