/*
SyslogAgent: a syslog agent for Windows
Copyright © 2021 Logzilla Corp.
*/

#pragma once
#include <memory>
#include <mutex>
#include <vector>
#include "BitmappedObjectPool.h"

/*
I realize globals / singletons are denigrated but given that this app
is supposed to run for long periods of time, memory churn and heap
fragmentation are of concern. Consequently this class is used to 
store objects that should persist and not be continually allocated/
deallocated.
*/

namespace Syslog_agent {
    class Globals {
    public:
        static constexpr unsigned int MESSAGE_BUFFER_SIZE = 132000;
        static constexpr unsigned int BUFFER_CHUNK_SIZE = 12;
        static constexpr unsigned int PERCENT_SLACK = -1;

        unsigned int batched_count_ = 0;
        unsigned int peek_count_ = 0;
        unsigned int queued_count_ = 0;

        // Deleted copy/move operations to ensure singleton semantics
        Globals(const Globals&) = delete;
        Globals& operator=(const Globals&) = delete;
        Globals(Globals&&) = delete;
        Globals& operator=(Globals&&) = delete;

        static Globals* instance();

        // Thread-safe buffer management
        char* getMessageBuffer(const char* debug_identifier = nullptr);
        void releaseMessageBuffer(char* buffer);
        int getMessageBufferSize() const;

        ~Globals() = default;
    private:
        Globals(int buffer_chunk_size, int percent_slack);

        // Singleton instance management
        static std::unique_ptr<Globals> instance_;
        static std::once_flag init_flag_;
        static void Initialize();

        // Message buffer management
        std::unique_ptr<BitmappedObjectPool<char[MESSAGE_BUFFER_SIZE]>> message_buffers_;
        mutable std::mutex buffer_mutex_;
    };
}