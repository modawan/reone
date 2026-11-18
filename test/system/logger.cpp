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
