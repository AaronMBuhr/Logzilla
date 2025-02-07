#include "stdafx.h"
#include "EventLogSubscription.h"
#include "Logger.h"
#include "Registry.h"
#include "SlidingWindowMetrics.h"
#include "Util.h"

#pragma comment(lib, "wevtapi.lib")

namespace Syslog_agent {

    // Move constructor
    EventLogSubscription::EventLogSubscription(EventLogSubscription&& source) noexcept
        : subscription_name_(source.subscription_name_),
        channel_(source.channel_),
        query_(source.query_),
        event_handler_(std::move(source.event_handler_)),
        bookmark_(source.bookmark_),
		only_while_running_(source.only_while_running_),
        subscription_handle_(source.subscription_handle_),
        subscription_active_(false),
        bookmark_modified_(source.bookmark_modified_),
        last_bookmark_save_(source.last_bookmark_save_),
        events_since_last_save_(0)
    {
        memcpy(bookmark_xml_buffer_, source.bookmark_xml_buffer_, sizeof(bookmark_xml_buffer_));

        source.bookmark_ = NULL;
        source.subscription_handle_ = NULL;
        source.bookmark_xml_buffer_[0] = 0;

        if (source.subscription_active_) {
            source.subscription_active_ = false;
            subscribe(bookmark_xml_buffer_, source.only_while_running_);
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

    void EventLogSubscription::subscribe(const wstring& bookmark_xml, const bool only_while_running) {
        char channel_buf[1024];
        Util::wstr2str(channel_buf, sizeof(channel_buf), channel_.c_str());
        Logger::debug("EventLogSubscription::subscribe()> Subscribing to %s\n", channel_buf);

        if (subscription_active_) {
            Logger::recoverable_error("EventLogSubscription::subscribe()> subscription already active\n");
            return;
        }

        memcpy(bookmark_xml_buffer_, bookmark_xml.c_str(), sizeof(bookmark_xml_buffer_));
        only_while_running_ = only_while_running;
        if (bookmark_) {
            EvtClose(bookmark_);
            bookmark_ = NULL;
        }

        EVT_SUBSCRIBE_FLAGS flags;
        EVT_HANDLE subscribe_bookmark = NULL;  // Separate bookmark for subscription

        // Check if we're in catch-up mode (has bookmark or wants all past events)
        // bool catchup_mode = (bookmark_xml_buffer_[0] != 0) || (query_.find(L"catch_up=true") != wstring::npos);

        if (!only_while_running) {
            if (bookmark_xml_buffer_[0] == 0) {
                flags = EvtSubscribeStartAtOldestRecord;
                bookmark_ = EvtCreateBookmark(NULL);  // Create new empty bookmark for tracking
                if (bookmark_ == NULL) {
                    auto error = GetLastError();
                    Logger::recoverable_error("EventLogSubscription::subscribe()> Failed to create empty bookmark (error %d)\n", error);
                    return;
                }
                Logger::debug2("EventLogSubscription::subscribe()> Created new empty bookmark %p for %s\n", 
                    bookmark_, channel_buf);
                Logger::debug("EventLogSubscription::subscribe()> Catch-up mode: subscribing to all events from start for %s\n", 
                    channel_buf);
            } else {
                flags = EvtSubscribeStartAfterBookmark;
                bookmark_ = EvtCreateBookmark(bookmark_xml_buffer_);
                if (bookmark_ == NULL) {
                    auto error = GetLastError();
                    Logger::warning("EventLogSubscription::subscribe()> Failed to create bookmark for %s (error %d), falling back to all events from start\n",
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
                    Logger::debug("EventLogSubscription::subscribe()> Catch-up mode: Using bookmark for %s\n", channel_buf);
                    subscribe_bookmark = bookmark_;  // Use existing bookmark for subscription
                }
            }
        } else {
            // Future-only mode
            flags = EvtSubscribeToFutureEvents;
            bookmark_ = EvtCreateBookmark(NULL);  // Create new empty bookmark for tracking
            if (bookmark_ == NULL) {
                auto error = GetLastError();
                Logger::recoverable_error("EventLogSubscription::subscribe()> Failed to create empty bookmark (error %d)\n", error);
                return;
            }
            Logger::debug("EventLogSubscription::subscribe()> Future-only mode: subscribing to new events only for %s\n", 
                channel_buf);
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
        EVT_HANDLE hEvent)
    {

        auto subscription = reinterpret_cast<EventLogSubscription*>(pContext);
        if (!subscription) {
            Logger::critical("EventLogSubscription::handleSubscriptionEvent()> Invalid subscription context\n");
            return ERROR_INVALID_PARAMETER;
        }

        try {
            switch (action) {
            case EvtSubscribeActionError:
                if (hEvent && (DWORD_PTR)hEvent != ERROR_EVT_QUERY_RESULT_STALE) {
                    Logger::recoverable_error("EventLogSubscription::handleSubscriptionEvent()> Received error event, error code: %lu\n",
                        (DWORD_PTR)hEvent);
                }
                break;

            case EvtSubscribeActionDeliver:
                if (hEvent && subscription->event_handler_) {
                    // Update the bookmark with the current event
                    if (subscription->bookmark_) {
                        if (!EvtUpdateBookmark(subscription->bookmark_, hEvent)) {
                            Logger::recoverable_error("EventLogSubscription::handleSubscriptionEvent()> Failed to update bookmark, error: %lu\n",
                                GetLastError());
                        } else {
                            subscription->bookmark_modified_ = true;
                        }
                    }

					SlidingWindowMetrics::instance().recordIncoming();

                    // Create EventLogEvent from handle
                    EventLogEvent evt(hEvent);
                    // Handle the event with proper arguments
                    subscription->event_handler_->handleEvent(subscription->subscription_name_.c_str(), evt);
                    subscription->incrementedSaveBookmark();
                }
                break;
            }
            return ERROR_SUCCESS;
        }
        catch (const std::exception& e) {
            Logger::recoverable_error("EventLogSubscription::handleSubscriptionEvent()> Exception: %s\n", e.what());
            return ERROR_INVALID_DATA;
        }
    }

    bool EventLogSubscription::saveBookmark()
    {
        if (!bookmark_modified_) {
            Logger::debug3("EventLogSubscription::saveBookmark()> No changes to save for %ls\n", channel_.c_str());
            return true;
        }

        try {
            if (!bookmark_) {
                Logger::debug3("EventLogSubscription::saveBookmark()> No bookmark to save for %ls\n", channel_.c_str());
                return false;
            }

            // First call EvtRender with NULL buffer to get required size
            DWORD bufferSize = 0;
            DWORD propertyCount = 0;
            if (!EvtRender(NULL, bookmark_, EvtRenderBookmark, 0, NULL, &bufferSize, &propertyCount)) {
                DWORD error = GetLastError();
                if (error != ERROR_INSUFFICIENT_BUFFER) {  // This error is expected when getting buffer size
                    Logger::recoverable_error("EventLogSubscription::saveBookmark()> Failed to get required buffer size, error: %lu\n",
                        error);
                    return false;
                }
            }

            // Typical bookmark XML is small, but let's be safe
            if (bufferSize > MAX_BOOKMARK_SIZE) {
                Logger::recoverable_error("EventLogSubscription::saveBookmark()> Bookmark size %lu exceeds maximum %lu\n",
                    bufferSize, MAX_BOOKMARK_SIZE);
                return false;
            }

            // Get buffer from global pool
            DWORD bufferUsed = 0;

            // Suppress false positive warning - bookmark_xml_buffer_ is a fixed array, not a pointer
            #pragma warning(push)
            #pragma warning(disable: 6387)
            // Now render the bookmark into our buffer
            if (!EvtRender(
                NULL, 
                bookmark_, 
                EvtRenderBookmark, 
                MAX_BOOKMARK_SIZE, 
                bookmark_xml_buffer_, 
                &bufferUsed, 
                &propertyCount)) {
                Logger::recoverable_error("EventLogSubscription::saveBookmark()> Failed to render bookmark, error: %lu\n",
                    GetLastError());
                return false;
            }
            #pragma warning(pop)

            try {
                Registry::writeBookmark(channel_.c_str(), bookmark_xml_buffer_, bufferUsed * sizeof(wchar_t));
                bookmark_modified_ = false;
                events_since_last_save_ = 0;
                last_bookmark_save_ = time(nullptr);
                Logger::debug2("EventLogSubscription::saveBookmark()> Saved bookmark for %ls\n", channel_.c_str());
                return true;
            }
            catch (const Result& r) {
                Logger::recoverable_error("EventLogSubscription::saveBookmark()> Failed to save bookmark: %s\n", r.what());
                return false;
            }
        }
        catch (const std::exception& e) {
            Logger::recoverable_error("EventLogSubscription::saveBookmark()> Exception: %s\n", e.what());
            return false;
        }
    }

} // namespace Syslog_agent