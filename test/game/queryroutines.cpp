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
#include "../fixtures/game.h"
#include "reone/game/game.h"
#include "reone/game/gui/ingame.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/sound.h"
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

void reone::game::TestGameModule::setSoundPitchVariation(Sound &sound, float pitchVariation) {
    sound._pitchVariation = pitchVariation;
}

namespace {

/**
 * A game plus the routine table it is bound to, so a routine is called the
 * way a compiled script calls it - through the registered table.
 */
class QueryRoutineHarness : boost::noncopyable {
public:
    QueryRoutineHarness(GameID gameId) :
        _game(gameId, "", testEngine().options(), testEngine().services(), _console),
        _routines(gameId, &_game, &testEngine().services()) {

        _routines.init();
    }

    Variable call(const std::string &name, std::vector<Variable> args = {}) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        return routine.invoke(args, execution);
    }

    Game &game() { return _game; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

constexpr uint32_t kMinute = 60 * 1000;
constexpr uint32_t kSecond = 1000;

} // namespace

// With Mod_MinPerHour 2 an hour lasts two minutes of world time, so the minute
// within the hour runs 0 to 1 while seconds and milliseconds are ordinary.
// The day is not part of any of the four.
TEST(QueryRoutines, get_time_splits_the_time_of_day_by_the_module_hour_length) {
    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        QueryRoutineHarness harness(gameId);
        TestGameModule::setSnapshotWorldTime(
            harness.game(), 3, 7 * 2 * kMinute + 1 * kMinute + 23 * kSecond + 456, 2);

        EXPECT_EQ(7, harness.call("GetTimeHour").intValue);
        EXPECT_EQ(1, harness.call("GetTimeMinute").intValue);
        EXPECT_EQ(23, harness.call("GetTimeSecond").intValue);
        EXPECT_EQ(456, harness.call("GetTimeMillisecond").intValue);
    }
}

// With Mod_MinPerHour 60 the clock reads as an ordinary clock.
TEST(QueryRoutines, get_time_reads_a_sixty_minute_hour_as_a_clock) {
    QueryRoutineHarness harness(GameID::KotOR);
    TestGameModule::setSnapshotWorldTime(
        harness.game(), 0, 13 * 60 * kMinute + 45 * kMinute + 9 * kSecond + 5, 60);

    EXPECT_EQ(13, harness.call("GetTimeHour").intValue);
    EXPECT_EQ(45, harness.call("GetTimeMinute").intValue);
    EXPECT_EQ(9, harness.call("GetTimeSecond").intValue);
    EXPECT_EQ(5, harness.call("GetTimeMillisecond").intValue);
}

// The resource name of the module, not the localized Mod_Name that
// GetModuleName answers.
TEST(QueryRoutines, get_module_file_name_returns_the_module_resource_name) {
    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        QueryRoutineHarness harness(gameId);
        TestGameModule::setActiveModule(harness.game(), true);
        TestGameModule::setSnapshotModuleName(harness.game(), "danm13");

        EXPECT_EQ("danm13", harness.call("GetModuleFileName").strValue);
    }
}

TEST(QueryRoutines, get_module_file_name_is_empty_without_a_module) {
    QueryRoutineHarness harness(GameID::KotOR);

    EXPECT_EQ("", harness.call("GetModuleFileName").strValue);
}

// The sound's authored PitchVariation; any other object has none.
TEST(QueryRoutines, sound_object_get_pitch_variance_returns_the_sound_pitch_variation) {
    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        QueryRoutineHarness harness(gameId);
        auto sound = harness.game().newSound();
        TestGameModule::setSoundPitchVariation(*sound, 0.25f);
        auto creature = harness.game().newCreature();

        EXPECT_FLOAT_EQ(0.25f, harness.call("SoundObjectGetPitchVariance", {Variable::ofObject(sound->id())}).floatValue);
        EXPECT_FLOAT_EQ(0.0f, harness.call("SoundObjectGetPitchVariance", {Variable::ofObject(creature->id())}).floatValue);
    }
}

// Outside the in-game menu no sub-screen is open: SUBSCREEN_ID_NONE.
TEST(QueryRoutines, get_sub_screen_id_is_none_outside_the_in_game_menu) {
    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        QueryRoutineHarness harness(gameId);

        EXPECT_EQ(0, harness.call("GetSubScreenID").intValue);
    }
}

// Each in-game menu tab maps to its SUBSCREEN_ID_* constant in nwscript.nss.
// The party selection is not one of them.
TEST(QueryRoutines, sub_screen_ids_follow_the_nwscript_constants) {
    EXPECT_EQ(0, subScreenId(InGameMenuTab::None));
    EXPECT_EQ(1, subScreenId(InGameMenuTab::Equipment));
    EXPECT_EQ(2, subScreenId(InGameMenuTab::Inventory));
    EXPECT_EQ(3, subScreenId(InGameMenuTab::Character));
    EXPECT_EQ(4, subScreenId(InGameMenuTab::Abilities));
    EXPECT_EQ(5, subScreenId(InGameMenuTab::Map));
    EXPECT_EQ(6, subScreenId(InGameMenuTab::Journal));
    EXPECT_EQ(7, subScreenId(InGameMenuTab::Options));
    EXPECT_EQ(8, subScreenId(InGameMenuTab::Messages));
    EXPECT_EQ(0, subScreenId(InGameMenuTab::Party));
}
