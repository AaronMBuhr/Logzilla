/*
SyslogAgent: a syslog agent for Windows
Copyright � 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include "pugixml.hpp"
#include "EventLogEvent.h"
#include "Globals.h"
#include "Logger.h"
#include "Util.h"

#pragma comment(lib, "wevtapi.lib")

namespace Syslog_agent {
    EventLogEvent::EventLogEvent(EVT_HANDLE windows_event_handle)
        : windows_event_handle_(windows_event_handle), xml_buffer_(nullptr), text_buffer_(nullptr) {
    }

    EventLogEvent::~EventLogEvent() {
        if (xml_buffer_)
            Globals::instance()->releaseMessageBuffer(xml_buffer_);
        if (text_buffer_)
            Globals::instance()->releaseMessageBuffer(text_buffer_);
    }

    void EventLogEvent::renderXml() {
        auto logger = LOG_THIS;
        DWORD buffer_size_needed;
        DWORD count;
        if (xml_buffer_ != nullptr)
            return;
        xml_buffer_ = Globals::instance()->getMessageBuffer("xml_buffer_");

        auto xml_buffer_w = reinterpret_cast<wchar_t*>(Globals::instance()->getMessageBuffer("xml_buffer_w"));
        BOOL succeeded = EvtRender(
            nullptr,
            windows_event_handle_,
            EvtRenderEventXml,
            static_cast<DWORD>(Globals::MESSAGE_BUFFER_SIZE / sizeof(wchar_t)),
            static_cast<PVOID>(xml_buffer_w),
            &buffer_size_needed,
            &count);
        if (!succeeded) {
            auto err = GetLastError();
            logger->recoverable_error("EventLogEvent::RenderXml()> error %d\n", err);
            Globals::instance()->releaseMessageBuffer(reinterpret_cast<char*>(xml_buffer_w));
            return;
        }
        if (buffer_size_needed < (Globals::MESSAGE_BUFFER_SIZE / sizeof(wchar_t))) {
            xml_buffer_w[buffer_size_needed] = 0;
            int utf8_size = WideCharToMultiByte(CP_UTF8, 0, xml_buffer_w, 
                static_cast<int>(buffer_size_needed), nullptr, 0, nullptr, nullptr);
            if (utf8_size >= Globals::MESSAGE_BUFFER_SIZE - 1)
                utf8_size = Globals::MESSAGE_BUFFER_SIZE - 1;
            WideCharToMultiByte(CP_UTF8, 0, xml_buffer_w, static_cast<int>(buffer_size_needed), 
                xml_buffer_, utf8_size, nullptr, nullptr);
            xml_buffer_[utf8_size] = '\0';  // Ensure null termination
        }
        else {
            xml_buffer_[0] = 0;
        }
        Globals::instance()->releaseMessageBuffer(reinterpret_cast<char*>(xml_buffer_w));
    }

    void EventLogEvent::renderEvent() {
        if (isRendered())
            return;
        renderXml();
        pugi::xml_parse_result xml_parsed = xml_doc_.load_buffer(xml_buffer_, strlen(xml_buffer_));
        auto provider_name = xml_doc_.child("Event").child("System").child("Provider").attribute("Name").value();
        renderText(provider_name);
    }

    void EventLogEvent::renderText(const char* publisher_name) {
        auto logger = LOG_THIS;
        if (text_buffer_ != nullptr)
            return;
        text_buffer_ = Globals::instance()->getMessageBuffer("text_buffer_");
        text_buffer_[0] = 0;
        wchar_t* text_buffer_w = reinterpret_cast<wchar_t*>(Globals::instance()->getMessageBuffer("text_buffer_w"));
        wchar_t publisher_name_w[1000];
        MultiByteToWideChar(CP_UTF8, 0, publisher_name, -1, publisher_name_w, 
            sizeof(publisher_name_w) / sizeof(wchar_t));
        EVT_HANDLE metadata_handle = EvtOpenPublisherMetadata(nullptr, publisher_name_w, 
            nullptr, 0, 0);
        if (!metadata_handle) {
            int status = GetLastError();
            logger->recoverable_error("EventPublisher::openMetadata()> EvtOpenPublisherMetadata "
                "failed with %d for %s\n", status, publisher_name);
            Globals::instance()->releaseMessageBuffer(reinterpret_cast<char*>(text_buffer_w));
            return;
        }
        DWORD buffer_size_needed;
        BOOL succeeded = EvtFormatMessage(metadata_handle, windows_event_handle_, 0, 0, nullptr, 
            EvtFormatMessageEvent, Globals::MESSAGE_BUFFER_SIZE / sizeof(wchar_t) - 1, text_buffer_w, 
            &buffer_size_needed);
        if (!succeeded) {
            auto err = GetLastError();
            // Check specifically for message not found
            if (err == 15029) {
                logger->debug("EventLogEvent::renderText()> Message template not found\n");
                strcpy_s(text_buffer_, Globals::MESSAGE_BUFFER_SIZE, "(Message template unavailable)");
            }
            else {
                logger->recoverable_error("EventLogEvent::renderText()> Failed to format message: %d\n", err);
            }
            text_buffer_w[0] = L'\0';
            buffer_size_needed = 0;
        }
        text_buffer_w[buffer_size_needed] = L'\0';
        
        int utf8_size = WideCharToMultiByte(CP_UTF8, 0, text_buffer_w, 
            static_cast<int>(buffer_size_needed), nullptr, 0, nullptr, nullptr);
        if (utf8_size >= Globals::MESSAGE_BUFFER_SIZE - 1)
            utf8_size = Globals::MESSAGE_BUFFER_SIZE - 1;
        WideCharToMultiByte(CP_UTF8, 0, text_buffer_w, static_cast<int>(buffer_size_needed),
            text_buffer_, utf8_size, nullptr, nullptr);
        text_buffer_[utf8_size] = '\0';  // Ensure null termination
        EvtClose(metadata_handle);
        Globals::instance()->releaseMessageBuffer(reinterpret_cast<char*>(text_buffer_w));
    }
}
