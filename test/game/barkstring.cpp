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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/types.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

// The Endar Spire's door and feedback scripts bark with this routine.
constexpr int kBarkString = 671;

class BarkHarness : boost::noncopyable {
public:
    BarkHarness(TestEngine &engine) :
        _game(GameID::KotOR, "", engine.options(), engine.services(), _console),
        _routines(GameID::KotOR, &_game, &engine.services()) {
        _routines.init();
    }

    Variable call(std::vector<Variable> args) {
        Routine &routine = _routines.get(kBarkString);
        ExecutionContext execution;
        execution.routines = &_routines;
        return routine.invoke(args, execution);
    }

    Routines &routines() { return _routines; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

} // namespace

TEST(BarkString, a_general_bark_reads_its_line_from_the_talk_table) {
    TestEngine engine;
    engine.init();
    BarkHarness harness(engine);

    // Not implemented, the routine only warned and never read the line.
    EXPECT_CALL(engine.resourceModule().strings(), getText(42015)).WillOnce(Return("The bridge is sealed!"));
    harness.call({Variable::ofObject(kObjectInvalid), Variable::ofInt(42015)});
}

TEST(BarkString, a_line_with_no_text_is_read_and_shows_nothing) {
    TestEngine engine;
    engine.init();
    BarkHarness harness(engine);

    EXPECT_CALL(engine.resourceModule().strings(), getText(-1)).WillOnce(Return(""));
    EXPECT_EQ(VariableType::Void, harness.call({Variable::ofObject(kObjectInvalid), Variable::ofInt(-1)}).type);
}

TEST(ConversationSpokenTime, is_a_second_plus_a_ninth_of_a_second_per_character) {
    EXPECT_FLOAT_EQ(1.0f, Conversation::spokenTime(""));
    EXPECT_FLOAT_EQ(1.0f + 0.11f * 10.0f, Conversation::spokenTime("0123456789"));
}
