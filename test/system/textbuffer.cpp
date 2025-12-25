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

#include "reone/system/textbuffer.h"

using namespace reone;

TEST(TextBuffer, write_char) {
    TextBuffer buf;
    buf.write('H');
    buf.write('e');
    buf.write('l');
    buf.write('l');
    buf.write('o');
    EXPECT_EQ(buf.str(), "Hello");
}

TEST(TextBuffer, write_string) {
    TextBuffer buf;
    buf.write("Hello");
    EXPECT_EQ(buf.str(), "Hello");
    buf.write("world");
    EXPECT_EQ(buf.str(), "Helloworld");
    buf.seekSet(5);
    buf.write(", ");
    EXPECT_EQ(buf.str(), "Hello, world");
    buf.write("");
    EXPECT_EQ(buf.str(), "Hello, world");
}

TEST(TextBuffer, tell) {
    TextBuffer buf;
    EXPECT_EQ(buf.tell(), 0);
    buf.write('H');
    EXPECT_EQ(buf.tell(), 1);
    buf.write("ello");
    EXPECT_EQ(buf.tell(), 5);
}

TEST(TextBuffer, seek_cur) {
    TextBuffer buf;
    buf.write("Hello");
    EXPECT_EQ(buf.tell(), 5);
    buf.seekCur(-1);
    EXPECT_EQ(buf.tell(), 4);
    buf.seekCur(2);
    EXPECT_EQ(buf.tell(), 5);
    buf.seekCur(-6);
    EXPECT_EQ(buf.tell(), 0);
}

TEST(TextBuffer, seek_set) {
    TextBuffer buf;
    buf.write("Hello");
    EXPECT_EQ(buf.tell(), 5);
    buf.seekSet(0);
    EXPECT_EQ(buf.tell(), 0);
    buf.seekSet(5);
    EXPECT_EQ(buf.tell(), 5);
    buf.seekSet(6);
    EXPECT_EQ(buf.tell(), 5);
    buf.seekSet(-1);
    EXPECT_EQ(buf.tell(), 0);
}

TEST(TextBuffer, seek_end) {
    TextBuffer buf;
    buf.write("Hello");
    EXPECT_EQ(buf.tell(), 5);
    buf.seekEnd(-1);
    EXPECT_EQ(buf.tell(), 4);
    buf.seekEnd(0);
    EXPECT_EQ(buf.tell(), 5);
    buf.seekEnd(-6);
    EXPECT_EQ(buf.tell(), 0);
    buf.seekEnd(1);
    EXPECT_EQ(buf.tell(), 5);
}

TEST(TextBuffer, read) {
    TextBuffer buf;
    buf.write("Hello");
    buf.seekSet(0);
    EXPECT_EQ(buf.tell(), 0);
    EXPECT_EQ(buf.read(), 'H');
    EXPECT_EQ(buf.read(), 'e');
    EXPECT_EQ(buf.read(), 'l');
    EXPECT_EQ(buf.read(), 'l');
    EXPECT_EQ(buf.read(), 'o');
}

TEST(TextBuffer, peek) {
    TextBuffer buf;
    buf.write("Hello");
    buf.seekSet(0);
    EXPECT_EQ(buf.tell(), 0);
    EXPECT_EQ(buf.peek(), 'H');
    buf.seekCur(1);
    EXPECT_EQ(buf.peek(), 'e');
    buf.seekCur(1);
    EXPECT_EQ(buf.peek(), 'l');
    buf.seekCur(1);
    EXPECT_EQ(buf.peek(), 'l');
    buf.seekCur(1);
    EXPECT_EQ(buf.peek(), 'o');
    EXPECT_EQ(buf.tell(), 4);
}

TEST(TextBuffer, erase) {
    TextBuffer buf;
    buf.write("Hello");
    EXPECT_EQ(buf.tell(), 5);
    buf.erase();
    EXPECT_EQ(buf.str(), "Hell");
    buf.seekSet(1);
    buf.erase();
    EXPECT_EQ(buf.str(), "ell");

    // Erase removes a character before the cursor. It does nothing when the
    // cursor is at 0.
    EXPECT_EQ(buf.tell(), 0);
    buf.erase();
    EXPECT_EQ(buf.str(), "ell");
}

TEST(TextBuffer, clear) {
    TextBuffer buf;
    buf.write("Hello");
    EXPECT_EQ(buf.tell(), 5);
    EXPECT_EQ(buf.str(), "Hello");

    buf.clear();

    EXPECT_EQ(buf.tell(), 0);
    EXPECT_EQ(buf.str(), "");

    buf.seekEnd(0);
    EXPECT_EQ(buf.tell(), 0);
}

TEST(TextBuffer, search) {
    TextBuffer buf;
    buf.write("Hello");
    buf.seekSet(0);
    EXPECT_TRUE(buf.search("He"));
    EXPECT_EQ(buf.tell(), 0);

    EXPECT_FALSE(buf.search("Hello, world"));
    EXPECT_EQ(buf.tell(), 5);

    buf.seekSet(0);
    EXPECT_TRUE(buf.search("llo"));
    EXPECT_EQ(buf.tell(), 2);

    EXPECT_TRUE(buf.search("o"));
    EXPECT_EQ(buf.tell(), 4);
}

TEST(TextBuffer, rsearch) {
    TextBuffer buf;
    buf.write("Hello");
    EXPECT_EQ(buf.tell(), 5);

    EXPECT_TRUE(buf.rsearch("o"));
    EXPECT_EQ(buf.tell(), 4);

    // Search spans backwards from the character before the cursor.
    // Therefore "o" is not included.
    EXPECT_FALSE(buf.rsearch("llo"));
    EXPECT_EQ(buf.tell(), 0);

    buf.seekEnd(0);
    EXPECT_TRUE(buf.rsearch("Hello"));
    EXPECT_EQ(buf.tell(), 0);

    buf.seekEnd(0);
    EXPECT_TRUE(buf.rsearch("He"));
    EXPECT_EQ(buf.tell(), 0);
}

TEST(TextBuffer, readline) {
    TextBuffer buf;
    buf.write("Hello");
    buf.seekSet(0);
    EXPECT_EQ(buf.readline(), "Hello");
    EXPECT_EQ(buf.tell(), 5);
    EXPECT_EQ(buf.readline(), "");
    EXPECT_EQ(buf.tell(), 5);
    buf.clear();

    buf.write("\nHello");
    buf.seekSet(0);
    EXPECT_EQ(buf.readline(), "\n");
    EXPECT_EQ(buf.tell(), 1);
    EXPECT_EQ(buf.readline(), "Hello");
    EXPECT_EQ(buf.tell(), 6);
    buf.clear();

    buf.write("Hello\nworld");
    buf.seekSet(0);
    EXPECT_EQ(buf.readline(), "Hello\n");
    EXPECT_EQ(buf.tell(), 6);
    EXPECT_EQ(buf.readline(), "world");
    EXPECT_EQ(buf.tell(), 11);
    buf.clear();

    buf.write("Hello");
    buf.seekSet(1);
    EXPECT_EQ(buf.readline(), "ello");
    EXPECT_EQ(buf.tell(), 5);
}

TEST(TextBuffer, readlineReverse) {
    TextBuffer buf;
    buf.write("Hello");
    EXPECT_EQ(buf.readlineReverse(), "Hello");
    EXPECT_EQ(buf.tell(), 0);
    EXPECT_EQ(buf.readlineReverse(), "");
    EXPECT_EQ(buf.tell(), 0);
    buf.clear();

    buf.write("Hello\n");
    EXPECT_EQ(buf.readlineReverse(), "Hello\n");
    EXPECT_EQ(buf.tell(), 0);
    buf.clear();

    buf.write("\nHello\n");
    EXPECT_EQ(buf.readlineReverse(), "Hello\n");
    EXPECT_EQ(buf.tell(), 1);
    EXPECT_EQ(buf.readlineReverse(), "\n");
    EXPECT_EQ(buf.tell(), 0);
    buf.clear();

    buf.write("Hello\nworld");
    EXPECT_EQ(buf.readlineReverse(), "world");
    EXPECT_EQ(buf.tell(), 6);
    EXPECT_EQ(buf.readlineReverse(), "Hello\n");
    EXPECT_EQ(buf.tell(), 0);
    buf.clear();
}
