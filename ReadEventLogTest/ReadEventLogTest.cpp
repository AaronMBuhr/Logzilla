#define SECURITY_WIN32
#include <windows.h>
#include <winevt.h>
#include <iostream>
#include <string>
#include <security.h>
#include <vector>
#include <iomanip>
#include <ctime>

#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "Secur32.lib")

// Function to convert the timestamp to a readable format
std::wstring FormatTimestamp(DWORD timestamp) {
    time_t rawTime = (time_t)timestamp;
    struct tm timeInfo;
    localtime_s(&timeInfo, &rawTime);

    wchar_t buffer[80];
    wcsftime(buffer, sizeof(buffer) / sizeof(wchar_t), L"%Y-%m-%d %H:%M:%S", &timeInfo);

    return std::wstring(buffer);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <event_log_name>\n";
        return 1;
    }

    WCHAR nameBuf[256];
    ULONG nameSize = sizeof(nameBuf) / sizeof(WCHAR);

    if (GetUserNameExW(NameSamCompatible, nameBuf, &nameSize)) {
        std::wcout << L"The current user is: " << nameBuf << std::endl;
    }
    else {
        DWORD error = GetLastError();
        std::wcerr << L"Failed to get username. Error code: " << error << std::endl;
        return 1;
    }

    std::wstring logName;
    int wchars_num = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, NULL, 0);
    if (wchars_num > 0) {
        logName.resize(wchars_num);
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &logName[0], wchars_num);
    }
    else {
        std::cerr << "Failed to convert log name to wide string." << std::endl;
        return 1;
    }

    HANDLE hEventLog = OpenEventLog(NULL, logName.c_str());
    if (!hEventLog) {
        std::cout << "Failed to open event log.\n";
        return 1;
    }

    DWORD dwRead, dwNeeded;
    EVENTLOGRECORD* pBuffer = NULL;
    std::vector<BYTE> buffer(1024);
    DWORD dwRecordCount = 0;

    while (ReadEventLog(hEventLog, EVENTLOG_BACKWARDS_READ | EVENTLOG_SEQUENTIAL_READ,
        0, buffer.data(), buffer.size(), &dwRead, &dwNeeded)) {
        pBuffer = (EVENTLOGRECORD*)buffer.data();
        while (dwRead > 0) {
            std::wstring eventTime = FormatTimestamp(pBuffer->TimeGenerated);
            std::wstring eventSource = (WCHAR*)(((LPBYTE)pBuffer) + sizeof(EVENTLOGRECORD));
            DWORD eventID = pBuffer->EventID & 0xFFFF;  // Mask to get only the event number (lowest 16 bits)

            // Pretty-print event info on one line
            std::wcout << L"Event " << ++dwRecordCount << L": [" << eventTime << L", "
                << L"Source: " << eventSource << L", "
                << L"Event ID: " << eventID << L"]\n";

            if (dwRecordCount >= 10) break;

            dwRead -= pBuffer->Length;
            pBuffer = (EVENTLOGRECORD*)((LPBYTE)pBuffer + pBuffer->Length);
        }
        if (dwRecordCount >= 10) break;
    }

    CloseEventLog(hEventLog);
    return 0;
}
