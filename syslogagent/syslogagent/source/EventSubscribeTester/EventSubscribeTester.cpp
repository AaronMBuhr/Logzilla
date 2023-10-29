// EventSubscribeTester.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <windows.h>
#include "WinEvents.h"

#if DISABLED
void eventCallback(LPCWSTR rendered_buffer, DWORD buffer_used)
{
    wprintf(L"%s\n\n", rendered_buffer);
}

int main()
{
    std::cout << "Hello World!\n";
    //WinEvents::Test();

    //LPCWSTR path = L"System";
    //LPCWSTR query = L"*";
    //WinEvents::QueryEvents(path, query);

    LPCWSTR path = L"System";
    LPCWSTR query = L"*";
    WinEvents::DoSubscribe(path, query, eventCallback);
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

#endif

#if EVENT_EXAMPLE_2
#include <windows.h>
#include <stdio.h>
#include <sddl.h>
#include <winevt.h>

#pragma comment(lib, "wevtapi.lib")

LPCWSTR GetMessageString(EVT_HANDLE hMetadata, EVT_HANDLE hEvent, EVT_FORMAT_MESSAGE_FLAGS FormatId);

void main(void)
{
    EVT_HANDLE hProviderMetadata = NULL;
    EVT_HANDLE hResults = NULL;
    EVT_HANDLE hEvent = NULL;
    DWORD status = ERROR_SUCCESS;
    DWORD dwReturned = 0;
    LPCWSTR pwsMessage = NULL;
    LPCWSTR pwsPath = L"System";
    LPCWSTR pwsQuery = L"*";
    LPCWSTR pwszPublisherName = L"System";

    // Get the handle to the provider's metadata that contains the message strings.
    hProviderMetadata = EvtOpenPublisherMetadata(NULL, pwszPublisherName, NULL, 0, 0);
    if (NULL == hProviderMetadata)
    {
        wprintf(L"EvtOpenPublisherMetadata failed with %d\n", GetLastError());
        goto cleanup;
    }

    // Query for an event.
    hResults = EvtQuery(NULL, pwsPath, pwsQuery, EvtQueryChannelPath);
    if (NULL == hResults)
    {
        status = GetLastError();

        if (ERROR_EVT_CHANNEL_NOT_FOUND == status)
            wprintf(L"Channel %s was not found.\n", pwsPath);
        else
            wprintf(L"EvtQuery failed with %lu.\n", status);

        goto cleanup;
    }

    // Get a single event from the result set.
    if (!EvtNext(hResults, 1, &hEvent, INFINITE, 0, &dwReturned))
    {
        wprintf(L"EvtNext failed with %lu\n", status);
        goto cleanup;
    }

    // Get the various message strings from the event.
    pwsMessage = GetMessageString(hProviderMetadata, hEvent, EvtFormatMessageEvent);
    if (pwsMessage)
    {
        wprintf(L"Event message string: %s\n\n", pwsMessage);
        free((void*)pwsMessage);
        pwsMessage = NULL;
    }

    pwsMessage = GetMessageString(hProviderMetadata, hEvent, EvtFormatMessageLevel);
    if (pwsMessage)
    {
        wprintf(L"Level message string: %s\n\n", pwsMessage);
        free((void*)pwsMessage);
        pwsMessage = NULL;
    }

    pwsMessage = GetMessageString(hProviderMetadata, hEvent, EvtFormatMessageTask);
    if (pwsMessage)
    {
        wprintf(L"Task message string: %s\n\n", pwsMessage);
        free((void*)pwsMessage);
        pwsMessage = NULL;
    }

    pwsMessage = GetMessageString(hProviderMetadata, hEvent, EvtFormatMessageOpcode);
    if (pwsMessage)
    {
        wprintf(L"Opcode message string: %s\n\n", pwsMessage);
        free((void*)pwsMessage);
        pwsMessage = NULL;
    }

    pwsMessage = GetMessageString(hProviderMetadata, hEvent, EvtFormatMessageKeyword);
    if (pwsMessage)
    {
        LPCWSTR ptemp = pwsMessage;

        wprintf(L"Keyword message string: %s", ptemp);

        while (*(ptemp += wcslen(ptemp) + 1))
            wprintf(L", %s", ptemp);

        wprintf(L"\n\n");
        free((void*)pwsMessage);
        pwsMessage = NULL;
    }

    pwsMessage = GetMessageString(hProviderMetadata, hEvent, EvtFormatMessageChannel);
    if (pwsMessage)
    {
        wprintf(L"Channel message string: %s\n\n", pwsMessage);
        free((void*)pwsMessage);
        pwsMessage = NULL;
    }

    pwsMessage = GetMessageString(hProviderMetadata, hEvent, EvtFormatMessageProvider);
    if (pwsMessage)
    {
        wprintf(L"Provider message string: %s\n\n", pwsMessage);
        free((void*)pwsMessage);
        pwsMessage = NULL;
    }

    pwsMessage = GetMessageString(hProviderMetadata, hEvent, EvtFormatMessageXml);
    if (pwsMessage)
    {
        wprintf(L"XML message string: %s\n\n", pwsMessage);
        free((void*)pwsMessage);
        pwsMessage = NULL;
    }

cleanup:

    if (hEvent)
        EvtClose(hEvent);

    if (hResults)
        EvtClose(hResults);

    if (hProviderMetadata)
        EvtClose(hProviderMetadata);
}


// Gets the specified message string from the event. If the event does not
// contain the specified message, the function returns NULL.
LPCWSTR GetMessageString(EVT_HANDLE hMetadata, EVT_HANDLE hEvent, EVT_FORMAT_MESSAGE_FLAGS FormatId)
{
    LPCWSTR pBuffer = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = 0;

    if (!EvtFormatMessage(hMetadata, hEvent, 0, 0, NULL, FormatId, dwBufferSize, pBuffer, &dwBufferUsed))
    {
        status = GetLastError();
        if (ERROR_INSUFFICIENT_BUFFER == status)
        {
            // An event can contain one or more keywords. The function returns keywords
            // as a list of keyword strings. To process the list, you need to know the
            // size of the buffer, so you know when you have read the last string, or you
            // can terminate the list of strings with a second null terminator character 
            // as this example does.
            //if ((EvtFormatMessageKeyword == FormatId))
            //    pBuffer[dwBufferSize - 1] = L'\0';
            //else
                dwBufferSize = dwBufferUsed;

            pBuffer = (LPCWSTR)malloc(dwBufferSize * sizeof(WCHAR));

            if (pBuffer)
            {
                EvtFormatMessage(hMetadata, hEvent, 0, 0, NULL, FormatId, dwBufferSize, pBuffer, &dwBufferUsed);

                // Add the second null terminator character.
                if ((EvtFormatMessageKeyword == FormatId))
                    pBuffer[dwBufferUsed - 1] = L'\0';
            }
            else
            {
                wprintf(L"malloc failed\n");
            }
        }
        else if (ERROR_EVT_MESSAGE_NOT_FOUND == status || ERROR_EVT_MESSAGE_ID_NOT_FOUND == status)
            ;
        else
        {
            wprintf(L"EvtFormatMessage failed with %u\n", status);
        }
    }

    return pBuffer;
}
#endif

#ifdef GET_CHANNEL_LIST
#include <windows.h>
#include <stdio.h>
#include <winevt.h>

#pragma comment(lib, "wevtapi.lib")


void main(void)
{
    EVT_HANDLE hChannels = NULL;
    LPWSTR pBuffer = NULL;
    LPWSTR pTemp = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = ERROR_SUCCESS;

    // Get a handle to an enumerator that contains all the names of the 
    // channels registered on the computer.
    hChannels = EvtOpenChannelEnum(NULL, 0);
    FILE* debug_file = NULL;

    if (NULL == hChannels)
    {
        wprintf(L"EvtOpenChannelEnum failed with %lu.\n", GetLastError());
        goto cleanup;
    }

    wprintf(L"List of Channels\n\n");
    fopen_s(&debug_file, "d:\\temp\\eventdebug.log", "w");


    // Enumerate through the list of channel names. If the buffer is not big
    // enough reallocate the buffer. To get the configuration information for
    // a channel, call the EvtOpenChannelConfig function.
    while (true)
    {
        if (!EvtNextChannelPath(hChannels, dwBufferSize, pBuffer, &dwBufferUsed))
        {
            status = GetLastError();

            if (ERROR_NO_MORE_ITEMS == status)
            {
                break;
            }
            else if (ERROR_INSUFFICIENT_BUFFER == status)
            {
                dwBufferSize = dwBufferUsed;
                pTemp = (LPWSTR)realloc(pBuffer, dwBufferSize * sizeof(WCHAR));
                if (pTemp)
                {
                    pBuffer = pTemp;
                    pTemp = NULL;
                    EvtNextChannelPath(hChannels, dwBufferSize, pBuffer, &dwBufferUsed);
                }
                else
                {
                    wprintf(L"realloc failed\n");
                    status = ERROR_OUTOFMEMORY;
                    goto cleanup;
                }
            }
            else
            {
                wprintf(L"EvtNextChannelPath failed with %lu.\n", status);
            }
        }

        wprintf(L"%s\n", pBuffer);
        fwprintf(debug_file, L"%s\n", pBuffer);
    }

cleanup:
    fclose(debug_file);

    if (hChannels)
        EvtClose(hChannels);

    if (pBuffer)
        free(pBuffer);
}
#endif


#define GET_CHANNEL_PROPERTIES
#ifdef GET_CHANNEL_PROPERTIES

#include <windows.h>
#include <stdio.h>
#include <winevt.h>

#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "ole32.lib")

LPCWSTR pwcsChannelTypes[] = { L"Admin", L"Operational", L"Analytic", L"Debug" };
LPCWSTR pwcsIsolationTypes[] = { L"Application", L"System", L"Custom" };
LPCWSTR pwcsClockTypes[] = { L"System", L"QPC" };

DWORD PrintChannelProperties(EVT_HANDLE hChannel);
DWORD PrintChannelProperty(int Id, PEVT_VARIANT pProperty);

void main(void)
{
    EVT_HANDLE hChannel = NULL;
    DWORD status = ERROR_SUCCESS;
    LPCWSTR pwsChannelName = L"Microsoft-Windows-TerminalServices-ServerUSBDevices/Admin";

    hChannel = EvtOpenChannelConfig(NULL, pwsChannelName, 0);

    if (NULL == hChannel) // Fails with 15007 (ERROR_EVT_CHANNEL_NOT_FOUND) if the channel is not found
    {
        wprintf(L"EvtOpenChannelConfig failed with %lu.\n", GetLastError());
        goto cleanup;
    }

    status = PrintChannelProperties(hChannel);

cleanup:

    if (hChannel)
        EvtClose(hChannel);
}

// Print the channel's configuration properties. Use the EVT_CHANNEL_CONFIG_PROPERTY_ID
// enumeration values to loop through all the properties.
DWORD PrintChannelProperties(EVT_HANDLE hChannel)
{
    PEVT_VARIANT pProperty = NULL;  // Buffer that receives the property value
    PEVT_VARIANT pTemp = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = ERROR_SUCCESS;

    for (int Id = 0; Id < EvtChannelConfigPropertyIdEND; Id++)
    {
        // Get the specified property. If the buffer is too small, reallocate it.
        if (!EvtGetChannelConfigProperty(hChannel, (EVT_CHANNEL_CONFIG_PROPERTY_ID)Id, 0, dwBufferSize, pProperty, &dwBufferUsed))
        {
            status = GetLastError();
            if (ERROR_INSUFFICIENT_BUFFER == status)
            {
                dwBufferSize = dwBufferUsed;
                pTemp = (PEVT_VARIANT)realloc(pProperty, dwBufferSize);
                if (pTemp)
                {
                    pProperty = pTemp;
                    pTemp = NULL;
                    EvtGetChannelConfigProperty(hChannel, (EVT_CHANNEL_CONFIG_PROPERTY_ID)Id, 0, dwBufferSize, pProperty, &dwBufferUsed);
                }
                else
                {
                    wprintf(L"realloc failed\n");
                    status = ERROR_OUTOFMEMORY;
                    goto cleanup;
                }
            }

            if (ERROR_SUCCESS != (status = GetLastError()))
            {
                wprintf(L"EvtGetChannelConfigProperty failed with %d\n", GetLastError());
                goto cleanup;
            }
        }

        if (status = PrintChannelProperty(Id, pProperty))
            break;
    }

cleanup:

    if (pProperty)
        free(pProperty);

    return status;
}

// Print the property value.
DWORD PrintChannelProperty(int Id, PEVT_VARIANT pProperty)
{
    DWORD status = ERROR_SUCCESS;
    WCHAR wszSessionGuid[50];
    LPCWSTR lpws = NULL;

    switch (Id)
    {
    case EvtChannelConfigEnabled:
        wprintf(L"Enabled: %s\n", (TRUE == pProperty->BooleanVal) ? L"TRUE" : L"FALSE");
        break;

    case EvtChannelConfigIsolation:
        wprintf(L"Isolation: %s\n", pwcsIsolationTypes[pProperty->UInt32Val]);
        break;

    case EvtChannelConfigType:
        wprintf(L"Type: %s\n", pwcsChannelTypes[pProperty->UInt32Val]);
        break;

        // This will contain a value if the channel is imported.
    case EvtChannelConfigOwningPublisher:
        wprintf(L"Publisher that defined the channel: %s\n", pProperty->StringVal);
        break;

    case EvtChannelConfigClassicEventlog:
        wprintf(L"ClassicEventlog: %s\n", (TRUE == pProperty->BooleanVal) ? L"TRUE" : L"FALSE");
        break;

    case EvtChannelConfigAccess:
        wprintf(L"Access: %s\n", pProperty->StringVal);
        break;

    case EvtChannelLoggingConfigRetention:
        wprintf(L"Retention: %s\n", (TRUE == pProperty->BooleanVal) ? L"TRUE (Sequential)" : L"FALSE (Circular)");
        break;

    case EvtChannelLoggingConfigAutoBackup:
        wprintf(L"AutoBackup: %s\n", (TRUE == pProperty->BooleanVal) ? L"TRUE" : L"FALSE");
        break;

    case EvtChannelLoggingConfigMaxSize:
        wprintf(L"MaxSize: %I64u MB\n", pProperty->UInt64Val / (1024 * 1024));
        break;

    case EvtChannelLoggingConfigLogFilePath:
        wprintf(L"LogFilePath: %s\n", pProperty->StringVal);
        break;

    case EvtChannelPublishingConfigLevel:
        if (EvtVarTypeNull == pProperty->Type)
            wprintf(L"Level: \n");
        else
            wprintf(L"Level: %lu\n", pProperty->UInt32Val);

        break;

        // The upper 8 bits can contain reserved bit values, so do not include them
        // when checking to see if any keywords are set.
    case EvtChannelPublishingConfigKeywords:
        if (EvtVarTypeNull == pProperty->Type)
            wprintf(L"Keywords: \n");
        else
            wprintf(L"Keywords: %I64u\n", pProperty->UInt64Val & 0x00FFFFFFFFFFFFFF);

        break;

    case EvtChannelPublishingConfigControlGuid:
        if (EvtVarTypeNull == pProperty->Type)
            wprintf(L"ControlGuid: \n");
        else
        {
            StringFromGUID2(*(pProperty->GuidVal), wszSessionGuid, sizeof(wszSessionGuid) / sizeof(WCHAR));
            wprintf(L"ControlGuid: %s\n", wszSessionGuid);
        }

        break;

    case EvtChannelPublishingConfigBufferSize:
        wprintf(L"BufferSize: %lu KB\n", pProperty->UInt32Val);
        break;

    case EvtChannelPublishingConfigMinBuffers:
        wprintf(L"MinBuffers: %lu\n", pProperty->UInt32Val);
        break;

    case EvtChannelPublishingConfigMaxBuffers:
        wprintf(L"MaxBuffers: %lu\n", pProperty->UInt32Val);
        break;

    case EvtChannelPublishingConfigLatency:
        wprintf(L"Latency: %lu (sec)\n", pProperty->UInt32Val / 1000); // 1 ms
        break;

    case EvtChannelPublishingConfigClockType:
        wprintf(L"ClockType: %s\n", pwcsClockTypes[pProperty->UInt32Val]);
        break;

    case EvtChannelPublishingConfigSidType:
        wprintf(L"Include security ID (SID): %s\n", (EvtChannelSidTypeNone == pProperty->UInt32Val) ? L"No" : L"Yes");
        break;

    case EvtChannelPublisherList:

        wprintf(L"List of providers that import this channel: \n");
        for (DWORD i = 0; i < pProperty->Count; i++)
        {
            wprintf(L"  %s\n", pProperty->StringArr[i]);
        }

        break;

    case EvtChannelPublishingConfigFileMax:
        wprintf(L"FileMax: %lu\n", pProperty->UInt32Val);
        break;

    default:
        wprintf(L"Unknown property Id: %d\n", Id);
    }

    return status;
}

#endif





#ifdef GET_PROVIDER_METADATA
#include <windows.h>
#include <stdio.h>
#include <winevt.h>

#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "ole32.lib")

// Contains the value and message string for a type, such as
// an opcode or task that the provider defines or uses. If the
// type does not specify a message string, the message member
// contains the value of the type's name attribute.
typedef struct _msgstring
{
    union
    {
        DWORD dwValue;  // Value attribute for opcode, task, and level
        UINT64 ullMask; // Mask attribute for keyword
    };
    LPWSTR pwcsMessage; // Message string or name attribute
} MSG_STRING, * PMSG_STRING;

// Header for the block of value/message string pairs. The dwSize member
// is the size, in bytes, of the block of MSG_STRING structures to which 
// the pMessage member points.
typedef struct _messages
{
    DWORD dwSize;
    PMSG_STRING pMessage;
} MESSAGES, * PMESSAGES;


DWORD PrintProviderEvents(EVT_HANDLE hMetadata);
DWORD PrintEventProperties(EVT_HANDLE hEvent);
DWORD PrintEventProperty(EVT_HANDLE hMetadata, int Id, PEVT_VARIANT pProperty);
LPWSTR GetPropertyName(EVT_HANDLE hMetadata, PMESSAGES pMessages, DWORD dwValue);
LPWSTR GetOpcodeName(EVT_HANDLE hMetadata, PMESSAGES pMessages, DWORD dwOpcodeValue, DWORD dwTaskValue);
LPWSTR GetKeywordName(EVT_HANDLE hMetadata, PMESSAGES pMessages, UINT64 ullKeyword);
DWORD PrintProviderProperties(EVT_HANDLE hMetadata);
DWORD PrintProviderProperty(EVT_HANDLE hMetadata, int Id, PEVT_VARIANT pProperty);
LPWSTR GetMessageString(EVT_HANDLE hMetadata, DWORD dwMessageId);
DWORD DumpChannelProperties(EVT_HANDLE hChannels, DWORD dwIndex, PMESSAGES pMessages);
DWORD DumpLevelProperties(EVT_HANDLE hLevels, DWORD dwIndex, PMESSAGES pMessages);
DWORD DumpTaskProperties(EVT_HANDLE hTasks, DWORD dwIndex, PMESSAGES pMessages);
DWORD DumpOpcodeProperties(EVT_HANDLE hOpcodes, DWORD dwIndex, PMESSAGES pMessages);
DWORD DumpKeywordProperties(EVT_HANDLE hKeywords, DWORD dwIndex, PMESSAGES pMessages);
void FreeMessages(PMESSAGES pMessages);
PEVT_VARIANT GetProperty(EVT_HANDLE handle, DWORD dwIndex, EVT_PUBLISHER_METADATA_PROPERTY_ID PropertyId);

// Global variables.
EVT_HANDLE g_hMetadata = NULL;
MESSAGES g_ChannelMessages = { 0, NULL };
MESSAGES g_LevelMessages = { 0, NULL };
MESSAGES g_TaskMessages = { 0, NULL };
MESSAGES g_OpcodeMessages = { 0, NULL };
MESSAGES g_KeywordMessages = { 0, NULL };


void main(void)
{
    LPCWSTR pwszPublisherName = L"Microsoft-Windows-WinNat";
    DWORD status = ERROR_SUCCESS;

    // Get a handle to the provider's metadata. You can specify the provider's name
    // if the provider is registered on the computer or the full path to an archived 
    // log file (archived log files contain the provider's metadata). Specify the locale 
    // identifier if you want the localized strings returned in a locale other than
    // the locale of the current thread.
    g_hMetadata = EvtOpenPublisherMetadata(NULL,
        pwszPublisherName,
        NULL,
        0, //MAKELCID(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), 0), 
        0);
    if (NULL == g_hMetadata)
    {
        wprintf(L"EvtOpenPublisherMetadata failed with %d\n", GetLastError());
        goto cleanup;
    }

    wprintf(L"Provider Metadata for %s\n\n", pwszPublisherName);
    status = PrintProviderProperties(g_hMetadata);

    wprintf(L"\nEvent Metadata for %s\n\n", pwszPublisherName);
    status = PrintProviderEvents(g_hMetadata);

cleanup:

    if (g_hMetadata)
        EvtClose(g_hMetadata);

    if (g_ChannelMessages.pMessage)
        FreeMessages(&g_ChannelMessages);

    if (g_LevelMessages.pMessage)
        FreeMessages(&g_LevelMessages);

    if (g_TaskMessages.pMessage)
        FreeMessages(&g_TaskMessages);

    if (g_OpcodeMessages.pMessage)
        FreeMessages(&g_OpcodeMessages);

    if (g_KeywordMessages.pMessage)
        FreeMessages(&g_KeywordMessages);
}

// Free the memory for each message string in the messages block
// and then free the messages block.
void FreeMessages(PMESSAGES pMessages)
{
    DWORD dwCount = pMessages->dwSize / sizeof(MSG_STRING);

    for (DWORD i = 0; i < dwCount; i++)
    {
        free(((pMessages->pMessage) + i)->pwcsMessage);
        ((pMessages->pMessage) + i)->pwcsMessage = NULL;
    }

    free(pMessages->pMessage);
}

// Get an enumerator to the provider's events and enumerate them.
// Call this function after first calling the PrintProviderProperties
// function to get the message strings that this section uses.
DWORD PrintProviderEvents(EVT_HANDLE hMetadata)
{
    EVT_HANDLE hEvents = NULL;
    EVT_HANDLE hEvent = NULL;
    DWORD status = ERROR_SUCCESS;

    // Get a handle to the provider's events.
    hEvents = EvtOpenEventMetadataEnum(hMetadata, 0);
    if (NULL == hEvents)
    {
        wprintf(L"EvtOpenEventMetadataEnum failed with %lu\n", GetLastError());
        goto cleanup;
    }

    // Enumerate the events and print each event's metadata.
    while (true)
    {
        hEvent = EvtNextEventMetadata(hEvents, 0);
        if (NULL == hEvent)
        {
            if (ERROR_NO_MORE_ITEMS != (status = GetLastError()))
            {
                wprintf(L"EvtNextEventMetadata failed with %lu\n", status);
            }

            break;
        }

        if (status = PrintEventProperties(hEvent))
            break;

        EvtClose(hEvent);
        hEvent = NULL;
    }


cleanup:

    if (hEvents)
        EvtClose(hEvents);

    if (hEvent)
        EvtClose(hEvent);

    return status;
}


// Print the metadata for the event.
DWORD PrintEventProperties(EVT_HANDLE hEvent)
{
    PEVT_VARIANT pProperty = NULL;  // Contains a metadata value
    PEVT_VARIANT pTemp = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = ERROR_SUCCESS;

    // Use the EVT_EVENT_METADATA_PROPERTY_ID's enumeration values to loop
    // through all the metadata for the event.
    for (int Id = 0; Id < EvtEventMetadataPropertyIdEND; Id++)
    {
        // Get the specified metadata property. If the pProperty buffer is not big enough, reallocate the buffer.
        if (!EvtGetEventMetadataProperty(hEvent, (EVT_EVENT_METADATA_PROPERTY_ID)Id, 0, dwBufferSize, pProperty, &dwBufferUsed))
        {
            status = GetLastError();
            if (ERROR_INSUFFICIENT_BUFFER == status)
            {
                dwBufferSize = dwBufferUsed;
                pTemp = (PEVT_VARIANT)realloc(pProperty, dwBufferSize);
                if (pTemp)
                {
                    pProperty = pTemp;
                    pTemp = NULL;
                    EvtGetEventMetadataProperty(hEvent, (EVT_EVENT_METADATA_PROPERTY_ID)Id, 0, dwBufferSize, pProperty, &dwBufferUsed);
                }
                else
                {
                    wprintf(L"realloc failed\n");
                    status = ERROR_OUTOFMEMORY;
                    goto cleanup;
                }
            }

            if (ERROR_SUCCESS != (status = GetLastError()))
            {
                wprintf(L"EvtGetEventMetadataProperty failed with %d\n", GetLastError());
                goto cleanup;
            }
        }

        if (status = PrintEventProperty(g_hMetadata, Id, pProperty))
            break;

        RtlZeroMemory(pProperty, dwBufferUsed);
    }

cleanup:

    if (pProperty)
        free(pProperty);

    return status;
}


// Print the event property.
DWORD PrintEventProperty(EVT_HANDLE hMetadata, int Id, PEVT_VARIANT pProperty)
{
    DWORD status = ERROR_SUCCESS;
    static DWORD dwOpcode = 0;
    LPWSTR pName = NULL;       // The event's name property
    LPWSTR pMessage = NULL;    // The event's message string

    switch (Id)
    {
    case EventMetadataEventID:
        wprintf(L"ID: %lu\n", pProperty->UInt32Val);
        break;

    case EventMetadataEventVersion:
        wprintf(L"Version: %lu\n", pProperty->UInt32Val);
        break;

        // The channel property contains the value of the channel's value attribute.
        // Instead of printing the value attribute, use it to find the channel's
        // message string or name and print it.
    case EventMetadataEventChannel:
        if (pProperty->UInt32Val > 0)
        {
            pName = GetPropertyName(hMetadata, &g_ChannelMessages, pProperty->UInt32Val);
            wprintf(L"Channel: %s\n", pName);
        }
        else
        {
            wprintf(L"Channel: \n");
        }
        break;

        // The level property contains the value of the level's value attribute.
        // Instead of printing the value attribute, use it to find the level's
        // message string or name and print it.
    case EventMetadataEventLevel:
        if (pProperty->UInt32Val > 0)
        {
            pName = GetPropertyName(hMetadata, &g_LevelMessages, pProperty->UInt32Val);
            wprintf(L"Level: %s\n", pName);
        }
        else
        {
            wprintf(L"Level: \n");
        }
        break;

        // The opcode property contains the value of the opcode's value attribute.
        // Instead of printing the value attribute, use it to find the opcode's
        // message string or name and print it.
        // The opcode value contains the opcode in the high word. If the opcode is 
        // task-specific, the opcode value will contain the task value in the low word.
        // Save the opcode in a static variable and print it when we get the task
        // value, so we can decide if the opcode is task-specific.
    case EventMetadataEventOpcode:
        dwOpcode = pProperty->UInt32Val;
        break;

        // The task property contains the value of the task's value attribute.
        // Instead of printing the value attribute, use it to find the task's
        // message string or name and print it.
    case EventMetadataEventTask:
        if (pProperty->UInt32Val > 0)
        {
            pName = GetPropertyName(hMetadata, &g_TaskMessages, pProperty->UInt32Val);
            wprintf(L"Task: %s\n", pName);
        }
        else
        {
            wprintf(L"Task: \n");
        }

        // Now that we know the task, get the opcode string and print it.
        if (dwOpcode > 0)
        {
            pName = GetOpcodeName(hMetadata, &g_OpcodeMessages, dwOpcode, pProperty->UInt32Val);
            wprintf(L"Opcode: %s\n", pName);
        }
        else
        {
            wprintf(L"Opcode: \n");
        }
        break;

        // The keyword property contains a bit mask of all the keywords.
        // Instead of printing the value attribute, use it to find the 
        // message string or name associated with each keyword and print them (space delimited).
    case EventMetadataEventKeyword:
        // The upper 8 bits can contain reserved bit values, so do not include them
        // when checking to see if any keywords are set.
        if ((pProperty->UInt32Val & 0x00FFFFFFFFFFFFFF) > 0)
        {
            pName = GetKeywordName(hMetadata, &g_KeywordMessages, pProperty->UInt32Val);
            wprintf(L"Keyword: %s\n", pName);
            if (pName)
                free(pName);
        }
        else
        {
            wprintf(L"Keyword: \n");
        }
        break;

        // If the message string is not specified, the value is -1.
    case EventMetadataEventMessageID:
        if (-1 == pProperty->UInt32Val)
        {
            wprintf(L"Message string: \n");
        }
        else
        {
            pMessage = GetMessageString(hMetadata, pProperty->UInt32Val);
            wprintf(L"Message string: %s\n", (pMessage) ? pMessage : L"");
            if (pMessage)
            {
                free(pMessage);
            }
        }
        break;

        // When you define the event, the template attribute contains the template
        // identifier; however, the template metadata contains an XML string of the 
        // template (includes the data items, not the UserData section).
    case EventMetadataEventTemplate:
        wprintf(L"Template: %s\n\n", pProperty->StringVal);
        break;

    default:
        wprintf(L"Unknown property Id: %d\n", Id);
    }

    return status;
}


// Used to get the message string or name for levels, tasks, and channels.
// Search the messages block sequentially for an item that has the same value
// and return a pointer to the message string.
LPWSTR GetPropertyName(EVT_HANDLE hMetadata, PMESSAGES pMessages, DWORD dwValue)
{
    UNREFERENCED_PARAMETER(hMetadata);

    LPWSTR pMessage = NULL;
    DWORD dwCount = pMessages->dwSize / sizeof(MSG_STRING);

    for (DWORD i = 0; i < dwCount; i++)
    {
        if (dwValue == ((pMessages->pMessage) + i)->dwValue)
        {
            pMessage = ((pMessages->pMessage) + i)->pwcsMessage;
            break;
        }
    }

    return pMessage;
}

// Used to get the message string or name for an opcode. Search the messages block sequentially 
// for an item that has the same opcode value (high word). Opcodes can be defined globally or 
// locally (task-specific). All global opcodes must be unique, but multiple tasks can specify the
// same opcode value, so we need to check the low word to see if the task on the event matches
// the task on the opcode.
LPWSTR GetOpcodeName(EVT_HANDLE hMetadata, PMESSAGES pMessages, DWORD dwOpcodeValue, DWORD dwTaskValue)
{
    UNREFERENCED_PARAMETER(hMetadata);

    LPWSTR pMessage = NULL;
    DWORD dwCount = pMessages->dwSize / sizeof(MSG_STRING);
    DWORD dwOpcodeIndex = 0;  // Points to the global opcode (low word is zero)
    BOOL fFound = FALSE;

    for (DWORD i = 0; i < dwCount; i++)
    {
        if (dwOpcodeValue == HIWORD(((pMessages->pMessage) + i)->dwValue))
        {
            if (0 == LOWORD(((pMessages->pMessage) + i)->dwValue))
            {
                dwOpcodeIndex = i;
            }
            else if (dwTaskValue == LOWORD(((pMessages->pMessage) + i)->dwValue))
            {
                pMessage = ((pMessages->pMessage) + i)->pwcsMessage;
                fFound = TRUE;
                break;
            }
        }
    }

    if (FALSE == fFound)
    {
        pMessage = ((pMessages->pMessage) + dwOpcodeIndex)->pwcsMessage;
    }

    return pMessage;
}

// Used to get the message strings or names for the keywords specified on the event. The event
// contains a bit mask that has bits set for each keyword specified on the event. Search the 
// messages block sequentially for items that have the same keyword bit set. Concatenate all the
// message strings.
LPWSTR GetKeywordName(EVT_HANDLE hMetadata, PMESSAGES pMessages, UINT64 ullKeywords)
{
    UNREFERENCED_PARAMETER(hMetadata);

    LPWSTR pMessage = NULL;
    LPWSTR pTemp = NULL;
    BOOL fFirstMessage = TRUE;
    size_t dwStringLen = 0;
    DWORD dwCount = pMessages->dwSize / sizeof(MSG_STRING);

    for (DWORD i = 0; i < dwCount; i++)
    {
        if (ullKeywords & ((pMessages->pMessage) + i)->ullMask)
        {
            dwStringLen += wcslen(((pMessages->pMessage) + i)->pwcsMessage) + 1 + 1;  // + space delimiter + null-terminator
            pTemp = (LPWSTR)realloc(pMessage, dwStringLen * sizeof(WCHAR));
            if (pTemp)
            {
                pMessage = pTemp;
                pTemp = NULL;

                if (fFirstMessage)
                {
                    *pMessage = L'\0';  // Need so first wcscat_s call works
                    fFirstMessage = FALSE;
                }
                else
                {
                    wcscat_s(pMessage, dwStringLen, L" ");  // Space delimiter
                }

                wcscat_s(pMessage, dwStringLen, ((pMessages->pMessage) + i)->pwcsMessage);
            }
            else
            {
                wprintf(L"realloc failed for GetKeywordName\n");
                if (pMessage)
                {
                    free(pMessage);
                    pMessage = NULL;
                }
                break;
            }
        }
    }

    return pMessage;
}


// Use the EVT_PUBLISHER_METADATA_PROPERTY_ID enumeration values to enumerate all the
// provider's metadata excluding event metadata. Enumerates the metadata for channels,
// tasks, opcodes, levels, keywords, and the provider.
DWORD PrintProviderProperties(EVT_HANDLE hMetadata)
{
    PEVT_VARIANT pProperty = NULL;  // Contains the metadata property
    PEVT_VARIANT pTemp = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = ERROR_SUCCESS;

    for (int Id = 0; Id < EvtPublisherMetadataPropertyIdEND; Id++)
    {
        // Get the metadata property. If the buffer is not big enough, reallocate the buffer.
        if (!EvtGetPublisherMetadataProperty(hMetadata, (EVT_PUBLISHER_METADATA_PROPERTY_ID)Id, 0, dwBufferSize, pProperty, &dwBufferUsed))
        {
            status = GetLastError();
            if (ERROR_INSUFFICIENT_BUFFER == status)
            {
                dwBufferSize = dwBufferUsed;
                pTemp = (PEVT_VARIANT)realloc(pProperty, dwBufferSize);
                if (pTemp)
                {
                    pProperty = pTemp;
                    pTemp = NULL;
                    EvtGetPublisherMetadataProperty(hMetadata, (EVT_PUBLISHER_METADATA_PROPERTY_ID)Id, 0, dwBufferSize, pProperty, &dwBufferUsed);
                }
                else
                {
                    wprintf(L"realloc failed\n");
                    status = ERROR_OUTOFMEMORY;
                    goto cleanup;
                }
            }

            if (ERROR_SUCCESS != (status = GetLastError()))
            {
                wprintf(L"EvtGetPublisherMetadataProperty failed with %d\n", GetLastError());
                goto cleanup;
            }
        }

        if (status = PrintProviderProperty(hMetadata, Id, pProperty))
            break;

        RtlZeroMemory(pProperty, dwBufferUsed);

        // Skip the type-specific IDs, so the loop doesn't fail. For channels, levels,
        // opcodes, tasks, and keywords, you use EvtGetPublisherMetadataProperty 
        // to get a handle to an array of those objects. You would then use the type
        // specific ID (for example, EvtPublisherMetadataLevelValue) to access the metadata from
        // the array. Do not call EvtGetPublisherMetadataProperty with a type specific ID or it 
        // will fail. The switch statement increments to the end of the type specific IDs for 
        // each type.
        switch (Id)
        {
        case EvtPublisherMetadataChannelReferences:
            Id = EvtPublisherMetadataChannelReferenceMessageID;
            break;

        case EvtPublisherMetadataLevels:
            Id = EvtPublisherMetadataLevelMessageID;
            break;

        case EvtPublisherMetadataOpcodes:
            Id = EvtPublisherMetadataOpcodeMessageID;
            break;

        case EvtPublisherMetadataTasks:
            Id = EvtPublisherMetadataTaskMessageID;
            break;

        case EvtPublisherMetadataKeywords:
            Id = EvtPublisherMetadataKeywordMessageID;
            break;
        }
    }

cleanup:

    if (pProperty)
        free(pProperty);

    return status;
}


// Print the metadata properties for the provider and the types that
// it defines or references.
DWORD PrintProviderProperty(EVT_HANDLE hMetadata, int Id, PEVT_VARIANT pProperty)
{
    UNREFERENCED_PARAMETER(hMetadata);

    DWORD status = ERROR_SUCCESS;
    WCHAR wszProviderGuid[50];
    DWORD dwArraySize = 0;
    DWORD dwBlockSize = 0;
    LPWSTR pMessage = NULL;

    switch (Id)
    {
    case EvtPublisherMetadataPublisherGuid:
        StringFromGUID2(*(pProperty->GuidVal), wszProviderGuid, sizeof(wszProviderGuid) / sizeof(WCHAR));
        wprintf(L"Guid: %s\n", wszProviderGuid);
        break;

    case EvtPublisherMetadataResourceFilePath:
        wprintf(L"ResourceFilePath: %s\n", pProperty->StringVal);
        break;

    case EvtPublisherMetadataParameterFilePath:
        wprintf(L"ParameterFilePath: %s\n", (EvtVarTypeNull == pProperty->Type) ? L"" : pProperty->StringVal);
        break;

    case EvtPublisherMetadataMessageFilePath:
        wprintf(L"MessageFilePath: %s\n", pProperty->StringVal);
        break;

    case EvtPublisherMetadataHelpLink:
        wprintf(L"HelpLink: %s\n", pProperty->StringVal);
        break;

        // The message string ID is -1 if the provider element does not specify the message attribute.
    case EvtPublisherMetadataPublisherMessageID:
        if (-1 == pProperty->UInt32Val)
        {
            wprintf(L"Message string: \n");
        }
        else
        {
            pMessage = GetMessageString(g_hMetadata, pProperty->UInt32Val);
            wprintf(L"Publisher message string: %s\n", (pMessage) ? pMessage : L"");
            if (pMessage)
            {
                free(pMessage);
            }
        }
        break;

        // We got the handle to all the channels defined in the channels section
        // of the manifest. Get the size of the array of channel objects and 
        // allocate the messages block that will contain the value and
        // message string for each channel. The strings are used to retrieve
        // display names for the channel referenced in an event definition.
    case EvtPublisherMetadataChannelReferences:
        wprintf(L"Channels:\n");
        if (EvtGetObjectArraySize(pProperty->EvtHandleVal, &dwArraySize))
        {
            // You always get a handle to the array but the array can be empty.
            if (dwArraySize > 0)
            {
                dwBlockSize = sizeof(MSG_STRING) * dwArraySize;
                g_ChannelMessages.pMessage = (PMSG_STRING)malloc(dwBlockSize);
                if (g_ChannelMessages.pMessage)
                {
                    RtlZeroMemory(g_ChannelMessages.pMessage, dwBlockSize);
                    g_ChannelMessages.dwSize = dwBlockSize;

                    // For each channel, print its metadata.
                    for (DWORD i = 0; i < dwArraySize; i++)
                    {
                        if (status = DumpChannelProperties(pProperty->EvtHandleVal, i, &g_ChannelMessages))
                            break;
                    }
                }
                else
                {
                    status = ERROR_OUTOFMEMORY;
                    wprintf(L"g_pChannelMessages allocation error\n");
                }
            }
        }
        else
        {
            status = GetLastError();
        }

        EvtClose(pProperty->EvtHandleVal);
        break;

        // These are handled by the EvtPublisherMetadataChannelReferences case;
        // they are here for completeness but will never be exercised.
    case EvtPublisherMetadataChannelReferencePath:
    case EvtPublisherMetadataChannelReferenceIndex:
    case EvtPublisherMetadataChannelReferenceID:
    case EvtPublisherMetadataChannelReferenceFlags:
    case EvtPublisherMetadataChannelReferenceMessageID:
        break;

        // We got the handle to all the levels defined in the channels section
        // of the manifest. Get the size of the array of level objects and 
        // allocate the messages block that will contain the value and
        // message string for each level. The strings are used to retrieve
        // display names for the level referenced in an event definition.
        // References to the levels defined in Winmeta.xml are included in 
        // the list.
    case EvtPublisherMetadataLevels:
        wprintf(L"Levels:\n");
        if (EvtGetObjectArraySize(pProperty->EvtHandleVal, &dwArraySize))
        {
            // You always get a handle to the array but the array can be empty.
            if (dwArraySize > 0)
            {
                dwBlockSize = sizeof(MSG_STRING) * dwArraySize;
                g_LevelMessages.pMessage = (PMSG_STRING)malloc(dwBlockSize);
                if (g_LevelMessages.pMessage)
                {
                    RtlZeroMemory(g_LevelMessages.pMessage, dwBlockSize);
                    g_LevelMessages.dwSize = dwBlockSize;

                    // For each level, print its metadata.
                    for (DWORD i = 0; i < dwArraySize; i++)
                    {
                        if (status = DumpLevelProperties(pProperty->EvtHandleVal, i, &g_LevelMessages))
                            break;
                    }
                }
                else
                {
                    status = ERROR_OUTOFMEMORY;
                    wprintf(L"g_pLevelMessages allocation error\n");
                }
            }
        }
        else
        {
            status = GetLastError();
        }

        EvtClose(pProperty->EvtHandleVal);
        break;

        // These are handled by the EvtPublisherMetadataLevels case;
        // they are here for completeness but will never be exercised.
    case EvtPublisherMetadataLevelName:
    case EvtPublisherMetadataLevelValue:
    case EvtPublisherMetadataLevelMessageID:
        break;

        // We got the handle to all the tasks defined in the channels section
        // of the manifest. Get the size of the array of task objects and 
        // allocate the messages block that will contain the value and
        // message string for each task. The strings are used to retrieve
        // display names for the task referenced in an event definition.
    case EvtPublisherMetadataTasks:
        wprintf(L"Tasks:\n");
        if (EvtGetObjectArraySize(pProperty->EvtHandleVal, &dwArraySize))
        {
            // You always get a handle to the array but the array can be empty.
            if (dwArraySize > 0)
            {
                dwBlockSize = sizeof(MSG_STRING) * dwArraySize;
                g_TaskMessages.pMessage = (PMSG_STRING)malloc(dwBlockSize);
                if (g_TaskMessages.pMessage)
                {
                    RtlZeroMemory(g_TaskMessages.pMessage, dwBlockSize);
                    g_TaskMessages.dwSize = dwBlockSize;

                    // For each task, print its metadata.
                    for (DWORD i = 0; i < dwArraySize; i++)
                    {
                        if (status = DumpTaskProperties(pProperty->EvtHandleVal, i, &g_TaskMessages))
                            break;
                    }
                }
                else
                {
                    status = ERROR_OUTOFMEMORY;
                    wprintf(L"g_pTaskMessages allocation error\n");
                }
            }
        }
        else
        {
            status = GetLastError();
        }

        EvtClose(pProperty->EvtHandleVal);
        break;

        // These are handled by the EvtPublisherMetadataTasks case;
        // they are here for completeness but will never be exercised.
    case EvtPublisherMetadataTaskName:
    case EvtPublisherMetadataTaskEventGuid:
    case EvtPublisherMetadataTaskValue:
    case EvtPublisherMetadataTaskMessageID:
        break;

        // We got the handle to all the opcodes defined in the channels section
        // of the manifest. Get the size of the array of opcode objects and 
        // allocate the messages block that will contain the value and
        // message string for each opcode. The strings are used to retrieve
        // display names for the opcode referenced in an event definition.
    case EvtPublisherMetadataOpcodes:
        wprintf(L"Opcodes:\n");
        if (EvtGetObjectArraySize(pProperty->EvtHandleVal, &dwArraySize))
        {
            // You always get a handle to the array but the array can be empty.
            if (dwArraySize > 0)
            {
                dwBlockSize = sizeof(MSG_STRING) * dwArraySize;
                g_OpcodeMessages.pMessage = (PMSG_STRING)malloc(dwBlockSize);
                if (g_OpcodeMessages.pMessage)
                {
                    RtlZeroMemory(g_OpcodeMessages.pMessage, dwBlockSize);
                    g_OpcodeMessages.dwSize = dwBlockSize;

                    // For each opcode, print its metadata.
                    for (DWORD i = 0; i < dwArraySize; i++)
                    {
                        if (status = DumpOpcodeProperties(pProperty->EvtHandleVal, i, &g_OpcodeMessages))
                            break;
                    }
                }
                else
                {
                    status = ERROR_OUTOFMEMORY;
                    wprintf(L"g_pOpcodeMessages allocation error\n");
                }
            }
        }
        else
        {
            status = GetLastError();
        }

        EvtClose(pProperty->EvtHandleVal);
        break;

        // These are handled by the EvtPublisherMetadataOpcodes case;
        // they are here for completeness but will never be exercised.
    case EvtPublisherMetadataOpcodeName:
    case EvtPublisherMetadataOpcodeValue:
    case EvtPublisherMetadataOpcodeMessageID:
        break;

        // We got the handle to all the keywords defined in the channels section
        // of the manifest. Get the size of the array of keyword objects and 
        // allocate the messages block that will contain the value and
        // message string for each keyword. The strings are used to retrieve
        // display names for the keyword referenced in an event definition.
    case EvtPublisherMetadataKeywords:
        wprintf(L"Keywords:\n");
        if (EvtGetObjectArraySize(pProperty->EvtHandleVal, &dwArraySize))
        {
            // You always get a handle to the array but the array can be empty.
            if (dwArraySize > 0)
            {
                dwBlockSize = sizeof(MSG_STRING) * dwArraySize;
                g_KeywordMessages.pMessage = (PMSG_STRING)malloc(dwBlockSize);
                if (g_KeywordMessages.pMessage)
                {
                    RtlZeroMemory(g_KeywordMessages.pMessage, dwBlockSize);
                    g_KeywordMessages.dwSize = dwBlockSize;

                    // For each keyword, print its metadata.
                    for (DWORD i = 0; i < dwArraySize; i++)
                    {
                        if (status = DumpKeywordProperties(pProperty->EvtHandleVal, i, &g_KeywordMessages))
                            break;
                    }
                }
                else
                {
                    status = ERROR_OUTOFMEMORY;
                    wprintf(L"g_pKeywordMessages allocation error\n");
                }
            }
        }
        else
        {
            status = GetLastError();
        }

        EvtClose(pProperty->EvtHandleVal);
        break;

        // These are handled by the EvtPublisherMetadataKeywords case;
        // they are here for completeness but will never be exercised.
    case EvtPublisherMetadataKeywordName:
    case EvtPublisherMetadataKeywordValue:
    case EvtPublisherMetadataKeywordMessageID:
        break;

    default:
        wprintf(L"Unknown property Id: %d\n", Id);
    }

    return status;
}

// Print the metadata for a channel. Capture the message string and value for use later.
DWORD DumpChannelProperties(EVT_HANDLE hChannels, DWORD dwIndex, PMESSAGES pMessages)
{
    LPWSTR pMessage = NULL;
    DWORD status = ERROR_SUCCESS;
    size_t dwStringLen = 0;
    PEVT_VARIANT pvBuffer = NULL;

    pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferenceMessageID);
    if (pvBuffer)
    {
        // The value is -1 if the channel did not specify a message attribute.
        if (-1 != pvBuffer->UInt32Val)
        {
            pMessage = GetMessageString(g_hMetadata, pvBuffer->UInt32Val);
        }

        wprintf(L"\tChannel message string: %s\n", (pMessage) ? pMessage : L"");
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // This is the channel name. You can use it to call the EvtOpenChannelConfig function
    // to get the channel's configuration information.
    pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferencePath);
    if (pvBuffer)
    {
        wprintf(L"\tChannel path is %s\n", pvBuffer->StringVal);
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // Capture the message string if the channel specified a message string; otherwise,
    // capture the channel's name.
    if (pMessage)
    {
        dwStringLen = wcslen(pMessage) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pMessage);
    }
    else
    {
        dwStringLen = wcslen(pvBuffer->StringVal) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pvBuffer->StringVal);
    }

    pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferenceIndex);
    if (pvBuffer)
    {
        wprintf(L"\tChannel index is %lu\n", pvBuffer->UInt32Val);
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // Capture the channel's value attribute, which is used to look up the channel's
    // message string.
    pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferenceID);
    if (pvBuffer)
    {
        wprintf(L"\tChannel ID is %lu\n", pvBuffer->UInt32Val);
        ((pMessages->pMessage) + dwIndex)->dwValue = pvBuffer->UInt32Val;
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferenceFlags);
    if (pvBuffer)
    {
        if (EvtChannelReferenceImported == (EvtChannelReferenceImported & pvBuffer->UInt32Val))
            wprintf(L"\tChannel is imported\n");
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

cleanup:

    if (pvBuffer)
        free(pvBuffer);

    if (pMessage)
        free(pMessage);

    return status;
}


// Print the metadata for a level. Capture the message string and value for use later.
DWORD DumpLevelProperties(EVT_HANDLE hLevels, DWORD dwIndex, PMESSAGES pMessages)
{
    LPWSTR pMessage = NULL;
    DWORD status = ERROR_SUCCESS;
    size_t dwStringLen = 0;
    PEVT_VARIANT pvBuffer = NULL;

    pvBuffer = GetProperty(hLevels, dwIndex, EvtPublisherMetadataLevelMessageID);
    if (pvBuffer)
    {
        // The value is -1 if the level did not specify a message attribute.
        if (-1 != pvBuffer->UInt32Val)
        {
            pMessage = GetMessageString(g_hMetadata, pvBuffer->UInt32Val);
        }

        wprintf(L"\tLevel message string: %s\n", (pMessage) ? pMessage : L"");
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    pvBuffer = GetProperty(hLevels, dwIndex, EvtPublisherMetadataLevelName);
    if (pvBuffer)
    {
        wprintf(L"\tLevel name is %s\n", pvBuffer->StringVal);
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // Capture the message string if the level specified a message string; otherwise,
    // capture the level's name.
    if (pMessage)
    {
        dwStringLen = wcslen(pMessage) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pMessage);
    }
    else
    {
        dwStringLen = wcslen(pvBuffer->StringVal) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pvBuffer->StringVal);
    }

    // Capture the level's value attribute, which is used to look up the level's
    // message string.
    pvBuffer = GetProperty(hLevels, dwIndex, EvtPublisherMetadataLevelValue);
    if (pvBuffer)
    {
        wprintf(L"\tLevel value is %lu\n", pvBuffer->UInt32Val);
        ((pMessages->pMessage) + dwIndex)->dwValue = pvBuffer->UInt32Val;
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

cleanup:

    if (pvBuffer)
        free(pvBuffer);

    if (pMessage)
        free(pMessage);

    return status;
}


// Print the metadata for a task. Capture the message string and value for use later.
DWORD DumpTaskProperties(EVT_HANDLE hTasks, DWORD dwIndex, PMESSAGES pMessages)
{
    LPWSTR pMessage = NULL;
    DWORD status = ERROR_SUCCESS;
    size_t dwStringLen = 0;
    PEVT_VARIANT pvBuffer = NULL;
    WCHAR wszEventGuid[50];

    pvBuffer = GetProperty(hTasks, dwIndex, EvtPublisherMetadataTaskMessageID);
    if (pvBuffer)
    {
        // The value is -1 if the task did not specify a message attribute.
        if (-1 != pvBuffer->UInt32Val)
        {
            pMessage = GetMessageString(g_hMetadata, pvBuffer->UInt32Val);
        }

        wprintf(L"\tTask message string: %s\n", (pMessage) ? pMessage : L"");
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    pvBuffer = GetProperty(hTasks, dwIndex, EvtPublisherMetadataTaskName);
    if (pvBuffer)
    {
        wprintf(L"\tTask name is %s\n", pvBuffer->StringVal);
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // Capture the message string if the task specified a message string; otherwise,
    // capture the task's name.
    if (pMessage)
    {
        dwStringLen = wcslen(pMessage) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pMessage);
    }
    else
    {
        dwStringLen = wcslen(pvBuffer->StringVal) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pvBuffer->StringVal);
    }

    pvBuffer = GetProperty(hTasks, dwIndex, EvtPublisherMetadataTaskEventGuid);
    if (pvBuffer)
    {
        if (!IsEqualGUID(GUID_NULL, *(pvBuffer->GuidVal)))
        {
            StringFromGUID2(*(pvBuffer->GuidVal), wszEventGuid, sizeof(wszEventGuid) / sizeof(WCHAR));
            wprintf(L"\tTask event GUID is %s\n", wszEventGuid);
        }
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // Capture the task's value attribute, which is used to look up the task's
    // message string.
    pvBuffer = GetProperty(hTasks, dwIndex, EvtPublisherMetadataTaskValue);
    if (pvBuffer)
    {
        wprintf(L"\tTask value is %lu\n", pvBuffer->UInt32Val);
        ((pMessages->pMessage) + dwIndex)->dwValue = pvBuffer->UInt32Val;
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

cleanup:

    if (pvBuffer)
        free(pvBuffer);

    if (pMessage)
        free(pMessage);

    return status;
}


// Print the metadata for a opcode. Capture the message string and value for use later.
DWORD DumpOpcodeProperties(EVT_HANDLE hOpcodes, DWORD dwIndex, PMESSAGES pMessages)
{
    LPWSTR pMessage = NULL;
    DWORD status = ERROR_SUCCESS;
    size_t dwStringLen = 0;
    PEVT_VARIANT pvBuffer = NULL;

    pvBuffer = GetProperty(hOpcodes, dwIndex, EvtPublisherMetadataOpcodeMessageID);
    if (pvBuffer)
    {
        // The value is -1 if the opcode did not specify a message attribute.
        if (-1 != pvBuffer->UInt32Val)
        {
            pMessage = GetMessageString(g_hMetadata, pvBuffer->UInt32Val);
        }

        wprintf(L"\tOpcode message string: %s\n", (pMessage) ? pMessage : L"");
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    pvBuffer = GetProperty(hOpcodes, dwIndex, EvtPublisherMetadataOpcodeName);
    if (pvBuffer)
    {
        wprintf(L"\tOpcode name is %s\n", pvBuffer->StringVal);
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // Capture the message string if the opcode specified a message string; otherwise,
    // capture the opcode's name.
    if (pMessage)
    {
        dwStringLen = wcslen(pMessage) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pMessage);
    }
    else
    {
        dwStringLen = wcslen(pvBuffer->StringVal) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pvBuffer->StringVal);
    }

    // Capture the opcode's value attribute, which is used to look up the opcode's
    // message string.
    pvBuffer = GetProperty(hOpcodes, dwIndex, EvtPublisherMetadataOpcodeValue);
    if (pvBuffer)
    {
        wprintf(L"\tOpcode value is %hu (task: %hu)\n", HIWORD(pvBuffer->UInt32Val), LOWORD(pvBuffer->UInt32Val));
        ((pMessages->pMessage) + dwIndex)->dwValue = pvBuffer->UInt32Val;
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

cleanup:

    if (pvBuffer)
        free(pvBuffer);

    if (pMessage)
        free(pMessage);

    return status;
}


// Print the metadata for a keyword. Capture the message string and mask for use later.
DWORD DumpKeywordProperties(EVT_HANDLE hKeywords, DWORD dwIndex, PMESSAGES pMessages)
{
    LPWSTR pMessage = NULL;
    DWORD status = ERROR_SUCCESS;
    size_t dwStringLen = 0;
    PEVT_VARIANT pvBuffer = NULL;

    pvBuffer = GetProperty(hKeywords, dwIndex, EvtPublisherMetadataKeywordMessageID);
    if (pvBuffer)
    {
        // The value is -1 if the keyword did not specify a message attribute.
        if (-1 != pvBuffer->UInt32Val)
        {
            pMessage = GetMessageString(g_hMetadata, pvBuffer->UInt32Val);
        }

        wprintf(L"\tKeyword message string: %s\n", (pMessage) ? pMessage : L"");
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    pvBuffer = GetProperty(hKeywords, dwIndex, EvtPublisherMetadataKeywordName);
    if (pvBuffer)
    {
        wprintf(L"\tKeyword name is %s\n", pvBuffer->StringVal);
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // Capture the message string if the keyword specified a message string; otherwise,
    // capture the keyword's name.
    if (pMessage)
    {
        dwStringLen = wcslen(pMessage) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pMessage);
    }
    else
    {
        dwStringLen = wcslen(pvBuffer->StringVal) + 1;
        ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
        wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pvBuffer->StringVal);
    }

    // Capture the keyword's mask attribute, which is used to look up the keyword's
    // message string.
    pvBuffer = GetProperty(hKeywords, dwIndex, EvtPublisherMetadataKeywordValue);
    if (pvBuffer)
    {
        wprintf(L"\tKeyword value is %I64u\n", pvBuffer->UInt64Val);
        ((pMessages->pMessage) + dwIndex)->ullMask = pvBuffer->UInt32Val;
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

cleanup:

    if (pvBuffer)
        free(pvBuffer);

    if (pMessage)
        free(pMessage);

    return status;
}


// Get the metadata property for an object in the array.
PEVT_VARIANT GetProperty(EVT_HANDLE handle, DWORD dwIndex, EVT_PUBLISHER_METADATA_PROPERTY_ID PropertyId)
{
    DWORD status = ERROR_SUCCESS;
    PEVT_VARIANT pvBuffer = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;

    if (!EvtGetObjectArrayProperty(handle, PropertyId, dwIndex, 0, dwBufferSize, pvBuffer, &dwBufferUsed))
    {
        status = GetLastError();
        if (ERROR_INSUFFICIENT_BUFFER == status)
        {
            dwBufferSize = dwBufferUsed;
            pvBuffer = (PEVT_VARIANT)malloc(dwBufferSize);
            if (pvBuffer)
            {
                EvtGetObjectArrayProperty(handle, PropertyId, dwIndex, 0, dwBufferSize, pvBuffer, &dwBufferUsed);
            }
            else
            {
                wprintf(L"malloc failed\n");
                status = ERROR_OUTOFMEMORY;
                goto cleanup;
            }
        }

        if (ERROR_SUCCESS != (status = GetLastError()))
        {
            wprintf(L"EvtGetObjectArrayProperty failed with %d\n", status);
        }
    }

cleanup:

    return pvBuffer;
}


// Get the formatted message string.
LPWSTR GetMessageString(EVT_HANDLE hMetadata, DWORD dwMessageId)
{
    LPWSTR pBuffer = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = 0;

    if (!EvtFormatMessage(hMetadata, NULL, dwMessageId, 0, NULL, EvtFormatMessageId, dwBufferSize, pBuffer, &dwBufferUsed))
    {
        status = GetLastError();
        if (ERROR_INSUFFICIENT_BUFFER == status)
        {
            dwBufferSize = dwBufferUsed;
            pBuffer = (LPWSTR)malloc(dwBufferSize * sizeof(WCHAR));

            if (pBuffer)
            {
                EvtFormatMessage(hMetadata, NULL, dwMessageId, 0, NULL, EvtFormatMessageId, dwBufferSize, pBuffer, &dwBufferUsed);
            }
            else
            {
                wprintf(L"malloc failed\n");
            }
        }
        else
        {
            wprintf(L"EvtFormatMessage failed with %u\n", status);
        }
    }

    return pBuffer;
}
#endif




#ifdef GET_PROVIDERS_AND_CHANNELS

#include <string>
#include <vector>
#include <Windows.h>
#include <winevt.h>

#pragma comment(lib, "wevtapi.lib")
using namespace std;

class Channel {
public:
    wstring Message;
    wstring Path;
};
class Provider {
public:
    wstring Name;
    vector<Channel> Channels;
};
// Contains the value and message string for a type, such as
// an opcode or task that the provider defines or uses. If the
// type does not specify a message string, the message member
// contains the value of the type's name attribute.
typedef struct _msgstring
{
    union
    {
        DWORD dwValue;  // Value attribute for opcode, task, and level
        UINT64 ullMask; // Mask attribute for keyword
    };
    LPWSTR pwcsMessage; // Message string or name attribute
} MSG_STRING, * PMSG_STRING;

// Header for the block of value/message string pairs. The dwSize member
// is the size, in bytes, of the block of MSG_STRING structures to which 
// the pMessage member points.
typedef struct _messages
{
    DWORD dwSize;
    PMSG_STRING pMessage;
} MESSAGES, * PMESSAGES;

// Global variables.
EVT_HANDLE g_hMetadata = NULL;
MESSAGES g_ChannelMessages = { 0, NULL };
vector<wstring> publishers;
vector<Provider> providers;


void loadEventPublishers() {
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = ERROR_SUCCESS;
    LPWSTR pwcsProviderName = NULL;
    LPWSTR pTemp = NULL;
    EVT_HANDLE evthandle_providers = EvtOpenPublisherEnum(NULL, 0);
    if (!evthandle_providers) {
        wprintf(L"EventSource::loadEventPublishers()> could not open publisher enum\n");
        exit(1);
    }

    while (true) {
        status = ERROR_SUCCESS;
        if (!EvtNextPublisherId(evthandle_providers, dwBufferSize, pwcsProviderName, &dwBufferUsed))
        {
            status = GetLastError();
            if (ERROR_NO_MORE_ITEMS == status)
            {
                break;
            }
            else if (ERROR_INSUFFICIENT_BUFFER == status)
            {
                dwBufferSize = dwBufferUsed;
                pTemp = (LPWSTR)realloc(pwcsProviderName, dwBufferSize * sizeof(WCHAR));
                if (pTemp)
                {
                    pwcsProviderName = pTemp;
                    pTemp = NULL;
                    if (EvtNextPublisherId(evthandle_providers, dwBufferSize, pwcsProviderName, &dwBufferUsed)) {
                        status = ERROR_SUCCESS;
                    }
                    else {
                        status = GetLastError();
                    }
                }
                else
                {
                    wprintf(L"EventSource::loadEventPublishers()> realloc failed\n");
                    ::exit(1);
                }
            }

            if (ERROR_SUCCESS != status)
            {
                wprintf(L"EventSource::loadEventPublishers()> EvtNextPublisherId failed with %d\n", status);
                ::exit(1);
            }
        }

        if (ERROR_SUCCESS == status) {
            // success
            wprintf(L"%s\n", pwcsProviderName);
            publishers.push_back(pwcsProviderName);
            Provider prov;
            prov.Name = wstring(pwcsProviderName);
            providers.push_back(prov);
        }
    }
    EvtClose(evthandle_providers);
    if (pwcsProviderName) {
        free(pwcsProviderName);
    }
}

// Get the metadata property for an object in the array.
PEVT_VARIANT GetProperty(EVT_HANDLE handle, DWORD dwIndex, EVT_PUBLISHER_METADATA_PROPERTY_ID PropertyId)
{
    DWORD status = ERROR_SUCCESS;
    PEVT_VARIANT pvBuffer = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;

    if (!EvtGetObjectArrayProperty(handle, PropertyId, dwIndex, 0, dwBufferSize, pvBuffer, &dwBufferUsed))
    {
        status = GetLastError();
        if (ERROR_INSUFFICIENT_BUFFER == status)
        {
            dwBufferSize = dwBufferUsed;
            pvBuffer = (PEVT_VARIANT)malloc(dwBufferSize);
            if (pvBuffer)
            {
                EvtGetObjectArrayProperty(handle, PropertyId, dwIndex, 0, dwBufferSize, pvBuffer, &dwBufferUsed);
            }
            else
            {
                wprintf(L"malloc failed\n");
                status = ERROR_OUTOFMEMORY;
                goto cleanup;
            }
        }

        if (ERROR_SUCCESS != (status = GetLastError()))
        {
            wprintf(L"EvtGetObjectArrayProperty failed with %d\n", status);
        }
    }

cleanup:

    return pvBuffer;
}


// Get the formatted message string.
LPWSTR GetMessageString(EVT_HANDLE hMetadata, DWORD dwMessageId)
{
    LPWSTR pBuffer = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = 0;

    if (!EvtFormatMessage(hMetadata, NULL, dwMessageId, 0, NULL, EvtFormatMessageId, dwBufferSize, pBuffer, &dwBufferUsed))
    {
        status = GetLastError();
        if (ERROR_INSUFFICIENT_BUFFER == status)
        {
            dwBufferSize = dwBufferUsed;
            pBuffer = (LPWSTR)malloc(dwBufferSize * sizeof(WCHAR));

            if (pBuffer)
            {
                EvtFormatMessage(hMetadata, NULL, dwMessageId, 0, NULL, EvtFormatMessageId, dwBufferSize, pBuffer, &dwBufferUsed);
            }
            else
            {
                wprintf(L"malloc failed\n");
            }
        }
        else
        {
            wprintf(L"EvtFormatMessage failed with %u\n", status);
        }
    }

    return pBuffer;
}


DWORD DumpChannelProperties(EVT_HANDLE hChannels, DWORD dwIndex, PMESSAGES pMessages, Channel& channel)
{
    LPWSTR pMessage = NULL;
    DWORD status = ERROR_SUCCESS;
    size_t dwStringLen = 0;
    PEVT_VARIANT pvBuffer = NULL;

    pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferenceMessageID);
    if (pvBuffer)
    {
        // The value is -1 if the channel did not specify a message attribute.
        if (-1 != pvBuffer->UInt32Val)
        {
            pMessage = GetMessageString(g_hMetadata, pvBuffer->UInt32Val);
        }

        wprintf(L"\tChannel message string: %s\n", (pMessage) ? pMessage : L"");
        if (pMessage)
            channel.Message = wstring(pMessage);
        
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    // This is the channel name. You can use it to call the EvtOpenChannelConfig function
    // to get the channel's configuration information.
    pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferencePath);
    if (pvBuffer)
    {
        wprintf(L"\tChannel path is %s\n", pvBuffer->StringVal);
        if (pvBuffer->StringVal)
            channel.Path = wstring(pvBuffer->StringVal);
    }
    else
    {
        status = GetLastError();
        goto cleanup;
    }

    //// Capture the message string if the channel specified a message string; otherwise,
    //// capture the channel's name.
    //if (pMessage)
    //{
    //    dwStringLen = wcslen(pMessage) + 1;
    //    ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
    //    wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pMessage);
    //}
    //else
    //{
    //    dwStringLen = wcslen(pvBuffer->StringVal) + 1;
    //    ((pMessages->pMessage) + dwIndex)->pwcsMessage = (LPWSTR)malloc(dwStringLen * sizeof(WCHAR));
    //    wcscpy_s(((pMessages->pMessage) + dwIndex)->pwcsMessage, dwStringLen, pvBuffer->StringVal);
    //}

    //pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferenceIndex);
    //if (pvBuffer)
    //{
    //    wprintf(L"\tChannel index is %lu\n", pvBuffer->UInt32Val);
    //}
    //else
    //{
    //    status = GetLastError();
    //    goto cleanup;
    //}

    //// Capture the channel's value attribute, which is used to look up the channel's
    //// message string.
    //pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferenceID);
    //if (pvBuffer)
    //{
    //    wprintf(L"\tChannel ID is %lu\n", pvBuffer->UInt32Val);
    //    ((pMessages->pMessage) + dwIndex)->dwValue = pvBuffer->UInt32Val;
    //}
    //else
    //{
    //    status = GetLastError();
    //    goto cleanup;
    //}

    //pvBuffer = GetProperty(hChannels, dwIndex, EvtPublisherMetadataChannelReferenceFlags);
    //if (pvBuffer)
    //{
    //    if (EvtChannelReferenceImported == (EvtChannelReferenceImported & pvBuffer->UInt32Val))
    //        wprintf(L"\tChannel is imported\n");
    //}
    //else
    //{
    //    status = GetLastError();
    //    goto cleanup;
    //}

cleanup:

    if (pvBuffer)
        free(pvBuffer);

    if (pMessage)
        free(pMessage);

    return status;
}

DWORD PrintProviderProperty(EVT_HANDLE hMetadata, PEVT_VARIANT pProperty, Provider& provider)
{
    UNREFERENCED_PARAMETER(hMetadata);

    DWORD status = ERROR_SUCCESS;
    WCHAR wszProviderGuid[50];
    DWORD dwArraySize = 0;
    DWORD dwBlockSize = 0;
    LPWSTR pMessage = NULL;

    // We got the handle to all the channels defined in the channels section
    // of the manifest. Get the size of the array of channel objects and 
    // allocate the messages block that will contain the value and
    // message string for each channel. The strings are used to retrieve
    // display names for the channel referenced in an event definition.
    wprintf(L"Channels:\n");
    if (EvtGetObjectArraySize(pProperty->EvtHandleVal, &dwArraySize))
    {
        // You always get a handle to the array but the array can be empty.
        if (dwArraySize > 0)
        {
            dwBlockSize = sizeof(MSG_STRING) * dwArraySize;
            g_ChannelMessages.pMessage = (PMSG_STRING)malloc(dwBlockSize);
            if (g_ChannelMessages.pMessage)
            {
                RtlZeroMemory(g_ChannelMessages.pMessage, dwBlockSize);
                g_ChannelMessages.dwSize = dwBlockSize;

                // For each channel, print its metadata.
                for (DWORD i = 0; i < dwArraySize; i++)
                {
                    Channel channel;
                    if (status = DumpChannelProperties(pProperty->EvtHandleVal, i, &g_ChannelMessages, channel))
                        break;
                    provider.Channels.push_back(channel);
                }
            }
            else
            {
                status = ERROR_OUTOFMEMORY;
                wprintf(L"g_pChannelMessages allocation error\n");
            }
        }
    }
    else
    {
        status = GetLastError();
    }

    EvtClose(pProperty->EvtHandleVal);
    return status;
}

//DWORD PrintProviderProperties(EVT_HANDLE hMetadata)
DWORD PrintProviderProperties(EVT_HANDLE hMetadata, Provider& provider)
{
    PEVT_VARIANT pProperty = NULL;  // Contains the metadata property
    PEVT_VARIANT pTemp = NULL;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD status = ERROR_SUCCESS;

    // Get the metadata property. If the buffer is not big enough, reallocate the buffer.
    if (!EvtGetPublisherMetadataProperty(hMetadata, EvtPublisherMetadataChannelReferences, 0, dwBufferSize, pProperty, &dwBufferUsed))
    {
        status = GetLastError();
        if (ERROR_INSUFFICIENT_BUFFER == status)
        {
            dwBufferSize = dwBufferUsed;
            pTemp = (PEVT_VARIANT)realloc(pProperty, dwBufferSize);
            if (pTemp)
            {
                pProperty = pTemp;
                pTemp = NULL;
                EvtGetPublisherMetadataProperty(hMetadata, EvtPublisherMetadataChannelReferences, 0, dwBufferSize, pProperty, &dwBufferUsed);
            }
            else
            {
                wprintf(L"realloc failed\n");
                status = ERROR_OUTOFMEMORY;
                goto cleanup;
            }
        }

        if (ERROR_SUCCESS != (status = GetLastError()))
        {
            wprintf(L"EvtGetPublisherMetadataProperty failed with %d\n", GetLastError());
            goto cleanup;
        }
    }

    status = PrintProviderProperty(hMetadata, pProperty, provider);

    RtlZeroMemory(pProperty, dwBufferUsed);


cleanup:

    if (pProperty)
        free(pProperty);

    return status;
}

// Free the memory for each message string in the messages block
// and then free the messages block.
void FreeMessages(PMESSAGES pMessages)
{
    DWORD dwCount = pMessages->dwSize / sizeof(MSG_STRING);

    for (DWORD i = 0; i < dwCount; i++)
    {
        free(((pMessages->pMessage) + i)->pwcsMessage);
        ((pMessages->pMessage) + i)->pwcsMessage = NULL;
    }

    free(pMessages->pMessage);
}

//void printChannelsForProviders(LPCWSTR pwszPublisherName) {
void printChannelsForProviders(Provider& provider) {
    DWORD status = ERROR_SUCCESS;

    // Get a handle to the provider's metadata. You can specify the provider's name
    // if the provider is registered on the computer or the full path to an archived 
    // log file (archived log files contain the provider's metadata). Specify the locale 
    // identifier if you want the localized strings returned in a locale other than
    // the locale of the current thread.
    g_hMetadata = EvtOpenPublisherMetadata(NULL,
        provider.Name.c_str(),
        NULL,
        0, //MAKELCID(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), 0), 
        0);
    if (NULL == g_hMetadata)
    {
        wprintf(L"EvtOpenPublisherMetadata failed with %d\n", GetLastError());
        goto cleanup;
    }

    wprintf(L"Provider Metadata for %s\n\n", provider.Name.c_str());
    status = PrintProviderProperties(g_hMetadata, provider);

    //wprintf(L"\nEvent Metadata for %s\n\n", pwszPublisherName);
    //status = PrintProviderEvents(g_hMetadata);

    // disable for a moment:
    //if (g_ChannelMessages.pMessage)
    //    FreeMessages(&g_ChannelMessages);

cleanup:

    if (g_hMetadata)
        EvtClose(g_hMetadata);

}

int main() {
    loadEventPublishers();

    //LPCWSTR pwszPublisherName = L"Microsoft-Windows-WinNat";
    //printChannelsForProviders(pwszPublisherName);

    //for (auto& pub : publishers) {
    //    wprintf(L"*** %s ***\n", pub.c_str());
    //    if (wstring(L"Microsoft-Windows-NetworkProfileTriggerProvider") == pub) {
    //        auto breakpoint = 1 + 1;
    //    }
    //    printChannelsForProviders(pub.c_str());
    //    wprintf(L"\n");
    //}

    for (auto& prov: providers) {
        wprintf(L"*** %s ***\n", prov.Name.c_str());
        printChannelsForProviders(prov);
        wprintf(L"\n");
    }

    auto breakpoint2 = 1 + 1;

    wprintf(L"Non-empty:\n");
    for (auto& prov : providers) {
        if (prov.Channels.size() > 0) {
            bool first = true;
            for (auto& chan : prov.Channels) {
                if (chan.Message.length() > 0) {
                    if (first) {
                        wprintf(L"%s:\n", prov.Name.c_str());
                        first = false;
                    }
                    wprintf(L"   %s -> %s\n", chan.Message.c_str(), chan.Path.c_str());
                }
            }
        }
    }
}
#endif
