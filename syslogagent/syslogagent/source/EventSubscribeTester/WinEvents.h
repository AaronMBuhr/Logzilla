#pragma once
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <winevt.h>

class WinEvents
{
public: 
	const size_t RENDERED_BUFFER_INITIAL_SIZE_ = 96000;
	typedef void (*EventReceivedCallbackType)(LPWSTR rendered_buffer, DWORD buffer_used);
	WinEvents();
	static void Test();
	static ULONG QueryEvents(__in PCWSTR Channel, __in PCWSTR XPath);
	static void DoSubscribe(LPCWSTR pwsPath, LPCWSTR pwsQuery, EventReceivedCallbackType callback);

private:
	typedef DWORD (WINAPI *WinSubscriptionCallbackType)(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID pContext, EVT_HANDLE hEvent);
	static DWORD WINAPI EventReceiptCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID pContext, EVT_HANDLE hEvent);

	static WinEvents* singleton_;
	EVT_HANDLE subscription_;
	EventReceivedCallbackType event_received_callback_;
	size_t rendered_buffer_size_;
	LPWSTR rendered_buffer_;
};

