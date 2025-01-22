#include "stdafx.h"
#include "EventLogSubscription.h"
#include "Logger.h"
#include "Util.h"

#pragma comment(lib, "wevtapi.lib")

namespace Syslog_agent {

    // Move constructor
    EventLogSubscription::EventLogSubscription(EventLogSubscription&& source) noexcept
        : subscription_name_(source.subscription_name_),
        channel_(source.channel_),
        query_(source.query_),
        bookmark_xml_(source.bookmark_xml_),
        event_handler_(std::move(source.event_handler_)),
        bookmark_(source.bookmark_),
        subscription_handle_(source.subscription_handle_),
        subscription_active_(false)
    {
        source.bookmark_ = NULL;
        source.subscription_handle_ = NULL;

        if (source.subscription_active_) {
            source.subscription_active_ = false;
            auto bookmark = source.getBookmark();
            subscribe(bookmark);
        }
    }

    // Destructor
    EventLogSubscription::~EventLogSubscription() {
        if (subscription_active_) {
            cancelSubscription();
        }
        if (bookmark_) {
            EvtClose(bookmark_);
        }
    }

    void EventLogSubscription::subscribe(const wstring& bookmark_xml) {
        char channel_buf[1024];
        Util::wstr2str(channel_buf, sizeof(channel_buf), channel_.c_str());
        Logger::debug("EventLogSubscription::subscribe()> Subscribing to %s\n", channel_buf);

        if (subscription_active_) {
            Logger::recoverable_error("EventLogSubscription::subscribe()> subscription already active\n");
            return;
        }

        bookmark_xml_ = bookmark_xml;
        if (bookmark_) {
            EvtClose(bookmark_);
            bookmark_ = NULL;
        }

        EVT_SUBSCRIBE_FLAGS flags;
        EVT_HANDLE subscribe_bookmark = NULL;  // Separate bookmark for subscription
        if (bookmark_xml_.empty()) {
            flags = EvtSubscribeStartAtOldestRecord;
            bookmark_ = EvtCreateBookmark(NULL);  // Create new empty bookmark for tracking
            if (bookmark_ == NULL) {
                auto error = GetLastError();
                Logger::recoverable_error("EventLogSubscription::subscribe()> Failed to create empty bookmark (error %d)\n", error);
                return;
            }
            Logger::debug2("EventLogSubscription::subscribe()> Created new empty bookmark %p for %s\n", 
                bookmark_, channel_buf);
            Logger::debug("EventLogSubscription::subscribe()> No bookmark, subscribing to all events from start for %s\n", 
                channel_buf);
        } else {
            flags = EvtSubscribeStartAfterBookmark;
            bookmark_ = EvtCreateBookmark(bookmark_xml_.c_str());
            if (bookmark_ == NULL) {
                auto error = GetLastError();
                Logger::warn("EventLogSubscription::subscribe()> Failed to create bookmark for %s (error %d), falling back to all events from start\n",
                    channel_buf, error);
                flags = EvtSubscribeStartAtOldestRecord;
                bookmark_ = EvtCreateBookmark(NULL);  // Create new empty bookmark for tracking
                if (bookmark_ == NULL) {
                    auto error = GetLastError();
                    Logger::recoverable_error("EventLogSubscription::subscribe()> Failed to create empty bookmark (error %d)\n", error);
                    return;
                }
                Logger::debug2("EventLogSubscription::subscribe()> Created new empty bookmark %p for %s after bookmark load failed\n", 
                    bookmark_, channel_buf);
            } else {
                Logger::debug2("EventLogSubscription::subscribe()> Created bookmark %p from XML for %s\n", 
                    bookmark_, channel_buf);
                Logger::debug("EventLogSubscription::subscribe()> Using bookmark for %s\n", channel_buf);
                subscribe_bookmark = bookmark_;  // Use existing bookmark for subscription
            }
        }

        Logger::debug2("EventLogSubscription::subscribe()> Attempting subscription to %s with flags %d and bookmark %p (tracking bookmark %p)\n", 
            channel_buf, flags, subscribe_bookmark, bookmark_);

        subscription_handle_ = EvtSubscribe(
            NULL,
            NULL,
            channel_.c_str(),
            query_.c_str(),
            subscribe_bookmark,  // Only pass bookmark for EvtSubscribeStartAfterBookmark
            this,
            EventLogSubscription::handleSubscriptionEvent,
            flags
        );

        if (subscription_handle_ == NULL) {
            auto status = GetLastError();
            Logger::critical("EventLogSubscription::subscribe()> could not subscribe to %s (error %d)\n",
                channel_buf, status);
            return;
        }

        subscription_active_ = true;
        Logger::debug2("EventLogSubscription::subscribe()> Successfully subscribed to %s\n", channel_buf);
    }

    void EventLogSubscription::cancelSubscription() {
        if (subscription_active_) {
            saveBookmark();
            if (subscription_handle_) {
                if (!EvtClose(subscription_handle_)) {
                    Logger::recoverable_error("EventLogSubscription::cancelSubscription()> "
                        "Failed to close subscription handle (error %d)\n",
                        GetLastError());
                }
                subscription_handle_ = NULL;
            }
            subscription_active_ = false;
        }
    }

    DWORD WINAPI EventLogSubscription::handleSubscriptionEvent(
        EVT_SUBSCRIBE_NOTIFY_ACTION action,
        PVOID pContext,
        EVT_HANDLE hEvent
    ) {
        EventLogSubscription* subscription = reinterpret_cast<EventLogSubscription*>(pContext);
        char channel_buf[1024];
        Util::wstr2str(channel_buf, sizeof(channel_buf), subscription->channel_.c_str());

        if (!subscription->subscription_active_) {
            Logger::debug2("handleSubscriptionEvent()> Subscription not active for %s, ignoring event\n", 
                channel_buf);
            return ERROR_SUCCESS;
        }

        switch (action) {
        case EvtSubscribeActionError:
        {
            DWORD errorCode = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(hEvent));
            if (ERROR_EVT_QUERY_RESULT_STALE == errorCode) {
                Logger::recoverable_error(
                    "EventLogSubscription::handleSubscriptionEvent()> The subscription callback "
                    "for %s was notified that event records are missing.\n", channel_buf);
            }
            else {
                Logger::recoverable_error(
                    "EventLogSubscription::handleSubscriptionEvent()> The subscription callback "
                    "for %s received Win32 error: %lu\n", channel_buf, errorCode);
            }
            break;
        }

        case EvtSubscribeActionDeliver:
        {
            Logger::debug3("handleSubscriptionEvent()> Got event for %s with bookmark %p\n", 
                channel_buf, subscription->bookmark_);

            if (subscription->bookmark_ == NULL) {
                Logger::recoverable_error(
                    "EventLogSubscription::handleSubscriptionEvent()> Null bookmark handle for %s\n", 
                    channel_buf);
                break;
            }

            // Wrap the hEvent in an EventLogEvent object
            EventLogEvent processed_event(hEvent);
            try {
                Logger::debug3("handleSubscriptionEvent()> Processing event for %s\n", channel_buf);
                subscription->event_handler_->handleEvent(subscription->subscription_name_.c_str(), processed_event);
                Logger::debug3("handleSubscriptionEvent()> Successfully processed event for %s\n", channel_buf);

                if (!EvtUpdateBookmark(subscription->bookmark_, hEvent)) {
                    auto error = GetLastError();
                    Logger::recoverable_error(
                        "EventLogSubscription::handleSubscriptionEvent()> could not update bookmark for %s (error %d)\n",
                        channel_buf, error);
                } else {
                    Logger::debug3("handleSubscriptionEvent()> Successfully updated bookmark for %s\n", channel_buf);
                }
            }
            catch (const std::exception& e) {
                Logger::recoverable_error(
                    "EventLogSubscription::handleSubscriptionEvent()> Exception in event handler for %s: %s\n", 
                    channel_buf, e.what());
            }
            break;
        }

        default:
            Logger::recoverable_error(
                "EventLogSubscription::handleSubscriptionEvent()> Unknown action for %s.\n",
                channel_buf);
        }

        return ERROR_SUCCESS;
    }

    void EventLogSubscription::saveBookmark() {
        if (!subscription_active_ || !bookmark_) {
            return;
        }

        DWORD buffer_used;
        DWORD property_count;
        std::vector<wchar_t> xml_buffer(2048);

        if (xml_buffer.size() * sizeof(wchar_t) > MAXDWORD) {
            Logger::recoverable_error("EventLogSubscription::saveBookmark()> "
                "Buffer size exceeds DWORD maximum\n");
            return;
        }

        if (!EvtRender(NULL, bookmark_, EvtRenderBookmark,
            static_cast<DWORD>(xml_buffer.size() * sizeof(wchar_t)),
            xml_buffer.data(),
            &buffer_used, &property_count))
        {
            auto status = GetLastError();
            if (status == ERROR_INSUFFICIENT_BUFFER) {
                xml_buffer.resize((buffer_used / sizeof(wchar_t)) + 1);

                if (xml_buffer.size() * sizeof(wchar_t) > MAXDWORD) {
                    Logger::recoverable_error("EventLogSubscription::saveBookmark()> "
                        "Resized buffer exceeds DWORD maximum\n");
                    return;
                }

                if (!EvtRender(NULL, bookmark_, EvtRenderBookmark,
                    static_cast<DWORD>(xml_buffer.size() * sizeof(wchar_t)),
                    xml_buffer.data(),
                    &buffer_used, &property_count))
                {
                    Logger::recoverable_error("EventLogSubscription::saveBookmark()> error %d on retry\n",
                        GetLastError());
                    return;
                }
            }
            else {
                Logger::recoverable_error("EventLogSubscription::saveBookmark()> error %d\n", status);
                return;
            }
        }

        xml_buffer[buffer_used / sizeof(wchar_t)] = 0;
        bookmark_xml_ = wstring(xml_buffer.data());
    }

} // namespace Syslog_agent