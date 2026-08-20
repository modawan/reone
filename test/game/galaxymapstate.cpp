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

#include "reone/game/galaxymapstate.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;

namespace {

/** A PARTYTABLE carrying a GalaxyMap struct built from the given fields. */
std::shared_ptr<Gff> newPartyTable(std::vector<Gff::Field> galaxyMapFields) {
    auto galaxyMap = std::make_shared<Gff>(0, std::move(galaxyMapFields));
    return std::make_shared<Gff>(
        0xffffffff,
        std::vector<Gff::Field> {
            Gff::Field::newStruct("GlxyMap", std::move(galaxyMap))});
}

/** Legacy planet mask: low half availability, high half selectability. */
uint32_t planetMask(std::initializer_list<int> available, std::initializer_list<int> selectable) {
    uint32_t mask = 0;
    for (int row : available) {
        mask |= 1u << row;
    }
    for (int row : selectable) {
        mask |= 1u << (row + 16);
    }
    return mask;
}

} // namespace

TEST(GalaxyMapState, k1_sizes_its_planet_list_from_content) {
    GalaxyMapState state;
    state.reset(GameID::KotOR, 8);

    EXPECT_EQ(8, state.rowCount());
}

TEST(GalaxyMapState, k1_keeps_rows_above_fifteen_when_content_defines_them) {
    GalaxyMapState state;
    state.reset(GameID::KotOR, 24);
    ASSERT_EQ(24, state.rowCount());

    state.setAvailable(16, true);
    state.setSelectable(23, true);

    EXPECT_TRUE(state.available(16));
    EXPECT_TRUE(state.selectable(23));
    EXPECT_TRUE(state.trySelectPlanet(16));
    EXPECT_EQ(16, state.selectedPlanet());
}

TEST(GalaxyMapState, k1_rejects_rows_content_does_not_define) {
    GalaxyMapState state;
    state.reset(GameID::KotOR, 3);

    state.setAvailable(3, true);
    state.setSelectable(3, true);

    EXPECT_FALSE(state.available(3));
    EXPECT_FALSE(state.selectable(3));
}

TEST(GalaxyMapState, k2_always_carries_sixteen_rows) {
    GalaxyMapState state;

    state.reset(GameID::TSL, 3);
    EXPECT_EQ(16, state.rowCount());

    state.reset(GameID::TSL, 40);
    EXPECT_EQ(16, state.rowCount());

    state.reset(GameID::TSL, -1);
    EXPECT_EQ(16, state.rowCount());
}

TEST(GalaxyMapState, k2_treats_zero_and_fifteen_as_valid_rows) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);

    state.setAvailable(0, true);
    state.setAvailable(15, true);
    state.setSelectable(0, true);
    state.setSelectable(15, true);

    EXPECT_TRUE(state.available(0));
    EXPECT_TRUE(state.available(15));
    EXPECT_TRUE(state.selectable(0));
    EXPECT_TRUE(state.selectable(15));
}

TEST(GalaxyMapState, k2_invalid_setters_do_nothing_and_invalid_getters_are_false) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);

    state.setAvailable(-1, true);
    state.setAvailable(16, true);
    state.setSelectable(-1, true);
    state.setSelectable(16, true);

    EXPECT_FALSE(state.available(-1));
    EXPECT_FALSE(state.available(16));
    EXPECT_FALSE(state.selectable(-1));
    EXPECT_FALSE(state.selectable(16));
}

TEST(GalaxyMapState, availability_and_selectability_are_independent) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);

    state.setAvailable(4, true);
    EXPECT_TRUE(state.available(4));
    EXPECT_FALSE(state.selectable(4));

    state.setSelectable(5, true);
    EXPECT_FALSE(state.available(5));
    EXPECT_TRUE(state.selectable(5));
}

TEST(GalaxyMapState, selection_follows_availability_alone) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    state.setAvailable(2, true);

    // Available but not selectable is still a selection the map can make.
    EXPECT_TRUE(state.trySelectPlanet(2));
    EXPECT_EQ(2, state.selectedPlanet());
}

TEST(GalaxyMapState, a_failed_selection_leaves_the_previous_one_in_place) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    state.setAvailable(3, true);
    state.setSelectable(7, true);
    ASSERT_TRUE(state.trySelectPlanet(3));

    // Unavailable, merely selectable, and out of range rows all fail alike.
    EXPECT_FALSE(state.trySelectPlanet(7));
    EXPECT_FALSE(state.trySelectPlanet(9));
    EXPECT_FALSE(state.trySelectPlanet(-1));
    EXPECT_FALSE(state.trySelectPlanet(16));

    EXPECT_EQ(3, state.selectedPlanet());
}

TEST(GalaxyMapState, nothing_is_selected_by_default) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);

    EXPECT_EQ(-1, state.selectedPlanet());
}

TEST(GalaxyMapState, a_reset_drops_planet_state_and_the_selection) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    state.setAvailable(1, true);
    state.setSelectable(1, true);
    ASSERT_TRUE(state.trySelectPlanet(1));

    state.reset(GameID::TSL, 0);

    EXPECT_FALSE(state.available(1));
    EXPECT_FALSE(state.selectable(1));
    EXPECT_EQ(-1, state.selectedPlanet());
}

TEST(GalaxyMapStateLoad, mask_bits_map_to_availability_and_selectability) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    auto ptGff = newPartyTable({
        Gff::Field::newDword("GlxyMapPlntMsk", planetMask({0, 5, 15}, {5, 15})),
        Gff::Field::newInt("GlxyMapSelPnt", 5)});

    state.loadFromPartyTable(*ptGff);

    EXPECT_TRUE(state.available(0));
    EXPECT_TRUE(state.available(5));
    EXPECT_TRUE(state.available(15));
    EXPECT_FALSE(state.available(1));

    EXPECT_FALSE(state.selectable(0));
    EXPECT_TRUE(state.selectable(5));
    EXPECT_TRUE(state.selectable(15));

    EXPECT_EQ(5, state.selectedPlanet());
}

TEST(GalaxyMapStateLoad, a_missing_galaxy_map_struct_leaves_safe_defaults) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    Gff ptGff(0xffffffff, std::vector<Gff::Field> {Gff::Field::newDword("PT_GOLD", 100)});

    state.loadFromPartyTable(ptGff);

    for (int row = 0; row < 16; ++row) {
        EXPECT_FALSE(state.available(row)) << "row " << row;
        EXPECT_FALSE(state.selectable(row)) << "row " << row;
    }
    EXPECT_EQ(-1, state.selectedPlanet());
}

TEST(GalaxyMapStateLoad, missing_fields_leave_safe_defaults) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    auto ptGff = newPartyTable({});

    state.loadFromPartyTable(*ptGff);

    for (int row = 0; row < 16; ++row) {
        EXPECT_FALSE(state.available(row)) << "row " << row;
        EXPECT_FALSE(state.selectable(row)) << "row " << row;
    }
    EXPECT_EQ(-1, state.selectedPlanet());
}

TEST(GalaxyMapStateLoad, a_selected_planet_outside_the_row_range_falls_back_to_none) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    auto ptGff = newPartyTable({
        Gff::Field::newDword("GlxyMapPlntMsk", planetMask({0}, {0})),
        Gff::Field::newInt("GlxyMapSelPnt", 40)});

    state.loadFromPartyTable(*ptGff);

    EXPECT_EQ(-1, state.selectedPlanet());
}

TEST(GalaxyMapStateLoad, k2_planet_count_bounds_the_flags_taken_from_the_mask) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    auto ptGff = newPartyTable({
        Gff::Field::newDword("GlxyMapNumPnts", 3),
        Gff::Field::newDword("GlxyMapPlntMsk", planetMask({1, 9}, {1, 9})),
        Gff::Field::newInt("GlxyMapSelPnt", 1)});

    state.loadFromPartyTable(*ptGff);

    EXPECT_TRUE(state.available(1));
    EXPECT_FALSE(state.available(9));
    EXPECT_FALSE(state.selectable(9));
    EXPECT_EQ(1, state.selectedPlanet());
}

TEST(GalaxyMapStateLoad, k1_takes_mask_flags_for_the_rows_content_defines) {
    GalaxyMapState state;
    state.reset(GameID::KotOR, 20);
    auto ptGff = newPartyTable({
        Gff::Field::newDword("GlxyMapPlntMsk", planetMask({0, 15}, {15})),
        Gff::Field::newInt("GlxyMapSelPnt", 15)});

    state.loadFromPartyTable(*ptGff);

    EXPECT_TRUE(state.available(0));
    EXPECT_TRUE(state.available(15));
    EXPECT_TRUE(state.selectable(15));
    // Rows beyond the legacy mask stay untouched, and remain settable content.
    EXPECT_FALSE(state.available(16));
    state.setAvailable(16, true);
    EXPECT_TRUE(state.available(16));
    EXPECT_EQ(15, state.selectedPlanet());
}

TEST(GalaxyMapState, a_clear_drops_planet_state_but_keeps_the_row_count) {
    GalaxyMapState state;
    state.reset(GameID::KotOR, 24);
    state.setAvailable(20, true);
    state.setSelectable(20, true);
    ASSERT_TRUE(state.trySelectPlanet(20));

    state.clear();

    EXPECT_EQ(24, state.rowCount());
    EXPECT_FALSE(state.available(20));
    EXPECT_FALSE(state.selectable(20));
    EXPECT_EQ(-1, state.selectedPlanet());
}

TEST(GalaxyMapStateLoad, a_full_planet_count_takes_the_whole_mask) {
    GalaxyMapState state;
    state.reset(GameID::TSL, 0);
    // Both titles write the count as a dword, and both write sixteen.
    auto ptGff = newPartyTable({
        Gff::Field::newDword("GlxyMapNumPnts", 16),
        Gff::Field::newDword("GlxyMapPlntMsk", planetMask({3, 15}, {15})),
        Gff::Field::newInt("GlxyMapSelPnt", 15)});

    state.loadFromPartyTable(*ptGff);

    EXPECT_TRUE(state.available(3));
    EXPECT_TRUE(state.available(15));
    EXPECT_FALSE(state.selectable(3));
    EXPECT_TRUE(state.selectable(15));
    EXPECT_EQ(15, state.selectedPlanet());
}
