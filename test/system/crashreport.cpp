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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <streambuf>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "reone/system/crashreport.h"
#include "reone/system/logger.h"
#include "reone/system/threadutil.h"

using namespace reone;
using namespace testing;

// The helpers below have external linkage and are never inlined, and none
// ends in a tail call, so their frames and names survive optimisation. MSVC's
// Release builds have no PDB to name them from, so the name checks are left
// out there.
#ifdef _MSC_VER
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif

// A helper's name as it appears in a backtrace, written so that renaming the
// helper without its checks fails to compile.
#define NAME_OF(function) (static_cast<void>(&function), #function)

static constexpr char kHelperMessage[] = "thrown for the test";
static constexpr char kWorkerMessage[] = "thrown on a worker thread";

TEST_NOINLINE std::string backtraceFromNamedHelper() {
    auto trace = currentBacktrace();
    trace.push_back('\n');
    return trace;
}

// Written after each call so that the recursion below can't become a loop.
static volatile int g_depthSink = 0;

TEST_NOINLINE std::string backtraceFromDepth(std::size_t depth) {
    if (depth == 0) {
        return currentBacktrace();
    }
    auto trace = backtraceFromDepth(depth - 1);
    g_depthSink = static_cast<int>(depth);
    return trace;
}

TEST_NOINLINE void throwFromNamedHelper() {
    throw std::runtime_error(kHelperMessage);
}

TEST_NOINLINE void crashInNamedHelper() {
#ifdef _WIN32
    RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#else
    std::raise(SIGSEGV);
#endif
    std::abort();
}

TEST_NOINLINE void throwFromWorkerThread() {
    throw std::runtime_error(kWorkerMessage);
}

static void installReportToStderr() {
    // Without a file, the report goes to standard error, which death tests
    // read.
    static Logger logger;
    logger.init(LogSeverity::Debug, {LogChannel::Global}, std::nullopt);
    installCrashReport(logger);
}

static std::size_t countOf(const std::string &text, const std::string &part) {
    std::size_t count = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) {
        ++count;
    }
    return count;
}

// A stream whose first write never returns, so the logger thread writing it
// holds the logger's stream lock for good.
class StuckBuffer : public std::streambuf {
public:
    std::future<void> stuck() { return _stuck.get_future(); }

protected:
    int overflow(int) override { return stick(); }
    std::streamsize xsputn(const char *, std::streamsize) override { return stick(); }

private:
    std::promise<void> _stuck;
    bool _signalled {false};

    int stick() {
        if (!_signalled) {
            _signalled = true;
            _stuck.set_value();
        }
        for (;;) {
            std::this_thread::sleep_for(std::chrono::hours(1));
        }
    }
};

TEST(CrashReport, should_name_the_calling_function_in_a_backtrace) {
    auto trace = backtraceFromNamedHelper();

    EXPECT_FALSE(trace.empty());
#ifndef _MSC_VER
    EXPECT_NE(std::string::npos, trace.find(NAME_OF(backtraceFromNamedHelper)));
#endif
}

TEST(CrashReport, should_keep_only_the_frames_nearest_a_deep_backtrace) {
    auto trace = backtraceFromDepth(3 * kMaxBacktraceFrames);

    EXPECT_EQ(kMaxBacktraceFrames, static_cast<std::size_t>(std::count(trace.begin(), trace.end(), '\n')));
}

TEST(CrashReport, should_trace_where_a_caught_exception_was_thrown) {
    std::string trace;
    try {
        throwFromNamedHelper();
    } catch (const std::exception &) {
        trace = thrownFromBacktrace();
    }

    if (trace.empty()) {
        GTEST_SKIP() << "This build can't record where exceptions are thrown";
    }
#ifndef _MSC_VER
    EXPECT_NE(std::string::npos, trace.find(NAME_OF(throwFromNamedHelper)));
#endif
}

// A worker thread is where Windows' C runtimes don't turn a crash into a
// signal, and it's outside the SEH handler gtest puts around each test body
// on MSVC.
TEST(CrashReportDeathTest, should_report_a_crash_on_a_worker_thread_with_its_backtrace) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
#ifdef _WIN32
    const char *crash = "C0000005";
#else
    const char *crash = "SIGSEGV";
#endif
#ifdef _MSC_VER
    Matcher<const std::string &> expected = HasSubstr(crash);
#else
    Matcher<const std::string &> expected = AllOf(HasSubstr(crash), HasSubstr(NAME_OF(crashInNamedHelper)));
#endif

    EXPECT_DEATH(
        {
            installReportToStderr();
            std::thread(crashInNamedHelper).join();
        },
        expected);
}

TEST(CrashReportDeathTest, should_write_one_report_when_threads_crash_together) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_DEATH(
        {
            installReportToStderr();
            std::thread first(crashInNamedHelper);
            std::thread second(crashInNamedHelper);
            first.join();
            second.join();
        },
        AllOf(Truly([](const std::string &output) { return countOf(output, " ERROR [") == 1; }), HasSubstr("0# ")));
}

TEST(CrashReportDeathTest, should_report_while_another_thread_is_stuck_in_the_logger) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
#ifdef _WIN32
    const char *crash = "C0000005";
#else
    const char *crash = "SIGSEGV";
#endif

    EXPECT_DEATH(
        {
            static StuckBuffer buffer;
            static Logger logger;
            logger.init(LogSeverity::Debug, {LogChannel::Global}, std::make_unique<std::ostream>(&buffer));
            installCrashReport(logger);
            auto stuck = buffer.stuck();
            std::thread([] { logger.append("held", LogChannel::Global, LogSeverity::Error); }).detach();
            stuck.wait();
            std::thread(crashInNamedHelper).join();
        },
        HasSubstr(crash));
}

TEST(CrashReportDeathTest, should_name_the_crashing_thread) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_DEATH(
        {
            setThreadName("named-for-the-test");
            installReportToStderr();
            std::abort();
        },
        HasSubstr("[named-for-the-test]"));
}

TEST(CrashReportDeathTest, should_report_the_exception_being_handled_when_aborting) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_DEATH(
        {
            installReportToStderr();
            try {
                throwFromNamedHelper();
            } catch (const std::exception &) {
                std::abort();
            }
        },
        AllOf(HasSubstr("SIGABRT"), HasSubstr(kHelperMessage)));
}

// Nothing catches the exception, so the stack isn't unwound before
// std::terminate and the throwing frame is still on it. MSVC reports it as
// an abort, because its terminate handlers are per thread.
TEST(CrashReportDeathTest, should_report_an_uncaught_exception_with_where_it_was_thrown) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
#ifdef _MSC_VER
    Matcher<const std::string &> expected = HasSubstr(kWorkerMessage);
#else
    Matcher<const std::string &> expected = AllOf(HasSubstr(kWorkerMessage), HasSubstr(NAME_OF(throwFromWorkerThread)));
#endif

    EXPECT_DEATH(
        {
            installReportToStderr();
            std::thread(throwFromWorkerThread).join();
        },
        expected);
}
