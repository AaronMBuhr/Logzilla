/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/


#include "stdafx.h"
#include "LogConfiguration.h"

using namespace Syslog_agent;

void LogConfiguration::loadFromRegistry(Registry& parent) {
    bookmark_ = Registry::readBookmark(channel_.c_str());
}

void LogConfiguration::saveToRegistry(Registry& parent) const {
    return;
    const wchar_t* bookmark_str = bookmark_.c_str();
    Registry::writeBookmark(channel_.c_str(), bookmark_str, static_cast<DWORD>(wcslen(bookmark_str) * sizeof(wchar_t)));
}
