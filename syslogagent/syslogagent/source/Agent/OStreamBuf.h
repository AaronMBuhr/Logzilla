/*
SyslogAgent: a syslog agent for Windows
Copyright � 2021 Logzilla Corp.
*/

#pragma once
#include <streambuf>

template <typename char_type>
struct OStreamBuf : public std::basic_streambuf<char_type, std::char_traits<char_type>> {
public:
    OStreamBuf(char_type* buffer, std::streamsize bufferLength) {
        // set the "put" pointer to the start of the buffer and record its length.
        this->setp(buffer, buffer + bufferLength);
    }

    // Public helper method to get the current number of characters written.
    std::streamsize current_length() const {
        return this->pptr() - this->pbase();
    }
};
