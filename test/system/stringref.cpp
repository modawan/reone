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

#include "reone/system/stringref.h"

using namespace reone;

TEST(StringRef, ctor_default) {
    StringRef s;
    EXPECT_EQ(s, "");
}

TEST(StringRef, ctor_string) {
    std::string str("foo");
    StringRef s(str);
    EXPECT_EQ(s, "foo");
}

TEST(StringRef, ctor_ptr_len) {
    char arr[] = {'f', 'o', 'o'};
    StringRef s(&arr[0], 2);
    EXPECT_EQ(s, "fo");
}

TEST(StringRef, ctor_cstr) {
    StringRef s("foo");
    EXPECT_EQ(s, "foo");
}

TEST(StringRef, str) {
    StringRef s("foo");
    std::string str = s.str();
    EXPECT_EQ(s, "foo");
    EXPECT_EQ(str, "foo");
    EXPECT_EQ(s, str);
}

TEST(StringRef, substr) {
    StringRef s("foo");
    EXPECT_EQ(s.substr(0, 1), "f");
    EXPECT_EQ(s.substr(0, 2), "fo");
    EXPECT_EQ(s.substr(0, 3), "foo");
    EXPECT_EQ(s.substr(1, 3), "oo");
    EXPECT_EQ(s.substr(0, 0), "");
    EXPECT_EQ(s.substr(1, 1), "");
    EXPECT_EQ(s.substr(3, 3), "");
}

TEST(StringRef, lstrip) {
    EXPECT_EQ(StringRef(" \t\r\n foo").lstrip(), "foo");
    EXPECT_EQ(StringRef("ssSsfoo").lstrip("sS"), "foo");
    EXPECT_EQ(StringRef("foo").lstrip(), "foo");
    EXPECT_EQ(StringRef("").lstrip(), "");
}

TEST(StringRef, rstrip) {
    EXPECT_EQ(StringRef("foo \t\r\n ").rstrip(), "foo");
    EXPECT_EQ(StringRef("foossSs").rstrip("sS"), "foo");
    EXPECT_EQ(StringRef("foo").rstrip(), "foo");
    EXPECT_EQ(StringRef("").rstrip(), "");
}

TEST(StringRef, strip) {
    EXPECT_EQ(StringRef(" \t\r\n foo \t\r\n ").strip(), "foo");
    EXPECT_EQ(StringRef("ssSsfoossSs").strip("sS"), "foo");
    EXPECT_EQ(StringRef("foo").strip(), "foo");
    EXPECT_EQ(StringRef("").strip(), "");
}

TEST(StringRef, find) {
    StringRef s("Hello, world!");
    EXPECT_EQ(s.find("!"), 12);
    EXPECT_EQ(s.find("H"), 0);
    EXPECT_EQ(s.find("o"), 4);
    EXPECT_EQ(s.find("V"), StringRef::npos);
    EXPECT_EQ(s.find("Hello"), 0);
    EXPECT_EQ(s.find("world"), 7);
    EXPECT_EQ(s.find("Hello, world"), 0);
}

TEST(StringRef, rfind) {
    StringRef s("Hello, world!");
    EXPECT_EQ(s.rfind("!"), 12);
    EXPECT_EQ(s.rfind("H"), 0);
    EXPECT_EQ(s.rfind("o"), 8);
    EXPECT_EQ(s.rfind("V"), StringRef::npos);
    EXPECT_EQ(s.rfind("Hello"), 0);
    EXPECT_EQ(s.rfind("world"), 7);
    EXPECT_EQ(s.find("Hello, world"), 0);
}
