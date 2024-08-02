#define SECURITY_WIN32
#include <windows.h>
#include <winevt.h>
#include <iostream>
#include <string>
#include <security.h>

#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "Secur32.lib")

using namespace std;

DWORD WINAPI SubscriptionCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID pContext, EVT_HANDLE hEvent)
{
    return ERROR_SUCCESS;
}

int main() {

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
    std::wcout << L"Enter the event log name to test (e.g., Application, System, Security): ";
    std::getline(std::wcin, logName);

    wstring query = L"*";
    wstring bookmark_xml = L"";
    EVT_HANDLE bookmark;

    if (bookmark_xml.empty()) {
        bookmark = EvtCreateBookmark(NULL);
    }
    else {
        bookmark = EvtCreateBookmark(bookmark_xml.c_str());
        if (bookmark == NULL) {
            bookmark = EvtCreateBookmark(NULL);
        }
    }

    EVT_HANDLE hSubscription = EvtSubscribe(
        NULL,
        NULL,
        logName.c_str(),
        query.c_str(),
        bookmark,
        NULL,
        SubscriptionCallback,
		EvtSubscribeStartAfterBookmark
    );

    if (hSubscription == NULL) {
        DWORD error = GetLastError();
        std::wcout << L"EvtSubscribe failed with error: " << error << std::endl;
        return 1;
    }

    std::wcout << L"Successfully opened event log: " << logName << std::endl;

    EvtClose(hSubscription);
    return 0;
}