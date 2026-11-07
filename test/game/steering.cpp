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

#include "reone/game/steering.h"

using namespace reone::game;

// W runs along (-sin f, cos f): facing 0 runs toward +y.
TEST(SteerToward, should_run_straight_when_facing_the_target) {
    auto keys = steerToward({0.0f, 0.0f}, {0.0f, 10.0f}, 0.0f);
    EXPECT_FALSE(keys.arrived);
    EXPECT_EQ(keys.turn, 0);
    EXPECT_TRUE(keys.forward);
}

TEST(SteerToward, should_turn_the_shorter_way) {
    // Target toward -x needs facing +pi/2: counter-clockwise, A.
    auto left = steerToward({0.0f, 0.0f}, {-10.0f, 0.0f}, 0.0f);
    EXPECT_EQ(left.turn, 1);
    EXPECT_FALSE(left.forward);
    // Target toward +x needs facing -pi/2: clockwise, D.
    auto right = steerToward({0.0f, 0.0f}, {10.0f, 0.0f}, 0.0f);
    EXPECT_EQ(right.turn, -1);
    EXPECT_FALSE(right.forward);
    // Facing almost all the way round: still the short way, across +-pi.
    auto across = steerToward({0.0f, 0.0f}, {0.0f, -10.0f}, 3.0f);
    EXPECT_EQ(across.turn, 1);
}

TEST(SteerToward, should_correct_a_slight_error_while_running) {
    auto keys = steerToward({0.0f, 0.0f}, {0.0f, 10.0f}, 0.3f);
    EXPECT_EQ(keys.turn, -1);
    EXPECT_TRUE(keys.forward);
}

TEST(SteerToward, should_stop_within_the_arrival_distance) {
    auto keys = steerToward({0.0f, 0.0f}, {0.5f, 0.5f}, 2.0f);
    EXPECT_TRUE(keys.arrived);
    EXPECT_EQ(keys.turn, 0);
    EXPECT_FALSE(keys.forward);
}
