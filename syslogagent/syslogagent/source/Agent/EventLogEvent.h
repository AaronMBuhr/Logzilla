#pragma once

#include <memory>
#include <Windows.h>
#include <winevt.h>
#include "BitmappedUsageCollection.h"
#include "pugixml.hpp"

using namespace std;

namespace Syslog_agent {
	class EventLogEvent {
	public:
		EventLogEvent(EVT_HANDLE windows_event_handle);
		~EventLogEvent();
		void renderEvent();
		pugi::xml_document& getXmlDoc() { return xml_doc_; }
		bool isRendered() { return xml_buffer_ != nullptr; }
		char* getEventXml() { return xml_buffer_; }
		char* getEventText() { return text_buffer_; }

	private:
		void renderXml();
		void renderText(const char* publisher_name);

		// these two stored as utf-8
		char* xml_buffer_;
		char* text_buffer_;
		EVT_HANDLE windows_event_handle_;
		pugi::xml_document xml_doc_;
	};
}