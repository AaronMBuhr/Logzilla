#pragma once

#include <memory>
#include <string>
#include <vector>
#include <Windows.h>
#include <winevt.h>
#include "EventLogEvent.h"
#include "ChannelEventHandlerBase.h"

using namespace std;

namespace Syslog_agent {
	class EventLogSubscription {
	public:
		EventLogSubscription(wstring subscription_name, wstring channel, wstring query, wstring bookmark_xml, unique_ptr<ChannelEventHandlerBase> event_handler) :
			subscription_name_(subscription_name),
			channel_(channel),
			query_(query),
			bookmark_(NULL),
			bookmark_xml_(bookmark_xml),
			event_handler_(std::move(event_handler)),
			subscription_handle_(NULL),
			subscription_active_(false) { }
		EventLogSubscription(EventLogSubscription&& source) noexcept;
		~EventLogSubscription();
		void subscribe(const wchar_t* bookmark_xml);
		void cancelSubscription();
		void saveBookmark();
		wstring getBookmark() { return bookmark_xml_; }
		wstring getName() { return subscription_name_; }
		wstring getChannel() { return channel_; }
	private:
		static DWORD WINAPI handleSubscriptionEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID pContext, EVT_HANDLE hEvent);
		EventLogSubscription(const EventLogSubscription&);
		wstring subscription_name_;
		wstring channel_;
		wstring query_;
		wstring bookmark_xml_;
		unique_ptr<ChannelEventHandlerBase> event_handler_;
		EVT_HANDLE subscription_handle_;
		EVT_HANDLE bookmark_;
		bool subscription_active_;

	};
}