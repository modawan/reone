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
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/script/virtualmachine.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

constexpr int kGetSubString = 65;

struct StringRoutineFixture {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game;
    Routines routines;
    script::ExecutionContext ctx;

    explicit StringRoutineFixture(GameID gameId = GameID::TSL) :
        game(gameId, "", engine.options(), engine.services(), console),
        routines(gameId, &game, &engine.services()) {

        routines.init();
    }

    std::string subString(const std::string &value, int start, int count) {
        return routines.get(kGetSubString)
            .invoke({script::Variable::ofString(value),
                     script::Variable::ofInt(start),
                     script::Variable::ofInt(count)},
                    ctx)
            .strValue;
    }
};

} // namespace

// The declaration reads "Get nCount characters from sString, starting at
// nStart", so the third argument is a length and the second is a zero-based
// offset.
TEST(GetSubString, takes_the_requested_number_of_characters) {
    StringRoutineFixture fixture;

    EXPECT_EQ("bcd", fixture.subString("abcdef", 1, 3));
    EXPECT_EQ("cde", fixture.subString("abcdef", 2, 3));
    EXPECT_EQ("ab", fixture.subString("abcdef", 0, 2));
}

// The shape shipped content uses most: a single leading character. This is also
// the sharpest regression, since taking the start as the length returned
// nothing at all here.
TEST(GetSubString, takes_a_single_leading_character) {
    StringRoutineFixture fixture;

    EXPECT_EQ("a", fixture.subString("abcdef", 0, 1));
}

// The other shipped literal shape: a three-character prefix.
TEST(GetSubString, takes_a_leading_run_of_characters) {
    StringRoutineFixture fixture;

    EXPECT_EQ("abc", fixture.subString("abcdef", 0, 3));
}

// Shipped K2 content slices the planet code out of a module name this way. The
// offset and the length happen to be equal here, so this case alone cannot tell
// a correct implementation from one that uses the offset as the length.
TEST(GetSubString, slices_a_module_planet_code) {
    StringRoutineFixture fixture;

    EXPECT_EQ("NAR", fixture.subString("301NAR", 3, 3));
}

TEST(GetSubString, takes_the_whole_string_and_the_trailing_run) {
    StringRoutineFixture fixture;

    EXPECT_EQ("abcdef", fixture.subString("abcdef", 0, 6));
    EXPECT_EQ("ef", fixture.subString("abcdef", 4, 2));
}

// A request that runs past the end has nothing well defined to return, and the
// declaration documents the empty string for an error. GetStringLeft and
// GetStringRight already answer their own out-of-range requests that way.
TEST(GetSubString, yields_an_empty_string_for_an_out_of_range_request) {
    StringRoutineFixture fixture;

    EXPECT_EQ("", fixture.subString("abcdef", 4, 5));
    EXPECT_EQ("", fixture.subString("abcdef", 7, 1));
    EXPECT_EQ("", fixture.subString("abcdef", -1, 2));
    EXPECT_EQ("", fixture.subString("abcdef", 1, -2));
    EXPECT_EQ("", fixture.subString("", 0, 1));
}

TEST(GetSubString, a_count_of_zero_yields_an_empty_string) {
    StringRoutineFixture fixture;

    EXPECT_EQ("", fixture.subString("abcdef", 2, 0));
}

TEST(GetSubString, is_registered_for_both_games) {
    StringRoutineFixture tsl(GameID::TSL);
    StringRoutineFixture k1(GameID::KotOR);

    EXPECT_EQ("GetSubString", tsl.routines.get(kGetSubString).name());
    EXPECT_EQ("GetSubString", k1.routines.get(kGetSubString).name());
    EXPECT_EQ("bcd", k1.subString("abcdef", 1, 3));
}

// Reached the way a compiled script reaches it. Arguments are pushed so that
// argument 0 - the string - ends up on top, which is what pins the
// (string, start, count) order down end to end.
TEST(GetSubString, takes_its_arguments_in_order_through_the_virtual_machine) {
    StringRoutineFixture fixture;

    auto program = std::make_shared<script::ScriptProgram>("substring_order");
    program->add(script::Instruction::newCONSTI(3));  // nCount
    program->add(script::Instruction::newCONSTI(1));  // nStart
    program->add(script::Instruction::newCONSTS("abcdef"));
    program->add(script::Instruction::newACTION(kGetSubString, 3));
    program->add(script::Instruction::newCONSTS("bcd"));
    program->add(script::Instruction(script::InstructionType::EQUALSS));
    program->add(script::Instruction(script::InstructionType::RETN));

    auto execution = std::make_unique<script::ExecutionContext>();
    execution->routines = &fixture.routines;

    EXPECT_EQ(1, script::VirtualMachine(program, std::move(execution)).run());
}
