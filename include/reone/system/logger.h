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

#pragma once

#include "checkutil.h"
#include "types.h"

namespace reone {

class Logger : boost::noncopyable {
public:
    static Logger instance;

    Logger();

    ~Logger() {
        deinit();
    }

    void init(LogSeverity minSeverity,
              std::set<LogChannel> enabledChannels,
              std::optional<std::string> filename);

    /** Writes to the given stream rather than a file or the console. */
    void init(LogSeverity minSeverity,
              std::set<LogChannel> enabledChannels,
              std::unique_ptr<std::ostream> stream);

    void append(std::string message,
                LogChannel channel,
                LogSeverity severity);

    void flush();

    bool isChannelEnabled(LogChannel channel) const {
        return _enabledChannels.count(channel) > 0;
    }

    /** Whether a message of this channel and severity would be written. */
    bool isEnabled(LogChannel channel, LogSeverity severity) const {
        return _inited && passesFilter(_minSeverity, _enabledChannels, channel, severity);
    }

    /**
     * Whether a message clears a minimum severity and a set of enabled
     * channels. Warnings and errors are written whatever their channel, so
     * the channel set only filters debug and info.
     */
    static bool passesFilter(LogSeverity minSeverity,
                             const std::set<LogChannel> &enabledChannels,
                             LogChannel channel,
                             LogSeverity severity) {
        if (severity < minSeverity) {
            return false;
        }
        return severity >= LogSeverity::Warn || enabledChannels.count(channel) > 0;
    }

    /** Whether messages are written yet; before init they are dropped. */
    bool isInited() const { return _inited; }

private:
    // Every thread that writes to a logger gets a buffer of its own in that
    // logger. They are found by the logger's id, not its address, so a logger
    // created where a destroyed one lived never picks up its buffers.
    static thread_local std::unordered_map<uint64_t, std::ostringstream *> threadBuffers;

    uint64_t _id;
    LogSeverity _minSeverity {LogSeverity::Warn};
    std::set<LogChannel> _enabledChannels;
    std::unique_ptr<std::ostream> _stream;
    bool _inited {false};

    std::list<std::ostringstream> _buffers;

    // Guards _buffers, which each thread adds its buffer to on its first line.
    std::mutex _buffersMutex;
    // Guards _stream, which every thread's flush writes to.
    std::mutex _streamMutex;

    int64_t _initTimeNs {0};

    void deinit();
    std::ostringstream &threadBuffer();
    void flush(std::ostringstream &buffer);
};

} // namespace reone
