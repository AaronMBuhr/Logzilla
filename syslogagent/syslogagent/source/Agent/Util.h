/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cctype>
#include <string>
#include <vector>
#include <wchar.h>

namespace Syslog_agent {

class Util {
public:
	static void toPrintableAscii(char* destination, int destination_count, 
		const wchar_t* source, char space_replacement);
	static size_t wstr2str(char* dest, size_t dest_size, const wchar_t* src);
	static size_t wstr2str_truncate(char* dest, size_t dest_size, const wchar_t* src);
	static size_t toLowercase(wchar_t* str);
	static size_t toLowercase(char* str);
	static std::wstring toLowercase(const std::wstring& str);
	static std::wstring getThisPath();
	static std::wstring getThisPath(bool with_trailing_backslash);
	static std::string readFileAsString(const char* filename);
	static std::string readFileAsString(const wchar_t* filename);
	static void replaceAll(std::string& str, const std::string& from, const std::string& to);
	static size_t hashWstring(const std::wstring& _Keyval);
	static int jsonEscape(char* input_buffer, char* output_buffer, int output_buffer_length);
	// Returns number of chars written to output buffer (not including null terminator)
	static size_t jsonEscapeString(const char* input, char* output_buffer, size_t output_buffer_size);
	static bool copyFile(const wchar_t* const source_filename, const wchar_t* const dest_filename);
	static int64_t getUnixTimeMilliseconds();
	static int compareSoftwareVersions(const std::string& version_a, const std::string& version_b);
	static std::vector<int> splitVersion(const std::string& version);

};

}
