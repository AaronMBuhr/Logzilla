#pragma once
#include <stdarg.h>
#include <mutex>
#include <vector>

using namespace std;


// TODO add unicode support

#define LOG_CURRENT_LOCATION(p) Logger::debug( "Debug marker (%s) - ::%s() in file: %s(%d)\n", p, __func__ , __FILE__, __LINE__ )
class Logger
{
public:
	static const int MAX_LOGMSG_LENGTH = 16384;
	static const wstring DEFAULT_LOG_FILENAME;
	enum LogLevel { DEBUG3, DEBUG2, DEBUG, VERBOSE, INFO, WARN, RECOVERABLE_ERROR, CRITICAL, FATAL, NOLOG, ALWAYS, FORCE };
	static char const* LOGLEVEL_ABBREVS[12];
	static char* LOGLEVEL_ABBREVS_WITHBRACKET[12];
	enum LogDestination { DEST_CONSOLE, DEST_FILE, DEST_CONSOLE_AND_FILE };

	static void setLogLevel(const LogLevel log_level);
	static LogLevel getLogLevel();
	static LogDestination getlogDestination() { return singleton()->log_destination_; }
	static void setLogDestination(LogDestination log_destination);
	static void setLogFile(const wstring const_log_path_and_filename);
	static bool log(const LogLevel log_level, const char* format, ...);
	static bool log_no_datetime(const LogLevel log_level, const char* format, ...);
	static void setLogEventsToFile() { singleton()->log_events_to_file_ = true; }
	static bool getLogEventsToFile() { return singleton()->log_events_to_file_; }
	static void logEventToFile(string event_message);
	// returns false if there was an error logging
	static void getDateTimeStr(char* buf, int bufsize);
	static bool isUnittestRunning();
	static string getUnitTestLog();
	static int writeToFile(const char *filename, bool append, const char* format, ...);

	template<typename... _args> static bool debug3(const char* format, _args... args) {
		return log(DEBUG3, format, args...);
	}
	template<typename... _args> static bool debug2(const char* format, _args... args) {
		return log(DEBUG2, format, args...);
	}
	template<typename... _args> static bool debug(const char* format, _args... args) {
		return log(DEBUG, format, args...);
	}
	template<typename... _args> static bool verbose(const char* format, _args... args) {
		return log(VERBOSE, format, args...);
	}
	template<typename... _args> static bool info(const char* format, _args... args) {
		return log(INFO, format, args...);
	}
	template<typename... _args> static bool warn(const char* format, _args... args) {
		return log(WARN, format, args...);
	}
	template<typename... _args> static bool recoverable_error(const char* format, _args... args) {
		return log(RECOVERABLE_ERROR, format, args...);
	}
	template<typename... _args> static bool critical(const char* format, _args... args) {
		return log(CRITICAL, format, args...);
	}
	template<typename... _args> static bool fatal(const char* format, _args... args) {
		return log(FATAL, format, args...);
	}
	template<typename... _args> static bool always(const char* format, _args... args) {
		return log(ALWAYS, format, args...);
	}
	template<typename... _args> static bool force(const char* format, _args... args) {
		return log(FORCE, format, args...);
	}


#if _UNITTEST
	static void logUnittest(const char* format, ...) {
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
		singleton()->unit_test_messages_.push_back(string(buf));
	}
#else
	static inline void logUnittest(const char* format, ...) { }
#endif

#if RELEASE
	static void breakPoint() { }
#else
	static void breakPoint() { __debugbreak(); }
#endif


private:
	static const string EVENTS_FILE_PATH_AND_FILENAME;
	Logger();
	static Logger* singleton_;
	LogLevel current_log_level_ = NOLOG;
	LogDestination log_destination_ = DEST_CONSOLE;
	mutex logger_lock_;
	char log_message_buffer_[MAX_LOGMSG_LENGTH];
	wstring log_path_and_filename_;
	bool log_events_to_file_;
	short is_unittest_running_;
	vector<string> unit_test_messages_;

	static Logger* singleton();
	bool logToConsole(const char* log_message_cstring);
	bool logToFile(const char* log_message_cstring);
	bool logToConsoleAndFile(const char* log_message_cstring);
};

