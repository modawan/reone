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

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

// It works only during dialogue; with none running, the game refuses the cut
// and the routine (461) returns nothing. The cut itself is covered in
// conversation.cpp, where a dialogue runs.
TEST(SetDialogPlaceableCamera, does_nothing_outside_a_dialogue) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();

    EXPECT_FALSE(game.cutDialogToPlaceableCamera(3));

    ExecutionContext execution;
    execution.routines = &routines;
    auto result = routines.get(461).invoke({Variable::ofInt(3)}, execution);
    EXPECT_EQ(VariableType::Void, result.type);
}
