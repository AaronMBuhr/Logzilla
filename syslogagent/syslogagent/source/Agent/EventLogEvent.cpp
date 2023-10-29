#include "stdafx.h"
#include "pugixml.hpp"
#include "EventLogEvent.h"
#include "Globals.h"
#include "Logger.h"
#include "Util.h"

#pragma comment(lib, "wevtapi.lib")

namespace Syslog_agent {
	EventLogEvent::EventLogEvent(EVT_HANDLE windows_event_handle)
		: windows_event_handle_(windows_event_handle),
		xml_buffer_(nullptr),
		text_buffer_(nullptr) {
	}
	
	EventLogEvent::~EventLogEvent() {
		if (xml_buffer_)
			Globals::instance()->releaseMessageBuffer("xml_buffer_", xml_buffer_);
		if (text_buffer_)
			Globals::instance()->releaseMessageBuffer("text_buffer_", text_buffer_);
	}

	// TODO change C casts to C++ casts (everywhere)
	void EventLogEvent::renderXml() {
		DWORD buffer_size_needed;
		DWORD count;
		if (xml_buffer_ != NULL)
			return;
		xml_buffer_ = Globals::instance()->getMessageBuffer("xml_buffer_");

		auto xml_buffer_w = (wchar_t*) Globals::instance()->getMessageBuffer("xml_buffer_w");
		auto succeeded = EvtRender(
			NULL,
			windows_event_handle_,
			EvtRenderEventXml,
			(DWORD) Globals::MESSAGE_BUFFER_SIZE / sizeof(wchar_t),
			(PVOID) xml_buffer_w,
			&buffer_size_needed,
			&count);
		if (!succeeded) {
			auto err = GetLastError();
			Logger::recoverable_error("EventLogEvent::RenderXml()> error %d\n");
			Globals::instance()->releaseMessageBuffer("xml_buffer_w", (char*)xml_buffer_w);
			return;
		}
		if (buffer_size_needed < (Globals::MESSAGE_BUFFER_SIZE / sizeof(wchar_t))) {
			xml_buffer_w[buffer_size_needed] = 0;
			wchar_t* test = (wchar_t*)xml_buffer_;
			int utf8_size = WideCharToMultiByte(CP_UTF8, 0, xml_buffer_w, buffer_size_needed, NULL, 0, NULL, NULL) + 1;
			if (utf8_size >= Globals::MESSAGE_BUFFER_SIZE)
				utf8_size = Globals::MESSAGE_BUFFER_SIZE - 1;
			WideCharToMultiByte(CP_UTF8, 0, xml_buffer_w, buffer_size_needed, xml_buffer_, utf8_size, NULL, NULL);
		}
		else {
			xml_buffer_[0] = 0;
		}
		Globals::instance()->releaseMessageBuffer("xml_buffer_w", (char*)xml_buffer_w);
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

		//  TODO fix error handling
		if (text_buffer_ != NULL)
			return;
		text_buffer_ = Globals::instance()->getMessageBuffer("text_buffer_");
		text_buffer_[0] = 0;
		wchar_t* text_buffer_w = (wchar_t*) Globals::instance()->getMessageBuffer("text_buffer_w");
		wchar_t publisher_name_w[1000];
		MultiByteToWideChar(CP_UTF8, 0, publisher_name, -1, publisher_name_w, sizeof(publisher_name_w) / sizeof(wchar_t));
		EVT_HANDLE metadata_handle = EvtOpenPublisherMetadata(NULL,
			publisher_name_w,
			NULL,
			0, //MAKELCID(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), 0), 
			0);

		int status = GetLastError();
		if (status != ERROR_SUCCESS) {
			Logger::recoverable_error("EventPublisher::openMetadata()> EvtOpenPublisherMetadata failed with %d for %s\n", status, publisher_name);
			Globals::instance()->releaseMessageBuffer("text_buffer_w", (char*)text_buffer_w);
			return;
		}

		DWORD buffer_size_needed;
		auto test = windows_event_handle_;
		auto succeeded = EvtFormatMessage(metadata_handle,
			windows_event_handle_,
			0,
			0,
			NULL,
			EvtFormatMessageEvent,
			Globals::MESSAGE_BUFFER_SIZE / sizeof(wchar_t) - 1,
			(LPWSTR) text_buffer_w,
			&buffer_size_needed
		);
		text_buffer_w[buffer_size_needed] = 0;
		if (buffer_size_needed == 0) {
			swprintf_s(text_buffer_w, Globals::MESSAGE_BUFFER_SIZE / sizeof(wchar_t) - 1,
				L"The description for the event from source %s cannot be found. "
				"Either the component that raises this event is not installed on your "
				"local computer or the installation is corrupted.",
				publisher_name_w);
			buffer_size_needed = wcslen(text_buffer_w);
		}
		if (!succeeded) {
			auto err = GetLastError();
			Logger::recoverable_error("EventLogEvent::renderText()> error %d\n", err);
		}
		int utf8_size = WideCharToMultiByte(CP_UTF8, 0, text_buffer_w, buffer_size_needed, NULL, 0, NULL, NULL) + 1;
		if (utf8_size >= Globals::MESSAGE_BUFFER_SIZE)
			utf8_size = Globals::MESSAGE_BUFFER_SIZE - 1;
		int len = WideCharToMultiByte(CP_UTF8, 0, text_buffer_w, buffer_size_needed, text_buffer_, utf8_size, NULL, NULL);

		Globals::instance()->releaseMessageBuffer("text_buffer_w", (char*)text_buffer_w);
		text_buffer_[len] = 0;
		EvtClose(metadata_handle);
	}
}
