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
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <type_traits>

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "reone/system/crashreport.h"

#include "report.h"

namespace reone::crashreport {

static int g_sink = -1;

void openSink(const std::optional<std::string> &filename) {
    if (filename) {
        g_sink = open(filename->c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
    }
    if (g_sink < 0) {
        g_sink = STDERR_FILENO;
    }
}

void writeToSink(const char *data, std::size_t size) {
    while (size > 0) {
        auto written = write(g_sink, data, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
}

int64_t currentTimeNs() {
    timespec time {};
    // Fails only for a clock the system lacks, and every POSIX system has this
    // one.
    (void)clock_gettime(CLOCK_REALTIME, &time);
    return static_cast<int64_t>(time.tv_sec) * 1000000000 + time.tv_nsec;
}

// Writes a pthread_t the way the standard library prints a std::thread::id:
// a number on Linux, and a pointer in hex on macOS.
template <typename Thread>
static void appendThread(FixedLine &line, Thread thread) {
    if constexpr (std::is_pointer_v<Thread>) {
        // Any object pointer converts to an integer of its size.
        auto value = reinterpret_cast<std::uintptr_t>(thread);
        std::size_t digits = 1;
        for (auto rest = value >> 4; rest != 0; rest >>= 4) {
            ++digits;
        }
        line.append("0x").appendHex(value, digits, true);
    } else {
        line.appendDecimal(static_cast<std::uint64_t>(thread));
    }
}

void appendCurrentThread(FixedLine &line) {
    appendThread(line, pthread_self());
}

std::uintptr_t engineLoadAddress() {
    Dl_info info {};
    // C++ only conditionally supports a function pointer as void *, but POSIX
    // requires it, and dladdr takes one.
    if (dladdr(reinterpret_cast<void *>(&installCrashReport), &info) == 0) {
        return 0;
    }
    // The base is an object pointer, which converts to an integer of its size.
    return reinterpret_cast<std::uintptr_t>(info.dli_fbase);
}

void park() {
    for (;;) {
        pause();
    }
}

// The signal the deadline raises, so the process still ends the way the crash
// would have ended it.
static std::atomic<int> g_deadlineSignal {0};
static_assert(std::atomic<int>::is_always_lock_free);

static void resetAndRaise(int sig) {
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    // These fail only for an invalid signal number.
    (void)sigaction(sig, &action, nullptr);
    (void)raise(sig);
}

static void onDeadline(int) {
    writeRawFrames();
    resetAndRaise(g_deadlineSignal.load());
}

// Walking can deadlock on the unwinder's lock and symbolising on the
// allocator's, if another thread held them when the crash came, so both run
// under an alarm. Its handler ends the process with the crash's own signal.
static void writeTraceWithinDeadline(int signalOnDeadline, bool describeExceptionBeingHandled) {
    g_deadlineSignal.store(signalOnDeadline);
    struct sigaction action {};
    action.sa_handler = onDeadline;
    sigemptyset(&action.sa_mask);
    // Installing the handler and unblocking the signal fail only for an
    // invalid signal number or mask operation.
    (void)sigaction(SIGALRM, &action, nullptr);
    // abort() on macOS blocks every other signal on its thread before it
    // raises SIGABRT, which would hold the alarm back for good.
    sigset_t alarmOnly;
    sigemptyset(&alarmOnly);
    sigaddset(&alarmOnly, SIGALRM);
    (void)pthread_sigmask(SIG_UNBLOCK, &alarmOnly, nullptr);
    alarm(kDeadlineSeconds);
    try {
        auto frames = dumpFrames();
        if (describeExceptionBeingHandled) {
            writeExceptionBeingHandled();
        }
        writeSymbolisedFrames(frames);
    } catch (...) {
        // Giving up on the trace leaves the headline, which is already out.
    }
    alarm(0);
}

void writeAbortTraceWithinDeadline(bool describeExceptionBeingHandled) {
    writeTraceWithinDeadline(SIGABRT, describeExceptionBeingHandled);
}

static void onFatalSignal(int sig) {
    switch (claimReport()) {
    case Claim::Taken:
        park();
    case Claim::Again:
        break;
    case Claim::First: {
        FixedLine what;
        what.append("Received ").append(describeSignal(sig));
        writeHeadline(what.data());
        // An abort from a catch block still has its exception current.
        writeTraceWithinDeadline(sig, sig == SIGABRT);
        break;
    }
    }
    resetAndRaise(sig);
}

static constexpr int kFatalSignals[] = {SIGSEGV, SIGFPE, SIGILL, SIGBUS, SIGABRT};

void installHandlers() {
    for (int sig : kFatalSignals) {
        // The handler stays installed, so a second crashing thread parks while
        // the first reports. It isn't blocked while it runs, so a crash in the
        // report comes straight back and ends the process.
        struct sigaction action {};
        action.sa_handler = onFatalSignal;
        action.sa_flags = SA_NODEFER;
        sigemptyset(&action.sa_mask);
        // Installing fails only for an invalid signal number, and these are
        // all standard signals.
        (void)sigaction(sig, &action, nullptr);
    }
}

} // namespace reone::crashreport
