#pragma once

#include <memory>
#include <string>
#include <vector>
#include <Windows.h>
#include <winevt.h>
#include "EventLogEvent.h"
// Include your IEventHandler interface:
#include "IEventHandler.h"  

namespace Syslog_agent {
    using namespace std;

    class EventLogSubscription {
    public:
        EventLogSubscription(
            const wstring& subscription_name,
            const wstring& channel,
            const wstring& query,
            const wstring& bookmark_xml,
            const bool only_while_running,
            unique_ptr<IEventHandler> event_handler)
            : subscription_name_(subscription_name),
            channel_(channel),
            query_(query),
            bookmark_xml_(bookmark_xml),
            only_while_running_(only_while_running),
            event_handler_(move(event_handler)),
            bookmark_(NULL),
            subscription_handle_(NULL),
            subscription_active_(false)
        {
        }

        EventLogSubscription(EventLogSubscription&& source) noexcept;
        ~EventLogSubscription();

        void subscribe(const wstring& bookmark_xml, const bool only_while_running);
        void cancelSubscription();
        void saveBookmark();
        wstring getBookmark() const { return bookmark_xml_; }
        wstring getName() const { return subscription_name_; }
        wstring getChannel() const { return channel_; }

    private:
        static DWORD WINAPI handleSubscriptionEvent(
            EVT_SUBSCRIBE_NOTIFY_ACTION action,
            PVOID pContext,
            EVT_HANDLE hEvent);

        // Disable copy constructor and assignment
        EventLogSubscription(const EventLogSubscription&) = delete;
        EventLogSubscription& operator=(const EventLogSubscription&) = delete;

        wstring subscription_name_;
        wstring channel_;
        wstring query_;
        wstring bookmark_xml_;
        bool only_while_running_;

        // Now store a pointer to IEventHandler instead of ChannelEventHandlerBase
        unique_ptr<IEventHandler> event_handler_;

        EVT_HANDLE bookmark_;
        EVT_HANDLE subscription_handle_;
        bool subscription_active_;
    };
}
