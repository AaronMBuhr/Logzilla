#include "stdafx.h"
#include "EventLogSubscription.h"
#include "Logger.h"
#include "Registry.h"
#include "SlidingWindowMetrics.h"
#include "Util.h"

#pragma comment(lib, "wevtapi.lib")

namespace Syslog_agent {

    // C-style function for handling SEH exceptions
    // This has no C++ objects so it can safely use __try/__except
    static DWORD HandleSubscriptionSEH(
        EVT_SUBSCRIBE_NOTIFY_ACTION action,
        PVOID pContext,
        EVT_HANDLE hEvent)
    {
        auto logger = LOG_THIS;

        __try {
            // Call into the C++ implementation
            auto subscription = reinterpret_cast<EventLogSubscription*>(pContext);
            if (!subscription) {
                return ERROR_INVALID_PARAMETER;
            }

            // Handle updates to bookmark - this might throw SEH
            if (action == EvtSubscribeActionDeliver && hEvent) {
                EVT_HANDLE bookmark = subscription->getBookmark();
                if (bookmark) {
                    if (!subscription->updateBookmark(hEvent)) {
                        DWORD lastError = GetLastError();
                        logger->recoverable_error("HandleSubscriptionSEH()> Failed to update bookmark, error: %lu\n",
                            lastError);
                        return lastError;
                    }
                    return ERROR_SUCCESS;
                }
            }

            return ERROR_SUCCESS; // Successfully handled SEH-prone operations
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Return the Windows exception code
            DWORD exceptionCode = GetExceptionCode();
            logger->critical("HandleSubscriptionSEH()> Structured exception: 0x%08X at %s:%d\n",
                exceptionCode, __FILE__, __LINE__);
            return exceptionCode;
        }
    }

    // Move constructor
    EventLogSubscription::EventLogSubscription(EventLogSubscription&& source) noexcept
        : subscription_name_(std::move(source.subscription_name_)),
        channel_(std::move(source.channel_)),
        query_(std::move(source.query_)),
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

        // Clear source pointers to prevent double deletion
        source.bookmark_ = NULL;
        source.subscription_handle_ = NULL;
        source.bookmark_xml_buffer_[0] = 0;
        // No need to reset event_handler_ as it's been moved

        if (source.subscription_active_) {
            source.subscription_active_ = false;
            subscribe(bookmark_xml_buffer_, source.only_while_running_);
        }
    }

    // Move assignment operator
    EventLogSubscription& EventLogSubscription::operator=(EventLogSubscription&& source) noexcept {
        if (this != &source) {
            // Clean up existing resources
            if (subscription_active_) {
                cancelSubscription();
            }
            
            if (bookmark_ != NULL) {
                EvtClose(bookmark_);
                bookmark_ = NULL;
            }
            
            // Move resource ownership
            subscription_name_ = std::move(source.subscription_name_);
            channel_ = std::move(source.channel_);
            query_ = std::move(source.query_);
            event_handler_ = std::move(source.event_handler_);
            bookmark_ = source.bookmark_;
            only_while_running_ = source.only_while_running_;
            subscription_handle_ = source.subscription_handle_;
            subscription_active_ = false; // We'll resubscribe if needed
            bookmark_modified_ = source.bookmark_modified_;
            last_bookmark_save_ = source.last_bookmark_save_;
            events_since_last_save_ = 0;
            
            memcpy(bookmark_xml_buffer_, source.bookmark_xml_buffer_, sizeof(bookmark_xml_buffer_));
            
            // Clear source handles to prevent double deletion
            source.bookmark_ = NULL;
            source.subscription_handle_ = NULL;
            source.bookmark_xml_buffer_[0] = 0;
            // event_handler_ is already moved, no need to clear
            
            // Resubscribe if the source was active
            if (source.subscription_active_) {
                source.subscription_active_ = false;
                subscribe(bookmark_xml_buffer_, source.only_while_running_);
            }
        }
        
        return *this;
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
        auto logger = LOG_THIS;
        char channel_buf[1024];
        Util::wstr2str(channel_buf, sizeof(channel_buf), channel_.c_str());
        logger->debug("EventLogSubscription::subscribe()> Subscribing to %s\n", channel_buf);

        if (subscription_active_) {
            logger->recoverable_error("EventLogSubscription::subscribe()> subscription already active\n");
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

        if (!only_while_running) {
            if (bookmark_xml_buffer_[0] == 0) {
                flags = EvtSubscribeStartAtOldestRecord;
                bookmark_ = EvtCreateBookmark(NULL);  // Create new empty bookmark for tracking
                if (bookmark_ == NULL) {
                    auto error = GetLastError();
                    logger->recoverable_error("EventLogSubscription::subscribe()> Failed to create empty bookmark (error %d)\n", error);
                    return;
                }
                logger->debug2("EventLogSubscription::subscribe()> Created new empty bookmark %p for %s\n", 
                    bookmark_, channel_buf);
                logger->debug("EventLogSubscription::subscribe()> Catch-up mode: subscribing to all events from start for %s\n", 
                    channel_buf);
            } else {
                flags = EvtSubscribeStartAfterBookmark;
                bookmark_ = EvtCreateBookmark(bookmark_xml_buffer_);
                if (bookmark_ == NULL) {
                    auto error = GetLastError();
                    logger->warning("EventLogSubscription::subscribe()> Failed to create bookmark for %s (error %d), falling back to all events from start\n",
                        channel_buf, error);
                    flags = EvtSubscribeStartAtOldestRecord;
                    bookmark_ = EvtCreateBookmark(NULL);  // Create new empty bookmark for tracking
                    if (bookmark_ == NULL) {
                        auto error = GetLastError();
                        logger->recoverable_error("EventLogSubscription::subscribe()> Failed to create empty bookmark (error %d)\n", error);
                        return;
                    }
                    logger->debug2("EventLogSubscription::subscribe()> Created new empty bookmark %p for %s after bookmark load failed\n", 
                        bookmark_, channel_buf);
                } else {
                    logger->debug2("EventLogSubscription::subscribe()> Created bookmark %p from XML for %s\n", 
                        bookmark_, channel_buf);
                    logger->debug("EventLogSubscription::subscribe()> Catch-up mode: Using bookmark for %s\n", channel_buf);
                    subscribe_bookmark = bookmark_;  // Use existing bookmark for subscription
                }
            }
        } else {
            // Future-only mode
            flags = EvtSubscribeToFutureEvents;
            bookmark_ = EvtCreateBookmark(NULL);  // Create new empty bookmark for tracking
            if (bookmark_ == NULL) {
                auto error = GetLastError();
                logger->recoverable_error("EventLogSubscription::subscribe()> Failed to create empty bookmark (error %d)\n", error);
                return;
            }
            logger->debug("EventLogSubscription::subscribe()> Future-only mode: subscribing to new events only for %s\n", 
                channel_buf);
        }

        logger->debug2("EventLogSubscription::subscribe()> Attempting subscription to %s with flags %d and bookmark %p (tracking bookmark %p)\n", 
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
            logger->critical("EventLogSubscription::subscribe()> could not subscribe to %s (error %d)\n",
                channel_buf, status);
            return;
        }

        subscription_active_ = true;
        logger->debug2("EventLogSubscription::subscribe()> Successfully subscribed to %s\n", channel_buf);
    }

    void EventLogSubscription::cancelSubscription() {
        auto logger = LOG_THIS;
        if (subscription_active_) {
            // saveBookmark();
            if (subscription_handle_) {
                if (!EvtClose(subscription_handle_)) {
                    logger->recoverable_error("EventLogSubscription::cancelSubscription()> "
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
        auto logger = LOG_THIS;
        
        try {
            // First, handle the SEH-prone parts in a separate C function
            DWORD sehResult = HandleSubscriptionSEH(action, pContext, hEvent);
            if (sehResult != ERROR_SUCCESS) {
                logger->critical("EventLogSubscription::handleSubscriptionEvent()> Structured exception: 0x%08X at %s:%d\n",
                    sehResult, __FILE__, __LINE__);
                return ERROR_SUCCESS;
            }
            
            // If we get here, we've handled the SEH-prone parts successfully
            EventLogSubscription* subscription = static_cast<EventLogSubscription*>(pContext);
            
            switch (action) {
            case EvtSubscribeActionError:
                if (hEvent && (DWORD_PTR)hEvent != ERROR_EVT_QUERY_RESULT_STALE) {
                    logger->recoverable_error("EventLogSubscription::handleSubscriptionEvent()> Received error event, error code: %lu\n",
                        (DWORD_PTR)hEvent);
                }
                break;

            case EvtSubscribeActionDeliver:
                if (hEvent && subscription) {
#if ONLY_FOR_DEBUGGING_CURRENTLY_DISABLED
                    SlidingWindowMetrics::instance().recordIncoming();
#endif

                    // Create EventLogEvent on the stack within this scope
                    EventLogEvent evt(hEvent);
                    try {
                        subscription->event_handler_->handleEvent(subscription->subscription_name_.c_str(), evt);
                    } catch (const std::exception& e) {
                        logger->critical("EventLogSubscription::handleEvent exception: %s\n", e.what());
                    } catch (...) {
                        logger->critical("EventLogSubscription::handleEvent unknown exception\n");
                    }

                    // Update the subscription bookmark and save it if needed
                    if (subscription->updateBookmark(hEvent)) {
                        subscription->incrementedSaveBookmark();
                    }
                }
                break;
            }

            return ERROR_SUCCESS;
        } catch (const std::exception& e) {
            logger->critical("EventLogSubscription::handleSubscriptionEvent()> Exception: %s\n", e.what());
            return ERROR_SUCCESS;
        } catch (...) {
            logger->critical("EventLogSubscription::handleSubscriptionEvent()> Unknown exception\n");
            return ERROR_SUCCESS;
        }
    }

    bool EventLogSubscription::saveBookmark()
    {
        auto logger = LOG_THIS;
        if (!bookmark_modified_) {
            logger->debug3("EventLogSubscription::saveBookmark()> No changes to save for %ls\n", channel_.c_str());
            return true;
        }

        try {
            if (!bookmark_) {
                logger->debug3("EventLogSubscription::saveBookmark()> No bookmark to save for %ls\n", channel_.c_str());
                return false;
            }

            // First call EvtRender with NULL buffer to get required size
            DWORD bufferSize = 0;
            DWORD propertyCount = 0;
            if (!EvtRender(NULL, bookmark_, EvtRenderBookmark, 0, NULL, &bufferSize, &propertyCount)) {
                DWORD error = GetLastError();
                if (error != ERROR_INSUFFICIENT_BUFFER) {  // This error is expected when getting buffer size
                    logger->recoverable_error("EventLogSubscription::saveBookmark()> Failed to get required buffer size, error: %lu\n",
                        error);
                    return false;
                }
            }

            // Typical bookmark XML is small, but let's be safe
            if (bufferSize > MAX_BOOKMARK_SIZE) {
                logger->recoverable_error("EventLogSubscription::saveBookmark()> Bookmark size %lu exceeds maximum %lu\n",
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
                logger->recoverable_error("EventLogSubscription::saveBookmark()> Failed to render bookmark, error: %lu\n",
                    GetLastError());
                return false;
            }
            #pragma warning(pop)

            try {
                Registry::writeBookmark(channel_.c_str(), bookmark_xml_buffer_, bufferUsed * sizeof(wchar_t));
                bookmark_modified_ = false;
                events_since_last_save_ = 0;
                last_bookmark_save_ = time(nullptr);
                logger->debug2("EventLogSubscription::saveBookmark()> Saved bookmark for %ls\n", channel_.c_str());
                return true;
            }
            catch (const Result& r) {
                logger->recoverable_error("EventLogSubscription::saveBookmark()> Failed to save bookmark: %s\n", r.what());
                return false;
            }
        }
        catch (const std::exception& e) {
            logger->recoverable_error("EventLogSubscription::saveBookmark()> Exception: %s\n", e.what());
            return false;
        }
    }

    bool EventLogSubscription::updateBookmark(EVT_HANDLE hEvent) {
        if (!bookmark_ || !hEvent) {
            return false;
        }

        BOOL updateResult = EvtUpdateBookmark(bookmark_, hEvent);
        if (!updateResult) {
            return false;
        }

        bookmark_modified_ = true;
        return true;
    }

} // namespace Syslog_agent