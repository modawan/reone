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

#include "reone/system/crashreport.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include <boost/stacktrace.hpp>

#include "reone/system/logger.h"
#include "reone/system/threadutil.h"

#include "report.h"

namespace reone {

namespace crashreport {

using NativeFrame = boost::stacktrace::frame::native_frame_ptr_t;

// The report runs once, on the first thread that crashes. Only lock-free
// atomics are safe in a signal handler.
static std::atomic<bool> g_reportClaimed {false};
static_assert(std::atomic<bool>::is_always_lock_free);
static thread_local bool t_ownsReport = false;

static int64_t g_startTimeNs = 0;
static std::uintptr_t g_moduleBase = 0;

// The crashing thread's frames, for symbolising and, past the deadline, for
// writing out raw. One more than the cap, for the terminating zero.
static NativeFrame g_frames[kMaxBacktraceFrames + 1] {};
static std::atomic<std::size_t> g_frameCount {0};

// Set up by setUpThisThread on the threads it covers. The name is a plain array,
// so reading it in a signal handler runs no constructor.
static thread_local char t_threadName[32] {};

Claim claimReport() {
    if (t_ownsReport) {
        return Claim::Again;
    }
    if (g_reportClaimed.exchange(true)) {
        return Claim::Taken;
    }
    t_ownsReport = true;
    return Claim::First;
}

void writeHeadline(const char *what) {
    auto elapsedMs = (currentTimeNs() - g_startTimeNs) / 1000000;
    if (elapsedMs < 0) {
        elapsedMs = 0;
    }
    FixedLine line;
    line.appendDecimal(static_cast<std::uint64_t>(elapsedMs / 1000))
        .append(".")
        .appendDecimal(static_cast<std::uint64_t>(elapsedMs % 1000), 3)
        .append(" ERROR [");
    if (t_threadName[0] != '\0') {
        line.append(t_threadName);
    } else {
        appendCurrentThread(line);
    }
    line.append("][global] ").append(what).append("\n");
    writeToSink(line);
}

void writeToSink(const FixedLine &line) {
    writeToSink(line.data(), line.size());
}

void writeToSink(const std::string &text) {
    writeToSink(text.data(), text.size());
}

std::size_t dumpFrames() {
    auto depth = boost::stacktrace::safe_dump_to(g_frames, sizeof(g_frames));
    // A walk always holds this function's own frame, so an empty one failed.
    // RtlCaptureStackBackTrace sometimes fails the first walk of a deep stack,
    // and a second walk succeeds.
    if (depth <= 1) {
        depth = boost::stacktrace::safe_dump_to(g_frames, sizeof(g_frames));
    }
    // The depth counts the terminating zero.
    auto frames = depth > 0 ? depth - 1 : 0;
    g_frameCount.store(frames);
    return frames;
}

std::size_t capturedFrames() {
    return g_frameCount.load();
}

void writeSymbolisedFrames(std::size_t frames) {
    auto trace = boost::stacktrace::stacktrace::from_dump(g_frames, frames * sizeof(NativeFrame));
    writeToSink(boost::stacktrace::to_string(trace));
}

void writeRawFrames() {
    auto frames = g_frameCount.load();
    if (frames == 0) {
        return;
    }
    FixedLine header;
    header.append("The trace couldn't be symbolised in time. Frames, with the engine loaded at 0x")
        .appendHex(g_moduleBase, 2 * sizeof(std::uintptr_t))
        .append(":\n");
    writeToSink(header);
    for (std::size_t i = 0; i < frames; ++i) {
        FixedLine line;
        // A frame is a code address, which converts to an integer of its size.
        line.appendDecimal(i, 2)
            .append("# 0x")
            .appendHex(reinterpret_cast<std::uintptr_t>(g_frames[i]), 2 * sizeof(std::uintptr_t))
            .append("\n");
        writeToSink(line);
    }
}

// What follows "exception" in a report: its message, or that it isn't a
// std::exception.
static std::string describeException(const std::exception_ptr &exception) {
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception &ex) {
        return std::string(": ") + ex.what();
    } catch (...) {
        return " of an unknown type";
    }
}

void writeExceptionBeingHandled() {
    if (auto exception = std::current_exception()) {
        writeToSink("While handling an exception" + describeException(exception) + "\n");
    }
}

const char *describeSignal(int sig) {
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGFPE:
        return "SIGFPE (arithmetic error)";
    case SIGILL:
        return "SIGILL (illegal instruction)";
#ifndef _WIN32
    case SIGBUS:
        return "SIGBUS (bus error)";
#endif
    case SIGABRT:
        return "SIGABRT (abort)";
    default:
        return "a fatal signal";
    }
}

[[noreturn]] static void abortWithoutReport() {
    // Resetting fails only for an invalid signal number.
    (void)std::signal(SIGABRT, SIG_DFL);
    std::abort();
}

void onTerminate() {
    switch (claimReport()) {
    case Claim::Taken:
        park();
    case Claim::Again:
        abortWithoutReport();
    case Claim::First:
        break;
    }
    try {
        auto exception = std::current_exception();
        auto what = exception ? "Uncaught exception" + describeException(exception)
                              : std::string("std::terminate called without an exception");
        writeHeadline(what.c_str());
    } catch (...) {
        writeHeadline("Uncaught exception");
    }
    writeAbortTraceWithinDeadline(false);
    abortWithoutReport();
}

// Prepares the calling thread for a crash: its name where the report can read
// it.
static void setUpThisThread() {
    const auto &name = threadName();
    auto length = name.size() < sizeof(t_threadName) - 1 ? name.size() : sizeof(t_threadName) - 1;
    std::memcpy(t_threadName, name.data(), length);
    t_threadName[length] = '\0';
}

} // namespace crashreport

void installCrashReport(const Logger &logger) {
    crashreport::openSink(logger.filename());
    crashreport::g_startTimeNs = logger.initTimeNs();
    crashreport::g_moduleBase = crashreport::engineLoadAddress();
    std::set_terminate(crashreport::onTerminate);
    crashreport::installHandlers();
    crashreport::setUpThisThread();
}

std::string currentBacktrace() {
    boost::stacktrace::stacktrace trace(0, kMaxBacktraceFrames);
    // A trace always holds this function's own frame, so an empty one is a
    // failed capture. RtlCaptureStackBackTrace sometimes fails like this on
    // the first walk of a deep stack, and a second walk succeeds.
    if (trace.empty()) {
        trace = boost::stacktrace::stacktrace(0, kMaxBacktraceFrames);
    }
    return boost::stacktrace::to_string(trace);
}

} // namespace reone
