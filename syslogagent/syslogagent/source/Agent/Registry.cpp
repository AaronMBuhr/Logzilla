/*
SyslogAgent: a syslog agent for Windows
Copyright © 2021 Logzilla Corp.
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

Registry::Registry() {
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
    auto status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SYSLOGAGENT_REGISTRYKEY_MAIN, 0, KEY_READ, &main_key_);
    if (status != ERROR_SUCCESS) {
        throw Result(status, "Registry::open()", "RegOpenKeyEx for main key");
    }
    else {
        status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SYSLOGAGENT_REGISTRYKEY_CHANNELS, 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &channels_key_);
        if (status != ERROR_SUCCESS) {
            channels_key_ = nullptr;
//            throw Result(status, "Registry::open()", "RegOpenKeyEx for channels key");
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

std::wstring Registry::readString(const wchar_t* name, wchar_t* default_value) const {
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
    auto status = RegEnumKeyEx(registry_key, index, (LPWSTR)&value, &size, nullptr, nullptr, nullptr, nullptr);
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
        status = RegEnumKeyEx(channels_key_, i, (LPWSTR)&channel_name, &channel_name_size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status != 0) {
            throw Result(status, "Registry::readSubkey()", "RegEnumKeyEx");
        }
        HKEY channel_key = NULL;
        wchar_t full_channel_path[4096];
        swprintf_s(full_channel_path, 4096, L"%s\\%s", SYSLOGAGENT_REGISTRYKEY_CHANNELS, channel_name);
        auto status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, full_channel_path, 0, KEY_READ, &channel_key);
        if (status != ERROR_SUCCESS) {
            throw Result(status, "Registry::readChannels()", "could not open channel");
        }
        DWORD value;
        DWORD value_size = sizeof value;
        status = RegQueryValueEx(channel_key, SYSLOGAGENT_REGISTRYKEY_CHANNEL_ENABLED, nullptr, nullptr, (LPBYTE)&value, &value_size);
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
    HKEY channel_key;
    DWORD status = ERROR_SUCCESS;
    wchar_t full_channel_path[4096];
    swprintf_s(full_channel_path, 4096, L"%s\\%s", SYSLOGAGENT_REGISTRYKEY_CHANNELS, channel);
    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, full_channel_path, 0, KEY_READ, &channel_key);
    if (status != ERROR_SUCCESS) {
        Logger::recoverable_error("Registry::readBookmark()", "could not open channel");
        return wstring();
    }
    wchar_t xml[32000];
    DWORD xml_size = sizeof xml;
    status = RegQueryValueEx(channel_key, SYSLOGAGENT_REGISTRYKEY_CHANNEL_BOOKMARK, nullptr, nullptr, (LPBYTE)&xml, &xml_size);
    if (status != ERROR_SUCCESS) {
        char warnbuf[1024];
        Util::toPrintableAscii(warnbuf, sizeof(warnbuf), channel, ' ');
        Logger::warn("Registry::readBookmark()", "could not read bookmark for %s\n", warnbuf);
        xml[0] = 0;
        // fall through to close channel
    }
    RegCloseKey(channel_key);
    return wstring(xml);
}

void Registry::writeBookmark(const wchar_t* channel, const wchar_t* bookmark) {
    HKEY channel_key;
    DWORD status = ERROR_SUCCESS;
    wchar_t full_channel_path[4096];
    swprintf_s(full_channel_path, 4096, L"%s\\%s", SYSLOGAGENT_REGISTRYKEY_CHANNELS, channel);
    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, full_channel_path, 0, KEY_READ | KEY_SET_VALUE, &channel_key);
    if (status != ERROR_SUCCESS) {
        throw Result(status, "Registry::writeBookmark()", "could not open channel");
    }
    status = RegSetValueEx(channel_key, SYSLOGAGENT_REGISTRYKEY_CHANNEL_BOOKMARK, 0, REG_SZ, (LPBYTE)bookmark, wcslen(bookmark) * sizeof(wchar_t));
    if (status != ERROR_SUCCESS) {
        throw Result(status, "Registry::writeBookmark()", "could not write bookmark");
    }
    RegCloseKey(channel_key);
}

void Registry::loadSetupFile() {
    HKEY main_key;
    LSTATUS status;
    DWORD size;

    status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SYSLOGAGENT_REGISTRYKEY_MAIN, 0, KEY_READ | KEY_WRITE, &main_key);
    if (status != ERROR_SUCCESS) {
        // can't open registry key, just return
        return;
    }

    wchar_t setup_filename[1024];
    size = sizeof setup_filename;
    status = RegQueryValueEx(main_key, SYSLOGAGENT_REGISTRYKEY_INITIAL_SETUP_FILE, nullptr, nullptr, (LPBYTE)&setup_filename, &size);
    RegCloseKey(main_key);

#if TRY1
    ifstream file(setup_filename);
    if (!file) {
        // can't open designated setup file, just return
        return;
    }

    // Read the file into a string
    string contents((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());

    // Close the file
    file.close();

    // Create a key and set the value to the contents of the .reg file

  // Open the registry key where the values will be imported.
    HKEY key;
    LONG result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SYSLOGAGENT_REGISTRYKEY_MAIN, 0, KEY_SET_VALUE, &key);
    if (result != ERROR_SUCCESS) {
        // Error: Could not open registry key
        return;
    }

    // Import the values from the .reg file into the registry key.
    result = RegSetValueEx(key, NULL, 0, REG_MULTI_SZ, (const BYTE*)contents.c_str(), contents.size());
    if (result != ERROR_SUCCESS) {
        // Error: Could not import values into registry key
        return;
    }

    // Close the registry key.
    RegCloseKey(key);

#elseif TRY2
    // Import the key and value into the registry
    result = RegRestoreKey(HKEY_CURRENT_USER, "Software\\TempKey", REG_WHOLE_HIVE_VOLATILE);
    if (result != ERROR_SUCCESS) {
        cout << "Error: Unable to restore key." << endl;
        return 1;
    }
#else

    // Acquiring required privileges	
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
    {
        PTOKEN_PRIVILEGES ns = (PTOKEN_PRIVILEGES)new BYTE[sizeof(DWORD) + sizeof(LUID_AND_ATTRIBUTES) + 2];
        if (LookupPrivilegeValue(NULL, SE_BACKUP_NAME, &(ns->Privileges[0].Luid)))
        {
            ns->PrivilegeCount = 1;
            ns->Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (!AdjustTokenPrivileges(hToken, FALSE, ns, 0, NULL, NULL))
            {
                //ShowError(NULL,IDS_ERRORCANNOTENABLEPRIVILEGE,GetLastError());
            }
        }
        if (LookupPrivilegeValue(NULL, SE_RESTORE_NAME, &(ns->Privileges[0].Luid)))
        {
            ns->PrivilegeCount = 1;
            ns->Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            if (!AdjustTokenPrivileges(hToken, FALSE, ns, 0, NULL, NULL))
            {
                //ShowError(NULL,IDS_ERRORCANNOTENABLEPRIVILEGE,GetLastError());
            }

        }
        delete[](BYTE*)ns;
        CloseHandle(hToken);
    }

    status = RegRestoreKey(HKEY_LOCAL_MACHINE, setup_filename, 0);

#endif

    // load cert files
    wchar_t cert_filename[1024];
    size = sizeof setup_filename;
    status = RegQueryValueEx(main_key, SYSLOGAGENT_REGISTRYKEY_SETUP_PRIMARY_TLS_FILENAME, nullptr, nullptr, (LPBYTE)&cert_filename, &size);
    if (status == ERROR_SUCCESS) {
        wstring primary_cert_path = Util::getThisPath(true) + SYSLOGAGENT_CERT_FILENAME_PRIMARY;
        Util::copyFile(cert_filename, primary_cert_path.c_str()); // if this fails, just ignore
    }

    status = RegQueryValueEx(main_key, SYSLOGAGENT_REGISTRYKEY_SETUP_SECONDARY_TLS_FILENAME, nullptr, nullptr, (LPBYTE)&cert_filename, &size);
    if (status == ERROR_SUCCESS) {
        wstring secondary_cert_path = Util::getThisPath(true) + SYSLOGAGENT_CERT_FILENAME_SECONDARY;
        Util::copyFile(cert_filename, secondary_cert_path.c_str()); // if this fails, just ignore
    }

    RegCloseKey(main_key);

#if DISABLED
    // delete setup regkey
    swprintf(keyname_buf, (size_t)sizeof keyname_buf, L"%s\\%s", SYSLOGAGENT_REGISTRYKEY_MAIN, SYSLOGAGENT_REGISTRYKEY_INITIAL_SETUP_FILE);
    status = RegDeleteKey(HKEY_LOCAL_MACHINE, keyname_buf);
#endif
}
