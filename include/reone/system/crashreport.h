/*
 * Copyright (c) 2026 The reone project contributors
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

#include <cstddef>
#include <string>

namespace reone {

class Logger;

/**
 * Reports a crash to the file the given logger writes to, or to standard
 * error when it has none: an exception nothing caught, which ends in
 * std::terminate, an abort, and a crash on any thread. Crashes are the
 * signals SIGSEGV, SIGFPE, SIGILL and SIGBUS on POSIX, and unhandled
 * structured exceptions on Windows. The first thread to crash writes the
 * report, a headline and then its backtrace, and any other waits for the
 * process to end. The backtrace has ten seconds, and past that its frames are
 * written unnamed. A stack overflow isn't reported. The default handling then
 * runs, so core dumps and debuggers see the crash as usual. Call it once,
 * after the logger's init.
 */
void installCrashReport(const Logger &logger);

/**
 * The most frames a backtrace holds, the ones nearest where it was taken.
 * Arbitrary, but deep enough that only deep recursion loses frames.
 */
inline constexpr std::size_t kMaxBacktraceFrames = 128;

/** The calling thread's backtrace, one frame per line. */
std::string currentBacktrace();

} // namespace reone
