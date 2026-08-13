/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include <algorithm>

#include "reone/resource/2da.h"
#include "reone/resource/format/2dareader.h"
#include "reone/system/binarywriter.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stringbuilder.h"

using namespace reone;
using namespace reone::resource;

TEST(TwoDAReader, should_read_two_da) {
    // given

    auto input = StringBuilder()
                     .append("2DA V2.b")
                     .append("\x0a", 1)
                     .append("key\x09", 4)
                     .append("value\x09", 6)
                     .append("\x00", 1)
                     .append("\x02\x00\x00\x00", 4)
                     .append("\x30\x09\x31\x09", 4)
                     .append("\x00\x00", 2)
                     .append("\x07\x00", 2)
                     .append("\x07\x00", 2)
                     .append("\x07\x00", 2)
                     .append("\x0c\x00", 2)
                     .append("unique\x00", 7)
                     .append("same\x00", 5)
                     .string();

    auto stream = MemoryInputStream(input);
    auto reader = TwoDAReader(stream);

    // when

    reader.load();

    // then

    auto twoDa = reader.twoDA();
    EXPECT_EQ(twoDa->getColumnCount(), 2);
    EXPECT_EQ(twoDa->getRowCount(), 2);
    EXPECT_EQ(std::string("unique"), twoDa->getString(0, "key"));
    EXPECT_EQ(std::string("same"), twoDa->getString(0, "value"));
    EXPECT_EQ(std::string("same"), twoDa->getString(1, "key"));
    EXPECT_EQ(std::string("same"), twoDa->getString(1, "value"));
}

namespace {

/// A V2.b table in the ordinary tab-delimited form: two columns, two rows.
ByteBuffer tabDelimitedTwoDa() {
    auto s = StringBuilder()
                 .append("2DA V2.b")
                 .append("\x0a", 1)
                 .append("key\x09", 4)
                 .append("value\x09", 6)
                 .append("\x00", 1)                  // end of column labels
                 .append("\x02\x00\x00\x00", 4)      // row count
                 .append("\x30\x09\x31\x09", 4)      // row labels "0", "1"
                 .append("\x00\x00", 2)              // cell offsets
                 .append("\x03\x00", 2)
                 .append("\x06\x00", 2)
                 .append("\x09\x00", 2)
                 .append("\x0c\x00", 2)              // data size
                 .append("aa\x00", 3)
                 .append("bb\x00", 3)
                 .append("cc\x00", 3)
                 .append("dd\x00", 3)
                 .string();
    return ByteBuffer(s.begin(), s.end());
}

/// The same table with only the label separators rewritten to NUL, which is the
/// form K1 carries inside global.rim. Nothing else moves, so any difference in
/// how it reads is the delimiter and nothing else.
ByteBuffer nulDelimitedTwoDa() {
    auto buf = tabDelimitedTwoDa();
    // Column labels end at the first 0x00; row labels are the four bytes after
    // the row count that follows it.
    auto end = std::find(buf.begin(), buf.end(), '\0');
    for (auto it = buf.begin() + 9; it != end; ++it) {
        if (*it == '\x09') {
            *it = '\0';
        }
    }
    auto rows = end + 1 + 4;
    for (auto it = rows; it != rows + 4; ++it) {
        if (*it == '\x09') {
            *it = '\0';
        }
    }
    return buf;
}

std::shared_ptr<TwoDA> readTwoDa(const ByteBuffer &bytes) {
    auto stream = MemoryInputStream(const_cast<ByteBuffer &>(bytes));
    auto reader = TwoDAReader(stream);
    reader.load();
    return reader.twoDA();
}

} // namespace

TEST(TwoDAReaderDelimiters, reads_the_tab_delimited_form) {
    auto twoDa = readTwoDa(tabDelimitedTwoDa());

    ASSERT_TRUE(twoDa);
    EXPECT_EQ((std::vector<std::string> {"key", "value"}), twoDa->columns());
    EXPECT_EQ(2, twoDa->getRowCount());
    EXPECT_EQ("aa", twoDa->getString(0, "key"));
    EXPECT_EQ("bb", twoDa->getString(0, "value"));
    EXPECT_EQ("cc", twoDa->getString(1, "key"));
    EXPECT_EQ("dd", twoDa->getString(1, "value"));
}

TEST(TwoDAReaderDelimiters, reads_the_nul_delimited_form_identically) {
    auto tab = readTwoDa(tabDelimitedTwoDa());
    auto nul = readTwoDa(nulDelimitedTwoDa());

    ASSERT_TRUE(nul);
    EXPECT_EQ(tab->columns(), nul->columns());
    EXPECT_EQ(tab->getRowCount(), nul->getRowCount());
    for (int row = 0; row < tab->getRowCount(); ++row) {
        for (const auto &column : tab->columns()) {
            EXPECT_EQ(tab->getString(row, column), nul->getString(row, column))
                << "row " << row << " column " << column;
        }
    }
}

TEST(TwoDAReaderDelimiters, the_substitution_does_not_move_the_row_count) {
    // The two inputs are the same length and differ only in separator bytes, so
    // a reader that mistook a label separator for the end of the section would
    // read the row count from the wrong place and blow up rather than differ
    // quietly.
    auto tab = tabDelimitedTwoDa();
    auto nul = nulDelimitedTwoDa();
    ASSERT_EQ(tab.size(), nul.size());

    std::size_t differing = 0;
    for (std::size_t i = 0; i < tab.size(); ++i) {
        if (tab[i] != nul[i]) {
            ++differing;
        }
    }
    EXPECT_EQ(4u, differing) << "only the two column and two row separators change";
    EXPECT_EQ(2, readTwoDa(nul)->getRowCount());
}

TEST(TwoDAReaderDelimiters, a_truncated_table_still_fails_rather_than_over_allocating) {
    auto buf = nulDelimitedTwoDa();
    buf.resize(20);

    EXPECT_ANY_THROW(readTwoDa(buf));
}
