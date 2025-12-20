/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/system/stringutil.h"

using namespace reone;

TEST(stringutil, lstrip) {
    EXPECT_EQ(string_lstrip(" \t\r\n foo"), "foo");
    EXPECT_EQ(string_lstrip("ssSsfoo", "Ss"), "foo");
    EXPECT_EQ(string_lstrip("foo"), "foo");
    EXPECT_EQ(string_lstrip(""), "");
}

TEST(stringutil, rstrip) {
    EXPECT_EQ(string_rstrip("foo \t\r\n "), "foo");
    EXPECT_EQ(string_rstrip("foossSs", "Ss"), "foo");
    EXPECT_EQ(string_rstrip("foo"), "foo");
    EXPECT_EQ(string_rstrip(""), "");
}

TEST(stringutil, strip) {
    EXPECT_EQ(string_strip(" \t\r\n foo \t\r\n "), "foo");
    EXPECT_EQ(string_strip("ssSsfoossSs", "sS"), "foo");
    EXPECT_EQ(string_strip("foo"), "foo");
    EXPECT_EQ(string_strip(""), "");
}
