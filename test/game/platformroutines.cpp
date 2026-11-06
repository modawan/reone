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
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

/**
 * A game plus the routine table it is bound to, so a routine is called the
 * way a compiled script calls it - through the registered table.
 */
class PlatformRoutineHarness : boost::noncopyable {
public:
    PlatformRoutineHarness(GameID gameId) :
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

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

} // namespace

// TSL scripts branch on the platform; this engine runs on PCs.
TEST(PlatformRoutines, get_is_xbox_returns_false) {
    PlatformRoutineHarness harness(GameID::TSL);

    EXPECT_EQ(0, harness.call("GetIsXBox").intValue);
}

// The PC release ships no Live content packages, so none is ever available.
TEST(PlatformRoutines, get_is_live_content_available_returns_false) {
    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        PlatformRoutineHarness harness(gameId);

        EXPECT_EQ(0, harness.call("GetIsLiveContentAvailable", {Variable::ofInt(1)}).intValue);
    }
}

// The engine reports that it took the pattern, and that it took it off again,
// whether or not a rumble device is there to feel it.
TEST(PlatformRoutines, rumble_patterns_report_success) {
    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        PlatformRoutineHarness harness(gameId);

        EXPECT_EQ(1, harness.call("PlayRumblePattern", {Variable::ofInt(1)}).intValue);
        EXPECT_EQ(1, harness.call("StopRumblePattern", {Variable::ofInt(1)}).intValue);
    }
}
