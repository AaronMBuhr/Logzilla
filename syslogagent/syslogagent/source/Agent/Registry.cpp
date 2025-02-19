/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"

#include <iostream>
#include <fstream>

#include "Logger.h"
#include "Registry.h"
#include "Result.h"
#include "SyslogAgentSharedConstants.h"
#include "Util.h"

using namespace Syslog_agent;

Registry::Registry() : channels_key_(nullptr) {
    is_open_ = false;
    main_key_ = nullptr;
}


void Registry::close() {
    if (!is_open_)
        return;
    RegCloseKey(main_key_);
    RegCloseKey(channels_key_);
    is_open_ = false;
}


void Registry::open() {
    auto status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
        SharedConstants::RegistryKey::MAIN_KEY, 0, KEY_READ, &main_key_);
    if (status != ERROR_SUCCESS) {
        throw Result(status, "Registry::open()", "RegOpenKeyEx for main key");
    }
    else {
        status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SharedConstants::RegistryKey::CHANNELS_KEY, 
            0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &channels_key_);
        if (status != ERROR_SUCCESS) {
            channels_key_ = nullptr;
        }
    is_open_ = true;
    }
}


void Registry::open(HKEY parent, const wchar_t* name) {
    auto status = RegOpenKeyEx(parent, name, 0, KEY_READ | KEY_WRITE, &main_key_);
    if (status == ERROR_SUCCESS) {
        is_open_ = true;
        return;
    }
    throw Result(status, "Registry::open()", "RegOpenKeyEx");
}


void Registry::open(Registry& parent, const wchar_t* name) {
    open(parent.main_key_, name);
}


Registry::~Registry() {
    close();
}


bool Registry::readBool(const wchar_t* name, bool default_value) const {
    bool value;
    DWORD size = sizeof value;
    auto status = RegQueryValueEx(main_key_, name, nullptr, nullptr, (LPBYTE)&value, &size);
    if (status == ERROR_SUCCESS) return value;
    if (status == ERROR_FILE_NOT_FOUND) return default_value;
    throw Result(status, "Registry::readBool()", "RegQueryValueEx");
}


char Registry::readChar(const wchar_t* name, char default_value) const {
    char value;
    DWORD size = sizeof value;
    auto status = RegQueryValueEx(main_key_, name, nullptr, nullptr, (LPBYTE)&value, &size);
    if (status == ERROR_SUCCESS) return value;
    if (status == ERROR_FILE_NOT_FOUND) return default_value;
    throw Result(status, "Registry::readChar()", "RegQueryValueEx");
}


int Registry::readInt(const wchar_t* name, int default_value) const {
    DWORD value;
    DWORD size = sizeof value;
    auto status = RegQueryValueEx(main_key_, name, nullptr, nullptr, (LPBYTE)&value, &size);
    if (status == ERROR_SUCCESS) return value;
    if (status == ERROR_FILE_NOT_FOUND) return default_value;
    throw Result(status, "Registry::readInt()", "RegQueryValueEx");
}


std::wstring Registry::readString(const wchar_t* name, const wchar_t* default_value) const {
    wchar_t value[1024];
    DWORD size = sizeof value;
    auto status = RegQueryValueEx(main_key_, name, nullptr, nullptr, (LPBYTE)&value, &size);
    if (status == ERROR_SUCCESS) return std::wstring(value);
    if (status == ERROR_FILE_NOT_FOUND) return std::wstring(default_value);
    throw Result(status, "Registry::readString()", "RegQueryValueEx");
}


time_t Registry::readTime(const wchar_t* name, time_t default_value) const {
    time_t value;
    DWORD size = sizeof value;
    auto status = RegQueryValueEx(main_key_, name, nullptr, nullptr, (LPBYTE)&value, &size);
    if (status == ERROR_SUCCESS) return value;
    if (status == ERROR_FILE_NOT_FOUND) return default_value;
    throw Result(status, "Registry::readTime()", "RegQueryValueEx");
}


std::wstring Registry::readSubkey(HKEY registry_key, DWORD index) const {
    wchar_t value[1024];
    DWORD size = sizeof value;
    auto status = RegEnumKeyEx(registry_key, index, (LPWSTR)&value, &size, nullptr, 
        nullptr, nullptr, nullptr);
    if (status == ERROR_SUCCESS) return std::wstring(value);
    if (status == ERROR_NO_MORE_ITEMS) return std::wstring();
    throw Result(status, "Registry::readSubkey()", "RegEnumKeyEx");
}


void Registry::writeUint(const wchar_t* name, DWORD value) const {
    DWORD size = sizeof value;
    auto status = RegSetValueEx(main_key_, name, 0, REG_DWORD, (LPBYTE)&value, size);
    if (status == ERROR_SUCCESS) return;
    throw Result(status, "Registry::writeUint()", "RegSetValueEx");
}


void Registry::writeTime(const wchar_t* name, time_t value) const {
    DWORD size = sizeof value;
    auto status = RegSetValueEx(main_key_, name, 0, REG_QWORD, (LPBYTE)&value, size);
    if (status == ERROR_SUCCESS) return;
    throw Result(status, "Registry::writeTime()", "RegSetValueEx");
}


std::vector<std::wstring> Registry::readChannels() const {
    std::vector<std::wstring> channels;
    if (channels_key_ == nullptr) {
        return channels;
    }
    wchar_t channel_name[1024];
    DWORD status = ERROR_SUCCESS;
    for (DWORD i = 0; true; i++) {
        DWORD channel_name_size = sizeof channel_name;
        status = RegEnumKeyEx(channels_key_, i, (LPWSTR)&channel_name, &channel_name_size, 
            nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != 0) {
            throw Result(status, "Registry::readSubkey()", "RegEnumKeyEx");
        }
        HKEY channel_key = NULL;
        wchar_t full_channel_path[4096];
        swprintf_s(full_channel_path, 4096, L"%s\\%s", 
            SharedConstants::RegistryKey::CHANNELS_KEY, channel_name);
        auto status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, full_channel_path, 0, 
            KEY_READ, &channel_key);
        if (status != ERROR_SUCCESS) {
            throw Result(status, "Registry::readChannels()", "could not open channel");
        }
        DWORD value;
        DWORD value_size = sizeof value;
        status = RegQueryValueEx(channel_key, SharedConstants::RegistryKey::CHANNEL_ENABLED, 
            nullptr, nullptr, (LPBYTE)&value, &value_size);
        RegCloseKey(channel_key);
        if (status != ERROR_SUCCESS) {
            throw Result(status, "Registry::readChannels()", "could not read channel");
        }
        if (value == 1) {
            channels.push_back(channel_name);
        }
    }
    return channels;
}


std::wstring Registry::readBookmark(const wchar_t* channel) {
    auto logger = LOG_THIS;
    HKEY channel_key;
    DWORD status = ERROR_SUCCESS;
    wchar_t tempbuf[4096];
    swprintf_s(tempbuf, 4096, L"%s\\%s",
        SharedConstants::RegistryKey::CHANNELS_KEY, channel);
    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, tempbuf, 0,
        KEY_READ, &channel_key);
    if (status != ERROR_SUCCESS) {
        DWORD error = GetLastError();
        Util::toPrintableAscii(reinterpret_cast<char*>(tempbuf), sizeof(tempbuf), channel, ' ');
        logger->recoverable_error("Registry::readBookmark()> error %d,"
            " could not open channel %s\n", error, reinterpret_cast<char*>(tempbuf));
        return std::wstring();
    }

    wchar_t bookmark[4096];
    DWORD bookmark_size = sizeof bookmark;
    status = RegQueryValueEx(channel_key, SharedConstants::RegistryKey::CHANNEL_BOOKMARK,
        nullptr, nullptr, (LPBYTE)bookmark, &bookmark_size);
    RegCloseKey(channel_key);
    if (status != ERROR_SUCCESS) {
        if (status == ERROR_FILE_NOT_FOUND) {
            Util::toPrintableAscii(reinterpret_cast<char*>(tempbuf), sizeof(tempbuf), channel, ' ');
            logger->debug("Registry::readBookmark()> no bookmark found for channel %s\n",
                reinterpret_cast<char*>(tempbuf));
            return std::wstring();
        }
        DWORD error = GetLastError();
        Util::toPrintableAscii(reinterpret_cast<char*>(tempbuf), sizeof(tempbuf), channel, ' ');
        logger->recoverable_error("Registry::readBookmark()> error %d,"
            " could not read bookmark for channel %s\n", error, reinterpret_cast<char*>(tempbuf));
        return std::wstring();
    }
    return std::wstring(bookmark);
}


void Registry::writeBookmark(const wchar_t* channel, const wchar_t* bookmark_buffer, DWORD buffer_size) {
    auto logger = LOG_THIS;
    HKEY channel_key;
    DWORD status = ERROR_SUCCESS;
    wchar_t tempbuf[4096];
    swprintf_s(tempbuf, 4096, L"%s\\%s",
        SharedConstants::RegistryKey::CHANNELS_KEY, channel);
    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, tempbuf, 0,
        KEY_WRITE, &channel_key);
    if (status != ERROR_SUCCESS) {
        DWORD error = GetLastError();
        Util::toPrintableAscii(reinterpret_cast<char*>(tempbuf), sizeof(tempbuf), channel, ' ');
        logger->recoverable_error("Registry::writeBookmark()> error %d,"
            " could not open channel %s\n", error, reinterpret_cast<char*>(tempbuf));
        return;
    }

    status = RegSetValueEx(channel_key, SharedConstants::RegistryKey::CHANNEL_BOOKMARK,
        0, REG_SZ, (LPBYTE)bookmark_buffer, buffer_size);
    RegCloseKey(channel_key);
    if (status != ERROR_SUCCESS) {
        DWORD error = GetLastError();
        Util::toPrintableAscii(reinterpret_cast<char*>(tempbuf), sizeof(tempbuf), channel, ' ');
        logger->recoverable_error("Registry::writeBookmark()> error %d,"
            " could not write bookmark for channel %s\n", error, reinterpret_cast<char*>(tempbuf));
    }
}


void Registry::loadSetupFile() {
    auto logger = LOG_THIS;
    HKEY key;
    DWORD status = ERROR_SUCCESS;
    wchar_t tempbuf[4096];
    swprintf_s(tempbuf, 4096, L"%s",
        SharedConstants::RegistryKey::MAIN_KEY);

    // Create or open the main key
    DWORD disposition;
    status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, tempbuf, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, &disposition);
    if (status != ERROR_SUCCESS) {
        logger->recoverable_error("Registry::loadSetupFile()> error %d,"
            " could not create/open main key\n", GetLastError());
        return;
    }

    // Read the setup file
    std::wifstream setup_file(L"setup.txt");
    if (!setup_file.is_open()) {
        logger->debug("Registry::loadSetupFile()> setup.txt not found\n");
        RegCloseKey(key);
        return;
    }

    std::wstring line;
    while (std::getline(setup_file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == L'#' || line[0] == L';') {
            continue;
        }

        // Split line into key and value
        size_t pos = line.find(L'=');
        if (pos == std::wstring::npos) {
            logger->warning("Registry::loadSetupFile()> invalid line format: %ls\n", line.c_str());
            continue;
        }

        std::wstring reg_key = line.substr(0, pos);
        std::wstring reg_value = line.substr(pos + 1);

        // Trim whitespace
        reg_key.erase(0, reg_key.find_first_not_of(L" \t"));
        reg_key.erase(reg_key.find_last_not_of(L" \t") + 1);
        reg_value.erase(0, reg_value.find_first_not_of(L" \t"));
        reg_value.erase(reg_value.find_last_not_of(L" \t") + 1);

        // Write to registry
        status = RegSetValueExW(key, reg_key.c_str(), 0, REG_SZ,
            reinterpret_cast<const BYTE*>(reg_value.c_str()),
            static_cast<DWORD>((reg_value.length() + 1) * sizeof(wchar_t)));
        if (status != ERROR_SUCCESS) {
            logger->recoverable_error("Registry::loadSetupFile()> error %d,"
                " could not write key %ls\n", GetLastError(), reg_key.c_str());
        }
    }

    setup_file.close();
    RegCloseKey(key);
    logger->info("Registry::loadSetupFile()> setup.txt loaded successfully\n");
}
