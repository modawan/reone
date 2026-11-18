/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/system/logger.h"
#include "reone/system/threadutil.h"

#include <SDL3/SDL_time.h>

namespace reone {

static const std::unordered_map<LogSeverity, std::string> kSeverityToName {
    {LogSeverity::Error, "ERROR"},
    {LogSeverity::Warn, "WARN"},
    {LogSeverity::Info, "INFO"},
    {LogSeverity::Debug, "DEBUG"}};

static const std::unordered_map<LogChannel, std::string> kChannelToName {
    {LogChannel::Global, "global"},
    {LogChannel::Resources, "resources"},
    {LogChannel::Resources2, "resources"},
    {LogChannel::Graphics, "graphics"},
    {LogChannel::Audio, "audio"},
    {LogChannel::GUI, "gui"},
    {LogChannel::Perception, "perception"},
    {LogChannel::Conversation, "conversation"},
    {LogChannel::Combat, "combat"},
    {LogChannel::Script, "script"},
    {LogChannel::Script2, "script"},
    {LogChannel::Script3, "script"}};

static constexpr int kBufferSize = 512;

static std::atomic<uint64_t> g_nextLoggerId {0};

Logger Logger::instance;
thread_local std::unordered_map<uint64_t, std::ostringstream *> Logger::threadBuffers;

Logger::Logger() :
    _id(g_nextLoggerId++) {
}

void Logger::init(LogSeverity minSeverity,
                  std::set<LogChannel> enabledChannels,
                  std::optional<std::string> filename) {
    if (filename) {
        init(minSeverity, std::move(enabledChannels), std::make_unique<std::ofstream>(*filename));
    } else {
        init(minSeverity, std::move(enabledChannels), std::make_unique<std::ostream>(std::clog.rdbuf()));
    }
}

void Logger::init(LogSeverity minSeverity,
                  std::set<LogChannel> enabledChannels,
                  std::unique_ptr<std::ostream> stream) {
    checkThat(!_inited, "Logger already initialized");
    _minSeverity = minSeverity;
    _enabledChannels = std::move(enabledChannels);
    _stream = std::move(stream);
    SDL_GetCurrentTime(&_initTimeNs);
    _inited = true;
}

void Logger::deinit() {
    if (!_inited) {
        return;
    }
    std::lock_guard<std::mutex> lock {_buffersMutex};
    for (auto &buffer : _buffers) {
        flush(buffer);
    }
    _inited = false;
}

void Logger::append(std::string message,
                    LogChannel channel,
                    LogSeverity severity) {
    if (!isEnabled(channel, severity)) {
        return;
    }

    // Print time in seconds from the start of the program (from the logger
    // initialization).
    int64_t timeNs = 0;
    SDL_GetCurrentTime(&timeNs);
    int64_t timeMs = (timeNs - _initTimeNs) / 1000000;

    auto formatted = str(boost::format("%1%.%2$03d %3$5s [%4%][%5%] %6%\n") %
                         (timeMs / 1000) % (timeMs % 1000) %
                         kSeverityToName.at(severity) %
                         threadName() %
                         kChannelToName.at(channel) %
                         message);
    auto &buf = threadBuffer();
    buf.write(&formatted[0], formatted.length());
    if (buf.tellp() >= kBufferSize) {
        flush(buf);
    }
}

void Logger::flush() {
    if (!_inited) {
        return;
    }
    auto it = threadBuffers.find(_id);
    if (it == threadBuffers.end()) {
        return;
    }
    flush(*it->second);
}

std::ostringstream &Logger::threadBuffer() {
    auto it = threadBuffers.find(_id);
    if (it != threadBuffers.end()) {
        return *it->second;
    }
    std::lock_guard<std::mutex> lock {_buffersMutex};
    auto &buffer = _buffers.emplace_back();
    threadBuffers[_id] = &buffer;
    return buffer;
}

void Logger::flush(std::ostringstream &buffer) {
    if (!_stream) {
        return;
    }
    auto str = buffer.str();
    {
        std::lock_guard<std::mutex> lock {_streamMutex};
        _stream->write(&str[0], str.length());
        _stream->flush();
    }
    buffer.str("");
}

} // namespace reone
