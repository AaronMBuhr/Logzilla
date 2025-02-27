/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "pch.h"
#include <algorithm>
#include <cctype>
#include <clocale>
#include <codecvt>
#include <fstream>
#include <iostream>
#include <locale>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <tlhelp32.h>
#include <vector>
#include <windows.h>
#include <Psapi.h>
#include <TlHelp32.h>
#include "Util.h"
#include "Logger.h"
#include <winhttp.h>


using namespace std;

void Util::toPrintableAscii(char* destination, int destination_count,
    const wchar_t* source, char space_replacement) {
    auto logger = LOG_THIS;

    int i;
    for (i = 0; i < destination_count - 1; i++) {
        if (source[i] == 0) break;
        if (source[i] < 32 || source[i] > 126) destination[i] = '?';
        else if (source[i] == 32) destination[i] = space_replacement;
        else destination[i] = static_cast<char>(source[i]);
    }
    destination[i] = 0;
}

size_t Util::wstr2str(char* dest, size_t dest_size, const wchar_t* src)
{
    if (!dest || !src || dest_size == 0) return 0;

    size_t converted = 0;
    if (WideCharToMultiByte(CP_UTF8, 0, src, -1, dest, static_cast<int>(dest_size), NULL, NULL)) {
        converted = strlen(dest);
    }
    dest[dest_size - 1] = '\0';  // Ensure null termination
    return converted;
}

size_t Util::wstr2str_truncate(char* dest, size_t dest_size, const wchar_t* src)
{
    if (!dest || !src || dest_size == 0) return 0;

    size_t converted = wstr2str(dest, dest_size, src);
    if (converted >= dest_size - 1) {
        dest[dest_size - 1] = '\0';
        return dest_size - 1;
    }
    return converted;
}

size_t Util::toLowercase(wchar_t* str) {
    if (!str) return 0;

    size_t count = 0;
    while (str[count]) {
        str[count] = towlower(str[count]);
        ++count;
    }
    return count;
}

size_t Util::toLowercase(char* str) {
    size_t i;
    for (i = 0; str[i] != 0; i++) {
        str[i] = tolower(str[i]);
    }
    return i;
}

std::wstring Util::toLowercase(const std::wstring& str) {
    std::wstring result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

wstring Util::getThisPath()
{
    return getThisPath(false);
}

wstring Util::getThisPath(bool with_trailing_backslash)
{

    TCHAR buf[1024];
    DWORD length = GetModuleFileName(NULL, buf, 1024);
    // link to this library fails...
    //#if (NTDDI_VERSION >= NTDDI_WIN8)
    //    PathCchRemoveFileSpec(dest, destSize);
    //#else
    //    if (MAX_PATH > destSize) return NULL;
    //    PathRemoveFileSpec(dest);
    //#endif
    // so we do it this way:
    wstring module_filename_wstr = wstring(buf, length);
    size_t last_pos = module_filename_wstr.find_last_of(L'\\');
    if (last_pos == string::npos || last_pos < 1) {
        return wstring();
    }
    return module_filename_wstr.substr(0, last_pos)
        + (with_trailing_backslash ? L"\\" : L"");
}

bool Util::getThisPath(wchar_t* buffer, size_t buffer_size, bool with_trailing_backslash) {
    if (!buffer || buffer_size < MAX_PATH) return false;

    DWORD result = GetModuleFileNameW(NULL, buffer, static_cast<DWORD>(buffer_size));
    if (result == 0 || result >= buffer_size) return false;

    // Find last backslash
    wchar_t* last_slash = wcsrchr(buffer, L'\\');
    if (!last_slash) return false;

    // Null terminate after the last slash to get the directory
    *(last_slash + 1) = L'\0';

    // Add trailing backslash if requested and not already present
    if (with_trailing_backslash && last_slash[0] != L'\\') {
        if (wcslen(buffer) + 2 > buffer_size) return false;  // +2 for \ and null terminator
        wcscat_s(buffer, buffer_size, L"\\");
    }

    return true;
}

string Util::readFileAsString(const char* filename) {
    ifstream infile(filename);
    if (!infile) {
        return string();
    }
    stringstream buffer;
    buffer << infile.rdbuf();
    return buffer.str();
}

string Util::readFileAsString(const wchar_t* filename) {
    FILE* infile;
    _wfopen_s(&infile, filename, L"r");
    if (!infile) {
        return string();
    }

    fseek(infile, 0, SEEK_END);
    int64_t fsize = ftell(infile);
    fseek(infile, 0, SEEK_SET);
    vector<char> contents(fsize + 1);
    fread(contents.data(), 1, fsize, infile);
    fclose(infile);
    contents[fsize] = 0;
    return string(contents.data(), fsize);
}

void Util::replaceAll(std::string& str, const std::string& from,
    const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
        // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

size_t Util::hashWstring(const std::wstring& _Keyval)
{    // hash _Keyval to size_t value by pseudorandomizing transform
    size_t _Val = 2166136261U;
    size_t _First = 0;
    size_t _Last = _Keyval.size();
    size_t _Stride = 1 + _Last / 10;

    if (_Stride < _Last)
        _Last -= _Stride;
    for (; _First < _Last; _First += _Stride)
        _Val = 16777619U * _Val ^ (size_t)_Keyval[_First];
    return (_Val);
}

int Util::jsonEscape(char* input_buffer, char* output_buffer,
    int output_buffer_length) {
    int output_pos = 0;
    for (int i = 0;
        output_pos < output_buffer_length - 1 && input_buffer[i] != 0;
        ++i) {
        unsigned char cur_char = static_cast<unsigned char>(input_buffer[i]);

        // Handle control characters (0x00-0x1F)
        if (cur_char < 0x20) {
            if (output_pos + 6 >= output_buffer_length - 1) break;  // Need room for \u00XX

            // Special handling for common control chars
            switch (cur_char) {
            case '\b': // backspace
                output_buffer[output_pos++] = '\\';
                output_buffer[output_pos++] = 'b';
                break;
            case '\f': // form feed
                output_buffer[output_pos++] = '\\';
                output_buffer[output_pos++] = 'f';
                break;
            case '\n': // newline
                output_buffer[output_pos++] = '\\';
                output_buffer[output_pos++] = 'n';
                break;
            case '\r': // carriage return
                output_buffer[output_pos++] = '\\';
                output_buffer[output_pos++] = 'r';
                break;
            case '\t': // tab
                output_buffer[output_pos++] = '\\';
                output_buffer[output_pos++] = 't';
                break;
            default:
                // Use \u00XX format for other control chars
                output_buffer[output_pos++] = '\\';
                output_buffer[output_pos++] = 'u';
                output_buffer[output_pos++] = '0';
                output_buffer[output_pos++] = '0';
                output_buffer[output_pos++] = "0123456789ABCDEF"[(cur_char >> 4) & 0x0F];
                output_buffer[output_pos++] = "0123456789ABCDEF"[cur_char & 0x0F];
                break;
            }
        }
        else if (cur_char == '"' || cur_char == '\\') {
            // Quote and backslash need escaping
            if (output_pos + 2 >= output_buffer_length - 1) break;
            output_buffer[output_pos++] = '\\';
            output_buffer[output_pos++] = cur_char;
        }
        else if (cur_char >= 0x20 && cur_char <= 0x7F) {
            // Printable ASCII
            if (output_pos + 1 >= output_buffer_length - 1) break;
            output_buffer[output_pos++] = cur_char;
        }
        else {
            // Non-ASCII characters get \u escaping
            if (output_pos + 6 >= output_buffer_length - 1) break;
            output_buffer[output_pos++] = '\\';
            output_buffer[output_pos++] = 'u';
            output_buffer[output_pos++] = '0';
            output_buffer[output_pos++] = '0';
            output_buffer[output_pos++] = "0123456789ABCDEF"[(cur_char >> 4) & 0x0F];
            output_buffer[output_pos++] = "0123456789ABCDEF"[cur_char & 0x0F];
        }
    }
    output_buffer[output_pos] = 0;
    return output_pos + 1;
}

size_t Util::jsonEscapeString(const char* input, char* output_buffer, size_t output_buffer_size) {
    if (!input || !output_buffer || output_buffer_size == 0) {
        if (output_buffer && output_buffer_size > 0) output_buffer[0] = '\0';
        return 0;
    }

    size_t input_len = strlen(input);
    if (input_len == 0) {
        output_buffer[0] = '\0';
        return 0;
    }

    // Call jsonEscape which already handles the escaping logic
    int result = jsonEscape(const_cast<char*>(input), output_buffer, static_cast<int>(output_buffer_size));
    return result > 0 ? static_cast<size_t>(result - 1) : 0;  // -1 to not count null terminator
}

bool Util::copyFile(const wchar_t* const source_filename, const wchar_t* const dest_filename)
{
    ifstream src(source_filename, ios::binary);
    if (!src) {
        return false;
    }

    ofstream dest(dest_filename, ios::binary);
    if (!dest) {
        return false;
    }

    dest << src.rdbuf();

    src.close();
    dest.close();

    return true;
}

#if MAYBE_THIS_WILL_BE_NEEDED

void EnumerateOpenFileHandles(DWORD processId)
{
    HANDLE hFileSnap = CreateToolhelp32Snapshot(TH32CS_SNAPALL, processId);
    if (hFileSnap == INVALID_HANDLE_VALUE)
    {
        printf("Error: CreateToolhelp32Snapshot failed.\n");
        return;
    }

    printf("Open file handles for process %d:\n", processId);

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(hFileSnap, &pe32))
    {
        printf("Error: Process32First failed.\n");
        CloseHandle(hFileSnap);
        return;
    }

    do
    {
        if (pe32.th32ProcessID != processId)
            continue;

        HANDLE hFile = CreateFile(pe32.szExeFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            printf("%s\n", pe32.szExeFile);
            CloseHandle(hFile);
        }
    } while (Process32Next(hFileSnap, &pe32));

    CloseHandle(hFileSnap);
}

/* to use this function you must include winsock2.h and iphlpapi.h, and link in iphlpapi.lib */
#include <winsock2.h>
#include <iphlpapi.h>
#include <stdio.h>

void EnumerateOpenTcpSockets(DWORD processId)
{
    DWORD dwSize = 0;
    ULONG ulRetVal = 0;

    // Retrieve the TCP table.
    PMIB_TCPTABLE2 pTcpTable = NULL;
    if (GetTcpTable2(NULL, &dwSize, TRUE) == ERROR_INSUFFICIENT_BUFFER)
    {
        pTcpTable = (PMIB_TCPTABLE2)malloc(dwSize);
        if (pTcpTable == NULL)
        {
            printf("Error: Memory allocation failed.\n");
            return;
        }
    }

    if ((ulRetVal = GetTcpTable2(pTcpTable, &dwSize, TRUE)) != NO_ERROR)
    {
        printf("Error: GetTcpTable2 failed with error %lu.\n", ulRetVal);
        free(pTcpTable);
        return;
    }

    // Enumerate the TCP connections and filter by process ID.
    for (DWORD i = 0; i < pTcpTable->dwNumEntries; i++)
    {
        PMIB_TCPROW2 pTcpRow = &pTcpTable->table[i];
        if (pTcpRow->dwOwningPid == processId)
        {
            printf("TCP connection %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d\n",
                (pTcpRow->dwLocalAddr >> 0) & 0xff,
                (pTcpRow->dwLocalAddr >> 8) & 0xff,
                (pTcpRow->dwLocalAddr >> 16) & 0xff,
                (pTcpRow->dwLocalAddr >> 24) & 0xff,
                ntohs((unsigned short)pTcpRow->dwLocalPort),
                (pTcpRow->dwRemoteAddr >> 0) & 0xff,
                (pTcpRow->dwRemoteAddr >> 8) & 0xff,
                (pTcpRow->dwRemoteAddr >> 16) & 0xff,
                (pTcpRow->dwRemoteAddr >> 24) & 0xff,
                ntohs((unsigned short)pTcpRow->dwRemotePort));
        }
    }

    free(pTcpTable);
}
#endif

int64_t Util::getUnixTimeMilliseconds() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);  // Retrieves the current system time in UTC

    // Combine high and low parts to form a 64-bit value
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;

    // Convert FILETIME (100-nanoseconds since January 1, 1601) to 
    // Unix epoch time in milliseconds
    int64_t unixTimeMilliseconds = (li.QuadPart - 116444736000000000LL) / 10000;

    return unixTimeMilliseconds;
}

void Util::epochToDateTime(const char* epochStr, char* output) {
    std::time_t epoch = std::atoll(epochStr);
    std::tm timeinfo;
    localtime_s(&timeinfo, &epoch);
    std::strftime(output, 20, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

int Util::compareSoftwareVersions(const std::string& version_a,
    const std::string& version_b) {
    std::vector<int> parts_a = splitVersion(version_a);
    std::vector<int> parts_b = splitVersion(version_b);

    // Find the maximum length of version parts
    size_t maxLength = (std::max)(parts_a.size(), parts_b.size());

    for (size_t i = 0; i < maxLength; i++) {
        // Get the current part for each version.
        // If the version doesn't have this part, treat it as '0'
        int part_a = i < parts_a.size() ? parts_a[i] : 0;
        int part_b = i < parts_b.size() ? parts_b[i] : 0;

        // Compare the current parts
        if (part_a < part_b)
            return -1;
        if (part_a > part_b)
            return 1;
    }

    // If all parts are equal
    return 0;
}

std::vector<int> Util::splitVersion(const std::string& version) {
    std::vector<int> parts;
    std::string clean_version;

    // Find the first occurrence of a non-numeric separator (-, ~, etc)
    size_t suffix_pos = version.find_first_of("-~+");
    if (suffix_pos != std::string::npos) {
        clean_version = version.substr(0, suffix_pos);
    }
    else {
        clean_version = version;
    }

    std::istringstream ss(clean_version);
    std::string token;

    while (std::getline(ss, token, '.')) {
        if (!token.empty()) {
            // Find the first numeric part in the token
            size_t start = token.find_first_of("0123456789");
            if (start != std::string::npos) {
                size_t end = token.find_first_not_of("0123456789", start);
                std::string numericPart = token.substr(start, end - start);
                try {
                    parts.push_back(std::stoi(numericPart));
                }
                catch (const std::invalid_argument&) {
                    parts.push_back(0); // Default to zero if conversion fails
                }
            }
            else {
                // If no numeric part found, treat it as zero
                parts.push_back(0);
            }
        }
        else {
            // If token is empty, treat it as zero
            parts.push_back(0);
        }
    }

    return parts;
}

bool Util::ParseUrl(const wchar_t* url, UrlComponents& components) {
    auto logger = LOG_THIS;

    if (!url || wcslen(url) == 0) {
        logger->recoverable_error("ParseUrl() empty URL\n");
        return false;
    }

    std::wstring urlStr(url);
    
    // Parse scheme (http/https)
    size_t schemeEnd = urlStr.find(L"://");
    if (schemeEnd == std::wstring::npos) {
        // No scheme specified, assume http
        components.isSecure = false;
    } else {
        std::wstring scheme = urlStr.substr(0, schemeEnd);
        // Convert to lowercase for comparison
        std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::towlower);
        components.isSecure = (scheme == L"https");
        urlStr = urlStr.substr(schemeEnd + 3); // Skip past "://"
    }

    // Find end of host (marked by '/' or ':')
    size_t hostEnd = urlStr.find_first_of(L":/");
    if (hostEnd == std::wstring::npos) {
        hostEnd = urlStr.length();
    }
    
    components.hostName = urlStr.substr(0, hostEnd);
    if (components.hostName.empty()) {
        logger->recoverable_error("ParseUrl() no hostname found\n");
        return false;
    }

    // Parse port if present
    size_t portStart = urlStr.find(L':', hostEnd);
    if (portStart != std::wstring::npos) {
        size_t pathStart = urlStr.find(L'/', portStart);
        std::wstring portStr;
        if (pathStart != std::wstring::npos) {
            portStr = urlStr.substr(portStart + 1, pathStart - portStart - 1);
        } else {
            portStr = urlStr.substr(portStart + 1);
        }
        
        try {
            components.port = std::stoi(portStr);
            components.hasExplicitPort = true;
        } catch (const std::exception&) {
            logger->recoverable_error("ParseUrl() invalid port number\n");
            return false;
        }
        
        if (components.port <= 0 || components.port > 65535) {
            logger->recoverable_error("ParseUrl() port number out of range\n");
            return false;
        }
    }

    // Parse path
    size_t pathStart = urlStr.find(L'/', hostEnd);
    if (pathStart != std::wstring::npos) {
        components.path = urlStr.substr(pathStart);
    } else {
        components.path = L"/";
    }

    return true;
}
