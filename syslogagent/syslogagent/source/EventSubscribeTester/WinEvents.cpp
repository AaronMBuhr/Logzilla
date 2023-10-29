#include "WinEvents.h"

#pragma comment(lib, "wevtapi.lib")

WinEvents* WinEvents::singleton_ = new WinEvents();

WinEvents::WinEvents() {
    rendered_buffer_size_ = RENDERED_BUFFER_INITIAL_SIZE_;
    rendered_buffer_ = (LPWSTR)malloc(rendered_buffer_size_);
}

void WinEvents::Test() {

    //const wchar_t* path = L"";
    //const wchar_t* query =
    //    L"<QueryList>"
    //    "<Query Id=\"0\">"
    //    "<Select Path = \"Application\">"
    //    "*[System[(Level <= 3)]"
    //    "</Select>"
    //    "</Query>"
    //    "</QueryList>";

    const wchar_t* path = L"Security";
    const wchar_t* query = L"*";

    EVT_HANDLE hEvent = EvtQuery(NULL, (LPWSTR) path, NULL, EvtQueryChannelPath);
    if (!hEvent) {
        printf("Error %d\n", GetLastError());
    }
    else {
        wchar_t buf[96000];
        DWORD dwBufferUsed;
        DWORD dwPropertyCount;
        EvtRender(NULL, hEvent, EvtRenderEventXml, sizeof(buf), buf, &dwBufferUsed, &dwPropertyCount);
        wprintf(L"%s\n\n", buf);

        //wchar_t newbuf[96000];
        //DWORD newbufused;
        //auto result = EvtGetQueryInfo(hEvent, EvtQueryNames, 96000, (PEVT_VARIANT) newbuf, &newbufused);

        //auto err = GetLastError();

        EVT_HANDLE hEvents[10];
        DWORD dwReturned;
        auto next = EvtNext(hEvent, 10, hEvents, INFINITE, 0, &dwReturned);
        auto err = GetLastError();


        auto wait = 1 + 1;
    }
}


void WinEvents::DoSubscribe(LPCWSTR pwsPath, LPCWSTR pwsQuery, EventReceivedCallbackType callback) {
    DWORD status = ERROR_SUCCESS;

    WinEvents::singleton_->event_received_callback_ = callback;
    WinEvents::singleton_->subscription_ = EvtSubscribe(NULL, NULL, pwsPath, pwsQuery, NULL, (PVOID) 0x1234,
        EventReceiptCallback, EvtSubscribeStartAtOldestRecord);
    if (NULL == WinEvents::singleton_->subscription_)
    {
        status = GetLastError();

        if (ERROR_EVT_CHANNEL_NOT_FOUND == status)
            wprintf(L"Channel %s was not found.\n", pwsPath);
        else if (ERROR_EVT_INVALID_QUERY == status)
            // You can call EvtGetExtendedStatus to get information as to why the query is not valid.
            wprintf(L"The query \"%s\" is not valid.\n", pwsQuery);
        else
            wprintf(L"EvtSubscribe failed with %lu.\n", status);

        goto cleanup;
    }

    wprintf(L"Hit any key to quit\n\n");
    while (!_kbhit())
        Sleep(10);

cleanup:

    if (WinEvents::singleton_->subscription_)
        EvtClose(WinEvents::singleton_->subscription_);
}


DWORD WINAPI WinEvents::EventReceiptCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID pContext, EVT_HANDLE hEvent) {
    UNREFERENCED_PARAMETER(pContext);

    DWORD status = ERROR_SUCCESS;
    DWORD dwBufferUsed = 0;
    DWORD dwPropertyCount = 0;

    switch (action)
    {
        // You should only get the EvtSubscribeActionError action if your subscription flags 
        // includes EvtSubscribeStrict and the channel contains missing event records.
    case EvtSubscribeActionError:
        if (ERROR_EVT_QUERY_RESULT_STALE == (DWORD)hEvent)
        {
            wprintf(L"The subscription callback was notified that event records are missing.\n");
            // Handle if this is an issue for your application.
        }
        else
        {
            wprintf(L"The subscription callback received the following Win32 error: %lu\n", (DWORD)hEvent);
        }
        break;

    case EvtSubscribeActionDeliver:

        if (!EvtRender(NULL, hEvent, EvtRenderEventXml, WinEvents::singleton_->rendered_buffer_size_, WinEvents::singleton_->rendered_buffer_, &dwBufferUsed, &dwPropertyCount))
        {
            if (ERROR_INSUFFICIENT_BUFFER == (status = GetLastError()))
            {
                WinEvents::singleton_->rendered_buffer_size_ = dwBufferUsed;
                WinEvents::singleton_->rendered_buffer_ = (LPWSTR)malloc(WinEvents::singleton_->rendered_buffer_size_);
                if (WinEvents::singleton_->rendered_buffer_)
                {
                    EvtRender(NULL, hEvent, EvtRenderEventXml, WinEvents::singleton_->rendered_buffer_size_, WinEvents::singleton_->rendered_buffer_, &dwBufferUsed, &dwPropertyCount);
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
                wprintf(L"EvtRender failed with %d\n", status);
                goto cleanup;
            }
        }

        WinEvents::singleton_->event_received_callback_(WinEvents::singleton_->rendered_buffer_, dwBufferUsed);
        break;

    default:
        wprintf(L"SubscriptionCallback: Unknown action.\n");
    }

cleanup:

    if (ERROR_SUCCESS != status)
    {
        // End subscription - Use some kind of IPC mechanism to signal
        // your application to close the subscription handle.
        EvtClose(WinEvents::singleton_->subscription_);
    }

    return status; // The service ignores the returned status.
}




ULONG
WinEvents::QueryEvents(
    __in PCWSTR Channel,
    __in PCWSTR XPath
)

/*++
Routine Description:
    This function queries events from the given channel and prints their
    description to the standard output.
Arguments:
    Channel - Supplies the name of the channel whose events will be displayed.
    XPath - Supplies the XPath expression to filter events with.
Return Value:
    Win32 error code indicating if querying was successful.
--*/

{
    PWSTR Buffer;
    ULONG BufferSize;
    ULONG BufferSizeNeeded;
    ULONG Count;
    EVT_HANDLE Event;
    EVT_HANDLE Query;
    ULONG Status;

    //
    // Create the query.
    //

    Query = EvtQuery(NULL, Channel, XPath, EvtQueryChannelPath);
    if (Query == NULL) {
        return GetLastError();
    }

    //
    // Read each event and render it as XML.
    //

    Buffer = NULL;
    BufferSize = 0;
    BufferSizeNeeded = 0;

    while (EvtNext(Query, 1, &Event, INFINITE, 0, &Count) != FALSE) {

        do {
            if (BufferSizeNeeded > BufferSize) {
                free(Buffer);
                BufferSize = BufferSizeNeeded;
                Buffer = (PWSTR) malloc(BufferSize);
                if (Buffer == NULL) {
                    Status = ERROR_OUTOFMEMORY;
                    BufferSize = 0;
                    break;
                }
            }

            if (EvtRender(NULL,
                Event,
                EvtRenderEventXml,
                BufferSize,
                Buffer,
                &BufferSizeNeeded,
                &Count) != FALSE) {
                Status = ERROR_SUCCESS;
            }
            else {
                Status = GetLastError();
            }
        } while (Status == ERROR_INSUFFICIENT_BUFFER);

        //
        // Display either the event xml or an error message.
        //
        auto wait = 1 + 1;

        if (Status == ERROR_SUCCESS) {
            wprintf(L"%s\n", Buffer);
        }
        else {
            wprintf(L"Error rendering event.\n");
        }

        EvtClose(Event);
    }

    //
    // When EvtNextChannelPath returns ERROR_NO_MORE_ITEMS, we have actually
    // iterated through all matching events and thus succeeded.
    //

    Status = GetLastError();
    if (Status == ERROR_NO_MORE_ITEMS) {
        Status = ERROR_SUCCESS;
    }

    //
    // Free resources.
    //

    EvtClose(Query);
    free(Buffer);

    return Status;
}
