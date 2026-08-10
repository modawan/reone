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

/**
 * Row labels.
 *
 * A label is a key in its own right, distinct from any column: some tables are
 * addressed by it and its value is not always the row ordinal.
 */

#include <gtest/gtest.h>

#include "reone/resource/2da.h"
#include "reone/resource/format/2dareader.h"
#include "reone/resource/format/2dawriter.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::resource;

namespace {

std::shared_ptr<TwoDA> labelledTable() {
    return TwoDA::Builder()
        .columns({"modulename", "includeinsave"})
        .row("001ebo", {"001ebo", "1"})
        .row("007ebo", {"007ebo", "0"})
        .row("101per", {"101per", "1"})
        .build();
}

} // namespace

TEST(TwoDARowLabel, finds_a_row_by_its_label) {
    auto table = labelledTable();

    EXPECT_EQ(0, table->indexByLabel("001ebo"));
    EXPECT_EQ(1, table->indexByLabel("007ebo"));
    EXPECT_EQ(2, table->indexByLabel("101per"));
}

TEST(TwoDARowLabel, reports_a_missing_label_rather_than_guessing_a_row) {
    auto table = labelledTable();

    EXPECT_EQ(-1, table->indexByLabel("235tel"));
    EXPECT_EQ(-1, table->indexByLabel(""));
    EXPECT_EQ(-1, table->indexByLabel("001ebo_")) << "a longer key is not a prefix match";
    EXPECT_EQ(-1, table->indexByLabel("001eb")) << "a shorter key is not a prefix match";
}

TEST(TwoDARowLabel, folds_ascii_case_when_matching_a_label) {
    auto table = TwoDA::Builder()
                     .columns({"modulename", "includeinsave"})
                     .row("003ebo", {"003ebo", "1"})
                     .row("007ebo", {"007ebo", "0"})
                     .build();

    EXPECT_EQ(0, table->indexByLabel("003EBO"));
    EXPECT_EQ(0, table->indexByLabel("003eBo"));
    EXPECT_EQ(0, table->indexByLabel("003ebo"));
    EXPECT_EQ(1, table->indexByLabel("007EBO"));
    EXPECT_EQ(-1, table->indexByLabel("004EBO"));

    // Folding is ASCII only: a byte outside A-Z is left alone, so a lookup
    // cannot start depending on the environment's locale.
    auto accented = TwoDA::Builder().columns({"a"}).row("\xc3\x89", {"x"}).build();
    EXPECT_EQ(0, accented->indexByLabel("\xc3\x89"));
    EXPECT_EQ(-1, accented->indexByLabel("\xc3\xa9"));
}

TEST(TwoDARowLabel, is_independent_of_any_column) {
    // A table can carry a column that happens to repeat the label. The two are
    // still separate keys, and a label lookup must not become a cell lookup.
    auto table = TwoDA::Builder()
                     .columns({"label"})
                     .row("row_key", {"cell_value"})
                     .build();

    EXPECT_EQ(0, table->indexByLabel("row_key"));
    EXPECT_EQ(-1, table->indexByLabel("cell_value"));
    EXPECT_EQ(0, table->indexByCellValue("label", "cell_value"));
    EXPECT_EQ(-1, table->indexByCellValue("label", "row_key"));
}

TEST(TwoDARowLabel, labels_rows_by_ordinal_when_the_builder_is_given_no_label) {
    auto table = TwoDA::Builder()
                     .columns({"a"})
                     .row({"first"})
                     .row({"second"})
                     .build();

    EXPECT_EQ(0, table->indexByLabel("0"));
    EXPECT_EQ(1, table->indexByLabel("1"));
}

TEST(TwoDARowLabel, survives_a_write_and_read_round_trip) {
    auto original = labelledTable();

    ByteBuffer buffer;
    {
        MemoryOutputStream out(buffer);
        TwoDAWriter writer(*original);
        writer.save(out);
    }

    MemoryInputStream in(buffer);
    TwoDAReader reader(in);
    reader.load();
    auto reloaded = reader.twoDA();
    ASSERT_TRUE(reloaded);

    ASSERT_EQ(3, reloaded->getRowCount());
    EXPECT_EQ("001ebo", reloaded->rows()[0].label);
    EXPECT_EQ("007ebo", reloaded->rows()[1].label);
    EXPECT_EQ("101per", reloaded->rows()[2].label);

    // The row a label selects must still hold that row's cells.
    int row = reloaded->indexByLabel("007ebo");
    ASSERT_NE(-1, row);
    EXPECT_EQ(0, reloaded->getInt(row, "includeinsave"));
    EXPECT_EQ(1, reloaded->getInt(reloaded->indexByLabel("001ebo"), "includeinsave"));
}
