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

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

/**
 * A game plus the routine table it is bound to, so a galaxy map routine can be
 * called the way a compiled script calls it.
 */
class RoutineHarness : boost::noncopyable {
public:
    RoutineHarness(GameID gameId, int contentRowCount) :
        _game(gameId, "", testEngine().options(), testEngine().services(), _console),
        _routines(gameId, &_game, &testEngine().services()) {

        _routines.init();
        _game.party().galaxyMap().reset(gameId, contentRowCount);
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        return routine.invoke(args, execution);
    }

    GalaxyMapState &galaxyMap() { return _game.party().galaxyMap(); }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

int callGetInt(RoutineHarness &harness, const std::string &name, int planet) {
    return harness.call(name, {Variable::ofInt(planet)}).intValue;
}

void callSet(RoutineHarness &harness, const std::string &name, int planet, int value) {
    harness.call(name, {Variable::ofInt(planet), Variable::ofInt(value)});
}

class GalaxyMapRoutines : public ::testing::TestWithParam<GameID> {};

} // namespace

TEST_P(GalaxyMapRoutines, set_and_get_planet_available_round_trip) {
    RoutineHarness harness(GetParam(), 16);

    callSet(harness, "SetPlanetAvailable", 3, 1);

    EXPECT_EQ(1, callGetInt(harness, "GetPlanetAvailable", 3));
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetAvailable", 4));

    callSet(harness, "SetPlanetAvailable", 3, 0);
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetAvailable", 3));
}

TEST_P(GalaxyMapRoutines, set_and_get_planet_selectable_round_trip) {
    RoutineHarness harness(GetParam(), 16);

    callSet(harness, "SetPlanetSelectable", 3, 1);

    EXPECT_EQ(1, callGetInt(harness, "GetPlanetSelectable", 3));
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetSelectable", 4));

    callSet(harness, "SetPlanetSelectable", 3, 0);
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetSelectable", 3));
}

TEST_P(GalaxyMapRoutines, boolean_setters_canonicalize_any_non_zero_value) {
    RoutineHarness harness(GetParam(), 16);

    callSet(harness, "SetPlanetAvailable", 2, 7);
    callSet(harness, "SetPlanetSelectable", 2, -3);

    EXPECT_EQ(1, callGetInt(harness, "GetPlanetAvailable", 2));
    EXPECT_EQ(1, callGetInt(harness, "GetPlanetSelectable", 2));
}

TEST_P(GalaxyMapRoutines, availability_and_selectability_are_set_independently) {
    RoutineHarness harness(GetParam(), 16);

    callSet(harness, "SetPlanetAvailable", 6, 1);

    EXPECT_EQ(1, callGetInt(harness, "GetPlanetAvailable", 6));
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetSelectable", 6));
}

TEST_P(GalaxyMapRoutines, get_selected_planet_is_none_until_one_is_selected) {
    RoutineHarness harness(GetParam(), 16);

    EXPECT_EQ(-1, harness.call("GetSelectedPlanet", {}).intValue);

    harness.galaxyMap().setAvailable(8, true);
    ASSERT_TRUE(harness.galaxyMap().trySelectPlanet(8));

    EXPECT_EQ(8, harness.call("GetSelectedPlanet", {}).intValue);
}

TEST_P(GalaxyMapRoutines, rows_zero_and_fifteen_are_valid) {
    RoutineHarness harness(GetParam(), 16);

    callSet(harness, "SetPlanetAvailable", 0, 1);
    callSet(harness, "SetPlanetAvailable", 15, 1);

    EXPECT_EQ(1, callGetInt(harness, "GetPlanetAvailable", 0));
    EXPECT_EQ(1, callGetInt(harness, "GetPlanetAvailable", 15));
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    GalaxyMapRoutines,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });

TEST(GalaxyMapRoutinesTSL, rows_outside_zero_to_fifteen_are_rejected) {
    RoutineHarness harness(GameID::TSL, 16);

    callSet(harness, "SetPlanetAvailable", -1, 1);
    callSet(harness, "SetPlanetAvailable", 16, 1);
    callSet(harness, "SetPlanetSelectable", -1, 1);
    callSet(harness, "SetPlanetSelectable", 16, 1);

    EXPECT_EQ(0, callGetInt(harness, "GetPlanetAvailable", -1));
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetAvailable", 16));
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetSelectable", -1));
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetSelectable", 16));
}

TEST(GalaxyMapRoutinesKotOR, rows_above_fifteen_work_when_content_defines_them) {
    RoutineHarness harness(GameID::KotOR, 24);

    callSet(harness, "SetPlanetAvailable", 20, 1);
    callSet(harness, "SetPlanetSelectable", 20, 1);

    EXPECT_EQ(1, callGetInt(harness, "GetPlanetAvailable", 20));
    EXPECT_EQ(1, callGetInt(harness, "GetPlanetSelectable", 20));

    // And a row content does not define is still rejected.
    callSet(harness, "SetPlanetAvailable", 24, 1);
    EXPECT_EQ(0, callGetInt(harness, "GetPlanetAvailable", 24));
}
