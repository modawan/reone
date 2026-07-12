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

#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/jrl.h"

using namespace reone;
using namespace reone::resource;

TEST(Jrl, should_parse_categories_and_entries) {
    // given

    auto entry1 = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {
            Gff::Field::newDword("ID", 10),
            Gff::Field::newWord("End", 0),
            Gff::Field::newCExoLocString("Text", -1, "first step"),
            Gff::Field::newFloat("XP_Percentage", 0.5f)});

    auto entry2 = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {
            Gff::Field::newDword("ID", 20),
            Gff::Field::newWord("End", 1),
            Gff::Field::newCExoLocString("Text", 123, ""),
            Gff::Field::newFloat("XP_Percentage", 1.0f)});

    auto category = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {
            Gff::Field::newCExoString("Tag", "test_plot"),
            Gff::Field::newCExoLocString("Name", 42, ""),
            Gff::Field::newDword("Priority", 2),
            Gff::Field::newInt("PlanetID", 3),
            Gff::Field::newInt("PlotIndex", 4),
            Gff::Field::newCExoString("Comment", "test comment"),
            Gff::Field::newList("EntryList", {entry1, entry2})});

    auto root = Gff(
        0xffffffff,
        std::vector<Gff::Field> {
            Gff::Field::newList("Categories", {category})});

    // when

    JRL jrl = parseJRL(root);

    // then

    ASSERT_EQ(1ll, jrl.categories.size());

    const JRL::Category &cat = jrl.categories[0];
    EXPECT_EQ("test_plot", cat.tag);
    EXPECT_EQ(42, cat.name.first);
    EXPECT_EQ(2u, cat.priority);
    EXPECT_EQ(3, cat.planetId);
    EXPECT_EQ(4, cat.plotIndex);
    EXPECT_EQ("test comment", cat.comment);

    ASSERT_EQ(2ll, cat.entries.size());

    EXPECT_EQ(10u, cat.entries[0].id);
    EXPECT_FALSE(cat.entries[0].end);
    EXPECT_EQ(-1, cat.entries[0].text.first);
    EXPECT_EQ("first step", cat.entries[0].text.second);
    EXPECT_EQ(0.5f, cat.entries[0].xpPercentage);

    EXPECT_EQ(20u, cat.entries[1].id);
    EXPECT_TRUE(cat.entries[1].end);
    EXPECT_EQ(123, cat.entries[1].text.first);
    EXPECT_EQ(1.0f, cat.entries[1].xpPercentage);
}

TEST(Jrl, should_parse_empty_journal) {
    auto root = Gff(0xffffffff, std::vector<Gff::Field> {});

    JRL jrl = parseJRL(root);

    EXPECT_TRUE(jrl.categories.empty());
}
