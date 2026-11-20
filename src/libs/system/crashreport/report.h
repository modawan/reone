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
#include <cstdint>
#include <optional>
#include <string>

// The crash path shared between report.cpp and the platform's own file,
// posix.cpp or windows.cpp. Everything here runs while the process is dying,
// so what a signal handler calls must be safe in one: no locks, and no
// allocation until the deadline is armed.
namespace reone::crashreport {

// How long walking and symbolising the stack may take before the report gives
// up on it. Three times the slowest symbolising measured, a Debug build with
// compressed debug info.
inline constexpr unsigned kDeadlineSeconds = 10;

// Stack a thread sets aside for reporting a crash after its own stack has
// overflowed: the alternate signal stack on POSIX, and the stack guarantee on
// Windows. Four times what a Debug build's signal handler was measured to
// need, which also covered the Windows filter in the stack-overflow demo.
inline constexpr std::size_t kCrashStackSize = 64 * 1024;

// Builds a line in a fixed buffer, so the headline needs no allocation.
// Anything past the buffer is dropped.
class FixedLine {
public:
    FixedLine &append(const char *text) {
        while (*text != '\0' && _size < sizeof(_text)) {
            _text[_size++] = *text++;
        }
        return *this;
    }

    FixedLine &appendDecimal(std::uint64_t value, std::size_t minDigits = 1) {
        char digits[20] {};
        std::size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < minDigits && count < sizeof(digits)) {
            digits[count++] = '0';
        }
        while (count > 0 && _size < sizeof(_text)) {
            _text[_size++] = digits[--count];
        }
        return *this;
    }

    FixedLine &appendHex(std::uint64_t value, std::size_t digits, bool lowercase = false) {
        const char *hexDigits = lowercase ? "0123456789abcdef" : "0123456789ABCDEF";
        for (std::size_t i = digits; i > 0 && _size < sizeof(_text); --i) {
            _text[_size++] = hexDigits[(value >> (4 * (i - 1))) & 0xF];
        }
        return *this;
    }

    const char *data() const { return _text; }
    std::size_t size() const { return _size; }

private:
    char _text[512] {};
    std::size_t _size {0};
};

// In report.cpp.

enum class Claim {
    First,
    Again,
    Taken
};

// Claims the report for the calling thread. The first crashing thread gets
// First; another thread gets Taken, and should park; the reporting thread
// crashing again gets Again, and should go straight to the default handling.
Claim claimReport();

// Writes a line in the logger's format: seconds since the logger started,
// severity, thread and channel. Safe in a signal handler.
void writeHeadline(const char *what);

void writeToSink(const FixedLine &line);
void writeToSink(const std::string &text);

// Walks the calling thread's stack into the frame buffer and returns how many
// frames it holds.
std::size_t dumpFrames();

// The frames dumpFrames last stored.
std::size_t capturedFrames();

void writeSymbolisedFrames(std::size_t frames);

// Writes the captured frames unnamed, with the engine's load address, for when
// symbolising missed the deadline.
void writeRawFrames();

// Writes the exception the calling thread is handling, if any.
void writeExceptionBeingHandled();

const char *describeSignal(int sig);

[[noreturn]] void onTerminate();

// Whether the calling thread has stack set aside for reporting an overflow.
bool hasCrashStack();

// In posix.cpp or windows.cpp.

// Opens the report's own append handle to the logger's file, or standard
// error when there's no file or it can't be opened.
void openSink(const std::optional<std::string> &filename);

// Safe in a signal handler.
void writeToSink(const char *data, std::size_t size);

// Nanoseconds since the Unix epoch, as SDL_GetCurrentTime counts them for the
// logger. Safe in a signal handler.
int64_t currentTimeNs();

// Writes the calling thread's ID the way the logger writes it for a thread
// without a name. Safe in a signal handler.
void appendCurrentThread(FixedLine &line);

std::uintptr_t engineLoadAddress();

// Waits for the reporting thread's default handling to end the process.
[[noreturn]] void park();

// Writes the calling thread's trace within the deadline; past it, writes the
// raw frames and ends the process as abort would.
void writeAbortTraceWithinDeadline(bool describeExceptionBeingHandled);

// Sets aside kCrashStackSize bytes at crashStack for reporting an overflow on
// the calling thread, and returns whether it could. Windows reserves its own
// and ignores crashStack. The memory must stay valid while the thread runs.
bool setUpThread(char *crashStack);

// Installs the platform's crash handlers: signals, and on Windows the
// unhandled-exception filter.
void installHandlers();

} // namespace reone::crashreport
