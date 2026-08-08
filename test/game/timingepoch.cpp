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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/resource/types.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

void reone::game::TestGameModule::raiseTimingDiscontinuity(Game &game) {
    game._timingDiscontinuity = true;
}

TEST(TimingEpoch, should_report_no_discontinuity_on_a_game_that_has_not_loaded) {
    // given
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    // when / then
    // Ordinary frames must measure the whole interval between them, so nothing
    // may claim a discontinuity that did not happen.
    EXPECT_FALSE(game.consumeTimingDiscontinuity());
    EXPECT_FALSE(game.consumeTimingDiscontinuity());
}

TEST(TimingEpoch, should_report_a_raised_discontinuity_exactly_once) {
    // given
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    TestGameModule::raiseTimingDiscontinuity(game);

    // when
    bool first = game.consumeTimingDiscontinuity();
    bool second = game.consumeTimingDiscontinuity();

    // then
    // One load rebases one frame. If the report stuck, every later frame would
    // also start from zero and the world would stop advancing altogether.
    EXPECT_TRUE(first);
    EXPECT_FALSE(second);
}

TEST(TimingEpoch, should_report_the_latest_discontinuity_after_an_earlier_one_was_consumed) {
    // given
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    // when / then
    // Loads recur - warping, transitions, loading a save - and each one owes
    // the next frame an epoch of its own.
    TestGameModule::raiseTimingDiscontinuity(game);
    EXPECT_TRUE(game.consumeTimingDiscontinuity());
    EXPECT_FALSE(game.consumeTimingDiscontinuity());

    TestGameModule::raiseTimingDiscontinuity(game);
    EXPECT_TRUE(game.consumeTimingDiscontinuity());
    EXPECT_FALSE(game.consumeTimingDiscontinuity());
}
