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

#include <algorithm>
#include <random>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/game/partyselectionstate.h"

using namespace reone::game;
using namespace testing;

namespace {

// KotOR's companions: Bastila 0, Canderous 1, Carth 2, HK-47 3, Jolee 4,
// Juhani 5, Mission 6, T3-M4 7, Zaalbar 8.
constexpr int kNpcCount = 9;
constexpr int kNone = -1;

std::vector<bool> allUsable() {
    return std::vector<bool>(kNpcCount, true);
}

PartySelectionState opened(const std::vector<int> &companions, int force1 = kNone, int force2 = kNone,
                           std::vector<bool> usable = allUsable()) {
    PartySelectionState state;
    state.open(std::move(usable), companions, force1, force2);
    return state;
}

} // namespace

// Davik's estate: Canderous is forced in while Carth and T3-M4 travel along.
TEST(PartySelectionState, forced_companion_takes_the_place_of_the_last_current_one) {
    auto state = opened({2, 7}, 1);

    EXPECT_THAT(state.selection(), ElementsAre(1, 2));
    EXPECT_FALSE(state.isSelected(7));
    EXPECT_EQ(0, state.availableSlots());
}

TEST(PartySelectionState, forced_companion_cannot_be_removed) {
    auto state = opened({2, 7}, 1);

    EXPECT_FALSE(state.remove(1));

    EXPECT_THAT(state.selection(), ElementsAre(1, 2));
    EXPECT_EQ(0, state.availableSlots());
}

TEST(PartySelectionState, current_companion_can_make_room_for_another) {
    auto state = opened({2, 7}, 1);

    EXPECT_TRUE(state.remove(2));
    EXPECT_THAT(state.selection(), ElementsAre(1));
    EXPECT_EQ(1, state.availableSlots());

    EXPECT_TRUE(state.add(7));
    EXPECT_THAT(state.selection(), ElementsAre(1, 7));
    EXPECT_EQ(0, state.availableSlots());
}

TEST(PartySelectionState, full_selection_takes_no_one_else) {
    auto state = opened({2, 7}, 1);

    EXPECT_FALSE(state.add(7));

    EXPECT_THAT(state.selection(), ElementsAre(1, 2));
}

// Trask on the Endar Spire.
TEST(PartySelectionState, forced_companion_into_a_party_of_one) {
    auto state = opened({}, 0);

    EXPECT_THAT(state.selection(), ElementsAre(0));
    EXPECT_EQ(1, state.availableSlots());
}

TEST(PartySelectionState, two_forced_companions_fill_the_party) {
    auto state = opened({2}, 6, 2);

    EXPECT_THAT(state.selection(), ElementsAre(2, 6));
    EXPECT_EQ(0, state.availableSlots());
    EXPECT_FALSE(state.remove(2));
    EXPECT_FALSE(state.remove(6));
    EXPECT_THAT(state.selection(), ElementsAre(2, 6));
}

TEST(PartySelectionState, two_forced_companions_displace_both_current_ones) {
    auto state = opened({2, 7}, 1, 2);

    EXPECT_THAT(state.selection(), ElementsAre(1, 2));
    EXPECT_FALSE(state.isSelected(7));
    EXPECT_EQ(0, state.availableSlots());
}

TEST(PartySelectionState, the_same_companion_forced_twice_counts_once) {
    auto state = opened({}, 2, 2);

    EXPECT_THAT(state.selection(), ElementsAre(2));
    EXPECT_THAT(state.forced(), ElementsAre(2));
    EXPECT_EQ(1, state.availableSlots());
}

TEST(PartySelectionState, out_of_range_forced_value_is_ignored) {
    auto state = opened({2, 7}, 9);

    EXPECT_THAT(state.selection(), ElementsAre(2, 7));
    EXPECT_THAT(state.forced(), IsEmpty());
    EXPECT_EQ(0, state.availableSlots());
}

TEST(PartySelectionState, unavailable_forced_companion_is_ignored) {
    auto usable = allUsable();
    usable[4] = false;
    auto state = opened({2}, 4, kNone, usable);

    EXPECT_THAT(state.selection(), ElementsAre(2));
    EXPECT_THAT(state.forced(), IsEmpty());
    EXPECT_EQ(1, state.availableSlots());
}

TEST(PartySelectionState, unforced_selection_opens_on_the_current_companions) {
    auto state = opened({2, 7});

    EXPECT_THAT(state.selection(), ElementsAre(2, 7));
    EXPECT_EQ(0, state.availableSlots());
    EXPECT_TRUE(state.remove(7));
    EXPECT_EQ(1, state.availableSlots());
}

TEST(PartySelectionState, unavailable_companion_cannot_be_added) {
    auto usable = allUsable();
    usable[5] = false;
    auto state = opened({}, kNone, kNone, usable);

    EXPECT_FALSE(state.add(5));
    EXPECT_THAT(state.selection(), IsEmpty());
}

TEST(PartySelectionState, no_clicks_ever_lose_a_forced_companion_or_overfill) {
    std::mt19937 random(434);
    std::uniform_int_distribution<int> npcs(-1, kNpcCount);
    std::uniform_int_distribution<int> coin(0, 1);
    for (int round = 0; round < 200; ++round) {
        std::vector<int> companions;
        for (int npc = 0; npc < kNpcCount && companions.size() < 2; ++npc) {
            if (coin(random) && coin(random)) {
                companions.push_back(npc);
            }
        }
        auto usable = allUsable();
        for (int npc = 0; npc < kNpcCount; ++npc) {
            usable[npc] = coin(random) || coin(random);
        }
        const int force1 = npcs(random);
        const int force2 = npcs(random);
        auto state = opened(companions, force1, force2, usable);
        for (int forced : {force1, force2}) {
            if (forced >= 0 && forced < kNpcCount && usable[forced]) {
                ASSERT_TRUE(state.isSelected(forced));
            }
        }
        for (int click = 0; click < 30; ++click) {
            const int npc = npcs(random);
            if (coin(random)) {
                state.add(npc);
            } else {
                state.remove(npc);
            }
            const auto selection = state.selection();
            ASSERT_LE(selection.size(), 2u);
            ASSERT_EQ(2 - static_cast<int>(selection.size()), state.availableSlots());
            for (int forced : {force1, force2}) {
                if (forced >= 0 && forced < kNpcCount && usable[forced]) {
                    ASSERT_TRUE(state.isSelected(forced));
                }
            }
            for (int picked : selection) {
                ASSERT_TRUE(usable[picked]);
            }
        }
    }
}

TEST(PartySelectionState, forcing_in_a_newcomer_is_known) {
    EXPECT_TRUE(opened({2, 7}, 1).forcesNewcomer());
    EXPECT_TRUE(opened({2}, 6, 2).forcesNewcomer());
    EXPECT_FALSE(opened({0, 2}, 2, 0).forcesNewcomer());
    EXPECT_FALSE(opened({2, 7}).forcesNewcomer());
    EXPECT_FALSE(opened({2, 7}, 9).forcesNewcomer());
}

TEST(PartySelectionState, current_companion_that_cannot_be_picked_opens_unselected) {
    auto usable = allUsable();
    usable[7] = false;
    auto state = opened({2, 7}, kNone, kNone, usable);

    EXPECT_THAT(state.selection(), ElementsAre(2));
    EXPECT_EQ(1, state.availableSlots());
    EXPECT_FALSE(state.add(7));
}

TEST(PartySelectionState, companion_that_cannot_leave_stays_picked_after_a_forced_one) {
    PartySelectionState state;
    state.open(allUsable(), {2}, 1, kNone, {7});

    EXPECT_THAT(state.selection(), ElementsAre(1, 7));
    EXPECT_THAT(state.request(), ElementsAre(1));
    EXPECT_FALSE(state.isSelected(2));
    EXPECT_FALSE(state.remove(7));
    EXPECT_TRUE(state.isLocked(7));
    EXPECT_EQ(0, state.availableSlots());
    EXPECT_TRUE(state.forcesNewcomer());
}

TEST(PartySelectionState, place_held_off_the_screen_counts_against_the_slots) {
    PartySelectionState state;
    state.open(allUsable(), {}, 1, kNone, {}, 1);

    EXPECT_THAT(state.selection(), ElementsAre(1));
    EXPECT_EQ(0, state.availableSlots());
    EXPECT_FALSE(state.add(4));
}

TEST(PartySelectionState, forced_companions_come_before_one_that_cannot_leave) {
    PartySelectionState state;
    state.open(allUsable(), {}, 1, 2, {7});

    EXPECT_THAT(state.selection(), ElementsAre(1, 2));
    EXPECT_FALSE(state.isSelected(7));
    EXPECT_EQ(0, state.availableSlots());
}

TEST(PartySelectionState, forced_companions_come_before_a_place_held_off_the_screen) {
    PartySelectionState state;
    state.open(allUsable(), {}, 1, 2, {}, 1);

    EXPECT_THAT(state.selection(), ElementsAre(1, 2));
    EXPECT_EQ(0, state.availableSlots());
}

TEST(PartySelectionState, never_more_than_two_picked_whatever_the_party) {
    std::mt19937 random(4340);
    std::uniform_int_distribution<int> npcs(-1, kNpcCount);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> places(0, 3);
    for (int round = 0; round < 300; ++round) {
        std::vector<int> companions;
        std::vector<int> fixed;
        for (int npc = 0; npc < kNpcCount; ++npc) {
            if (coin(random) && coin(random)) {
                if (coin(random)) {
                    companions.push_back(npc);
                } else {
                    fixed.push_back(npc);
                }
            }
        }
        auto usable = allUsable();
        for (int npc = 0; npc < kNpcCount; ++npc) {
            usable[npc] = coin(random) || coin(random);
        }
        const int force1 = npcs(random);
        const int force2 = npcs(random);
        PartySelectionState state;
        state.open(usable, companions, force1, force2, fixed, places(random));
        for (int click = 0; click < 30; ++click) {
            const int npc = npcs(random);
            if (coin(random)) {
                state.add(npc);
            } else {
                state.remove(npc);
            }
            const auto selection = state.selection();
            ASSERT_LE(selection.size(), 2u);
            ASSERT_LE(static_cast<int>(selection.size()) + state.availableSlots(), 2);
            for (int forced : {force1, force2}) {
                if (forced >= 0 && forced < kNpcCount && usable[forced]) {
                    ASSERT_TRUE(state.isSelected(forced));
                }
            }
            for (int requested : state.request()) {
                ASSERT_TRUE(std::find(fixed.begin(), fixed.end(), requested) == fixed.end());
            }
        }
    }
}
