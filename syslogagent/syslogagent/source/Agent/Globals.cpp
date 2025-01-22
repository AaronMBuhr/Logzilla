/*
SyslogAgent: a syslog agent for Windows
Copyright 2021 Logzilla Corp.
*/

#include "stdafx.h"
#include <memory>
#include "Globals.h"
#include "Logger.h"

using namespace std;

namespace Syslog_agent {

std::unique_ptr<Globals> Globals::instance_ = nullptr;
std::once_flag Globals::init_flag_;

Globals::Globals(int buffer_chunk_size, int percent_slack) {
    message_buffers_ = make_unique<BitmappedObjectPool<char[MESSAGE_BUFFER_SIZE]>>(
        buffer_chunk_size, percent_slack);
}

void Globals::Initialize() {
    std::call_once(init_flag_, []() {
        try {
            instance_.reset(new Globals(BUFFER_CHUNK_SIZE, PERCENT_SLACK));
        }
        catch (const std::exception& e) {
            Logger::fatal("Failed to initialize Globals: %s\n", e.what());
            throw; // Re-throw to prevent use of uninitialized instance
        }
    });
}

Globals* Globals::instance() {
    Initialize();
    return instance_.get();
}

char* Globals::getMessageBuffer(const char* debug_identifier) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    auto* buffer = message_buffers_->getAndMarkNextUnused();
    if (!buffer) {
        if (debug_identifier) {
            Logger::recoverable_error("Failed to allocate message buffer for %s\n", debug_identifier);
        } else {
            Logger::recoverable_error("Failed to allocate message buffer\n");
        }
        return nullptr;
    }
    return static_cast<char*>(*buffer);
}

void Globals::releaseMessageBuffer(char* buffer) {
    if (!buffer) return;

    std::lock_guard<std::mutex> lock(buffer_mutex_);
    auto ptr = reinterpret_cast<char(*)[MESSAGE_BUFFER_SIZE]>(buffer);
    message_buffers_->markAsUnused(ptr);
}

int Globals::getMessageBufferSize() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return static_cast<int>(message_buffers_->countBuffers());
}

}