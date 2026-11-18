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

#include <gtest/gtest.h>

#include "reone/system/logger.h"

using namespace reone;

TEST(Logger, should_write_warnings_and_errors_on_a_disabled_channel) {
    auto channels = std::set<LogChannel> {LogChannel::Global};

    EXPECT_TRUE(Logger::passesFilter(LogSeverity::Debug, channels, LogChannel::Graphics, LogSeverity::Warn));
    EXPECT_TRUE(Logger::passesFilter(LogSeverity::Debug, channels, LogChannel::Graphics, LogSeverity::Error));
}

TEST(Logger, should_drop_debug_and_info_on_a_disabled_channel) {
    auto channels = std::set<LogChannel> {LogChannel::Global};

    EXPECT_FALSE(Logger::passesFilter(LogSeverity::Debug, channels, LogChannel::Graphics, LogSeverity::Debug));
    EXPECT_FALSE(Logger::passesFilter(LogSeverity::Debug, channels, LogChannel::Graphics, LogSeverity::Info));
    EXPECT_TRUE(Logger::passesFilter(LogSeverity::Debug, channels, LogChannel::Global, LogSeverity::Debug));
}

TEST(Logger, should_drop_anything_below_the_minimum_severity) {
    auto channels = std::set<LogChannel> {LogChannel::Graphics};

    EXPECT_FALSE(Logger::passesFilter(LogSeverity::Error, channels, LogChannel::Graphics, LogSeverity::Warn));
    EXPECT_TRUE(Logger::passesFilter(LogSeverity::Error, channels, LogChannel::Graphics, LogSeverity::Error));
}

static bool contains(const std::stringbuf &output, const std::string &text) {
    return output.str().find(text) != std::string::npos;
}

TEST(Logger, should_hold_a_line_until_flushed) {
    std::stringbuf output;
    Logger logger;
    logger.init(LogSeverity::Debug, {LogChannel::Global}, std::make_unique<std::ostream>(&output));

    logger.append("held line", LogChannel::Global, LogSeverity::Info);
    EXPECT_FALSE(contains(output, "held line"));

    logger.flush();
    EXPECT_TRUE(contains(output, "held line"));
}

TEST(Logger, should_write_lines_once_its_buffer_fills) {
    std::stringbuf output;
    Logger logger;
    logger.init(LogSeverity::Debug, {LogChannel::Global}, std::make_unique<std::ostream>(&output));

    // Longer than the logger's buffer on its own.
    logger.append(std::string(600, 'x'), LogChannel::Global, LogSeverity::Info);

    EXPECT_TRUE(contains(output, std::string(600, 'x')));
}

TEST(Logger, should_write_held_lines_when_destroyed) {
    std::stringbuf output;
    {
        Logger logger;
        logger.init(LogSeverity::Debug, {LogChannel::Global}, std::make_unique<std::ostream>(&output));
        logger.append("held line", LogChannel::Global, LogSeverity::Info);
    }

    EXPECT_TRUE(contains(output, "held line"));
}

TEST(Logger, should_keep_two_loggers_lines_apart_on_one_thread) {
    std::stringbuf firstOutput;
    std::stringbuf secondOutput;
    Logger first;
    Logger second;
    first.init(LogSeverity::Debug, {LogChannel::Global}, std::make_unique<std::ostream>(&firstOutput));
    second.init(LogSeverity::Debug, {LogChannel::Global}, std::make_unique<std::ostream>(&secondOutput));

    first.append("from first", LogChannel::Global, LogSeverity::Info);
    second.append("from second", LogChannel::Global, LogSeverity::Info);
    first.flush();
    second.flush();

    EXPECT_TRUE(contains(firstOutput, "from first"));
    EXPECT_FALSE(contains(firstOutput, "from second"));
    EXPECT_TRUE(contains(secondOutput, "from second"));
    EXPECT_FALSE(contains(secondOutput, "from first"));
}

TEST(Logger, should_write_a_warning_and_the_lines_before_it_at_once) {
    std::stringbuf output;
    Logger logger;
    logger.init(LogSeverity::Debug, {LogChannel::Global}, std::make_unique<std::ostream>(&output));

    logger.append("earlier line", LogChannel::Global, LogSeverity::Info);
    logger.append("the warning", LogChannel::Global, LogSeverity::Warn);

    EXPECT_TRUE(contains(output, "earlier line"));
    EXPECT_TRUE(contains(output, "the warning"));
}

TEST(Logger, should_write_an_error_at_once) {
    std::stringbuf output;
    Logger logger;
    logger.init(LogSeverity::Debug, {LogChannel::Global}, std::make_unique<std::ostream>(&output));

    logger.append("the error", LogChannel::Global, LogSeverity::Error);

    EXPECT_TRUE(contains(output, "the error"));
}
