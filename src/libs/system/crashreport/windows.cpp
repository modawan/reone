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

#include <atomic>
#include <csignal>
#include <cstdint>
#include <exception>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "report.h"

namespace reone::crashreport {

// The exit code the C runtime's abort() ends a process with.
static constexpr DWORD kAbortExitCode = 3;

static HANDLE g_sink = INVALID_HANDLE_VALUE;

void openSink(const std::optional<std::string> &filename) {
    if (filename) {
        g_sink = CreateFileA(filename->c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (g_sink == INVALID_HANDLE_VALUE) {
        g_sink = GetStdHandle(STD_ERROR_HANDLE);
    }
}

void writeToSink(const char *data, std::size_t size) {
    // WriteFile takes a 32-bit count. The chunk size is arbitrary, well under
    // that limit.
    constexpr std::size_t kMaxChunk = 0x10000000;
    while (size > 0) {
        DWORD written = 0;
        auto chunk = static_cast<DWORD>(size > kMaxChunk ? kMaxChunk : size);
        if (!WriteFile(g_sink, data, chunk, &written, nullptr) || written == 0) {
            return;
        }
        data += written;
        size -= written;
    }
}

int64_t currentTimeNs() {
    // FILETIME counts 100 ns ticks from 1601; this many ticks reach 1970.
    constexpr int64_t kTicksTo1970 = 116444736000000000;
    FILETIME time {};
    GetSystemTimePreciseAsFileTime(&time);
    auto ticks = (static_cast<int64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    return (ticks - kTicksTo1970) * 100;
}

void appendCurrentThread(FixedLine &line) {
    line.appendDecimal(GetCurrentThreadId());
}

std::uintptr_t engineLoadAddress() {
    // A module handle is the module's load address, an object pointer that
    // converts to an integer of its size.
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
}

void park() {
    for (;;) {
        Sleep(INFINITE);
    }
}

// The C runtime's own filter, which installHandlers replaces.
static LPTOP_LEVEL_EXCEPTION_FILTER g_runtimeFilter = nullptr;

// The helper thread symbolises the frames on a stack of its own, and keeps
// dbghelp, which is single-threaded, to one caller. It is also the watchdog
// for the walk.
static HANDLE g_framesReady = nullptr;
static std::atomic<DWORD> g_exitCode {0};

static DWORD WINAPI symboliseFrames(void *) {
    if (WaitForSingleObject(g_framesReady, kDeadlineSeconds * 1000) != WAIT_OBJECT_0) {
        // The walk may have finished before the crashing thread got stuck.
        writeRawFrames();
        // Ending the calling process doesn't return, so there's no failure to
        // see.
        (void)TerminateProcess(GetCurrentProcess(), g_exitCode.load());
    }
    try {
        writeSymbolisedFrames(capturedFrames());
    } catch (...) {
        // Giving up on the trace leaves the headline, which is already out.
    }
    return 0;
}

static void writeTraceWithinDeadline(DWORD exitCodeOnDeadline, bool describeExceptionBeingHandled) {
    g_exitCode.store(exitCodeOnDeadline);
    auto helper = CreateThread(nullptr, 0, symboliseFrames, nullptr, 0, nullptr);
    dumpFrames();
    if (describeExceptionBeingHandled) {
        try {
            writeExceptionBeingHandled();
        } catch (...) {
            // The frames matter more than the exception's message.
        }
    }
    SetEvent(g_framesReady);
    // Under the loader lock, the helper may never start, and the wait still
    // ends at the deadline.
    if (!helper || WaitForSingleObject(helper, kDeadlineSeconds * 1000) != WAIT_OBJECT_0) {
        writeRawFrames();
    }
    if (helper) {
        CloseHandle(helper);
    }
}

void writeAbortTraceWithinDeadline(bool describeExceptionBeingHandled) {
    writeTraceWithinDeadline(kAbortExitCode, describeExceptionBeingHandled);
}

static void onFatalSignal(int sig) {
    // The C runtime resets the handler before calling it, and SIGABRT's is
    // process-wide, so another thread aborting now would end the process
    // halfway through the report. Installing fails only for an invalid signal
    // number.
    (void)std::signal(sig, onFatalSignal);
    switch (claimReport()) {
    case Claim::Taken:
        park();
    case Claim::Again:
        break;
    case Claim::First: {
        FixedLine what;
        what.append("Received ").append(describeSignal(sig));
        writeHeadline(what.data());
        // An abort from MSVC's per-thread terminate handler, or from a catch
        // block, still has its exception current.
        writeTraceWithinDeadline(kAbortExitCode, sig == SIGABRT);
        break;
    }
    }
    // Resetting and raising fail only for an invalid signal number.
    (void)std::signal(sig, SIG_DFL);
    (void)std::raise(sig);
}

static const char *describeExceptionCode(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_IN_PAGE_ERROR:
        return "page could not be read";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer divide by zero";
    case EXCEPTION_INT_OVERFLOW:
        return "integer overflow";
    default:
        return "unrecognised exception";
    }
}

// Both C runtimes turn only a main-thread access violation into SIGSEGV, and
// this filter sees an unhandled exception on any thread. Windows Error
// Reporting and debuggers still see the crash afterwards.
static LONG WINAPI onUnhandledException(EXCEPTION_POINTERS *info) {
    // GCC's unwinder raises each throw as an exception with "GCC" in its code,
    // and the top byte telling throw, unwind and forced unwind apart. One that
    // nothing catches must be continued so that libstdc++ calls std::terminate,
    // which reports it, as the MinGW runtime's own filter does.
    constexpr DWORD kGccExceptionMask = 0x20FFFFFF;
    constexpr DWORD kGccException = 0x20474343;
    auto code = info->ExceptionRecord->ExceptionCode;
    auto continuable = (info->ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE) == 0;
    if ((code & kGccExceptionMask) == kGccException && continuable) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    // MSVC raises each throw as exception 0xE06D7363, "msc" in its low bytes.
    // One that nothing catches on the main thread belongs to the C runtime's
    // filter, which makes it current and calls std::terminate, so onTerminate
    // reports it with its message.
    constexpr DWORD kMsvcException = 0xE06D7363;
    if (code == kMsvcException && g_runtimeFilter) {
        return g_runtimeFilter(info);
    }
    // Too little stack is left after an overflow to walk the stack and log.
    if (code == EXCEPTION_STACK_OVERFLOW) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    switch (claimReport()) {
    case Claim::Taken:
        park();
    case Claim::Again:
        return EXCEPTION_CONTINUE_SEARCH;
    case Claim::First:
        break;
    }
    FixedLine what;
    what.append("Unhandled exception 0x").appendHex(code, 8).append(" (").append(describeExceptionCode(code)).append(")");
    writeHeadline(what.data());
    writeTraceWithinDeadline(code, false);
    return EXCEPTION_CONTINUE_SEARCH;
}

void installHandlers() {
    g_framesReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    // MSVC's runtime filter turns C++ exceptions into std::terminate, and
    // MinGW's turns crashes into the signals no longer handled here.
    g_runtimeFilter = SetUnhandledExceptionFilter(onUnhandledException);
    // Windows raises crashes as structured exceptions, which the filter
    // reports, so abort is its only fatal signal. Installing fails only for an
    // invalid signal number.
    (void)std::signal(SIGABRT, onFatalSignal);
}

} // namespace reone::crashreport
