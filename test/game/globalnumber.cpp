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

#include <limits>

#include "../fixtures/engine.h"

#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/script/virtualmachine.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace {

// KotOR II routine numbers.
constexpr int kIncrementGlobalNumber = 799;
constexpr int kDecrementGlobalNumber = 800;
constexpr int kGetGlobalNumber = 580;

constexpr char kCounter[] = "TEST_COUNTER";
constexpr char kOtherCounter[] = "OTHER_COUNTER";

// The signed 8-bit domain number globals live in.
constexpr int kMinGlobalNumber = -128;
constexpr int kMaxGlobalNumber = 127;

struct GlobalNumberFixture {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game {resource::GameID::TSL, "", engine.options(), engine.services(), console};
    Routines routines {resource::GameID::TSL, &game, &engine.services()};
    script::ExecutionContext ctx;

    GlobalNumberFixture() {
        routines.init();
    }

    // Invoke a routine the way a script does: by number, with script variables.
    script::Variable call(int index, const std::string &identifier, int amount) {
        return routines.get(index).invoke(
            {script::Variable::ofString(identifier), script::Variable::ofInt(amount)}, ctx);
    }

    int readThroughRoutine(const std::string &identifier) {
        return routines.get(kGetGlobalNumber)
            .invoke({script::Variable::ofString(identifier)}, ctx)
            .intValue;
    }
};

} // namespace

// -- Increment ---------------------------------------------------------------

TEST(GlobalNumber, increment_adds_the_given_amount) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 0);

    fixture.call(kIncrementGlobalNumber, kCounter, 1);

    EXPECT_EQ(1, fixture.game.getGlobalNumber(kCounter));
}

// The routine is not a "+1": the amount the caller supplies is the amount added.
// Shipped content passes +3, +20 and +25 to the same accumulator.
TEST(GlobalNumber, increment_honours_the_amount_rather_than_stepping_by_one) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 5);

    fixture.call(kIncrementGlobalNumber, kCounter, 20);

    EXPECT_EQ(25, fixture.game.getGlobalNumber(kCounter));
}

// A negative amount reduces the value, and the domain is signed, so it may go
// below zero. Shipped content increments a standing counter by -1.
TEST(GlobalNumber, increment_with_a_negative_amount_reduces_the_value_below_zero) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 0);

    fixture.call(kIncrementGlobalNumber, kCounter, -1);

    EXPECT_EQ(-1, fixture.game.getGlobalNumber(kCounter));
}

TEST(GlobalNumber, repeated_increments_accumulate) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 0);

    fixture.call(kIncrementGlobalNumber, kCounter, 3);
    fixture.call(kIncrementGlobalNumber, kCounter, 3);

    EXPECT_EQ(6, fixture.game.getGlobalNumber(kCounter));
}

// Landing exactly on the ceiling is a success, not a rejection.
TEST(GlobalNumber, increment_onto_the_upper_bound_is_accepted) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 120);

    fixture.call(kIncrementGlobalNumber, kCounter, 7);

    EXPECT_EQ(kMaxGlobalNumber, fixture.game.getGlobalNumber(kCounter));
}

// Past the ceiling the call fails and the stored value is left alone. In
// particular it must not wrap to -128, nor be stored as an out-of-domain 128,
// nor clamp - clamping and rejection agree here only because the value already
// sits on the bound, which the next test separates.
TEST(GlobalNumber, increment_past_the_upper_bound_leaves_the_value_unchanged) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, kMaxGlobalNumber);

    fixture.call(kIncrementGlobalNumber, kCounter, 1);

    EXPECT_EQ(kMaxGlobalNumber, fixture.game.getGlobalNumber(kCounter));
}

// Rejection rather than saturation: a large increment on a mid-range
// accumulator keeps its old value instead of being pinned to the ceiling. This
// is the case shipped treasure scripts can reach, stepping +25 at a time.
TEST(GlobalNumber, increment_overshooting_the_upper_bound_does_not_clamp_to_it) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 110);

    fixture.call(kIncrementGlobalNumber, kCounter, 25);

    EXPECT_EQ(110, fixture.game.getGlobalNumber(kCounter));
}

// The floor is documented for the decrement routine; the increment routine
// shares the same 8-bit domain, so a negative amount is bounded by it too.
TEST(GlobalNumber, increment_past_the_lower_bound_leaves_the_value_unchanged) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, kMinGlobalNumber);

    fixture.call(kIncrementGlobalNumber, kCounter, -1);

    EXPECT_EQ(kMinGlobalNumber, fixture.game.getGlobalNumber(kCounter));
}

// -- Decrement ---------------------------------------------------------------

TEST(GlobalNumber, decrement_subtracts_the_given_amount) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 10);

    fixture.call(kDecrementGlobalNumber, kCounter, 4);

    EXPECT_EQ(6, fixture.game.getGlobalNumber(kCounter));
}

// Decrementing by a negative amount subtracts a negative, raising the value.
TEST(GlobalNumber, decrement_with_a_negative_amount_raises_the_value) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 5);

    fixture.call(kDecrementGlobalNumber, kCounter, -3);

    EXPECT_EQ(8, fixture.game.getGlobalNumber(kCounter));
}

TEST(GlobalNumber, decrement_onto_the_lower_bound_is_accepted) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, -120);

    fixture.call(kDecrementGlobalNumber, kCounter, 8);

    EXPECT_EQ(kMinGlobalNumber, fixture.game.getGlobalNumber(kCounter));
}

TEST(GlobalNumber, decrement_past_the_lower_bound_leaves_the_value_unchanged) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, kMinGlobalNumber);

    fixture.call(kDecrementGlobalNumber, kCounter, 1);

    EXPECT_EQ(kMinGlobalNumber, fixture.game.getGlobalNumber(kCounter));
}

TEST(GlobalNumber, decrement_undershooting_the_lower_bound_does_not_clamp_to_it) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, -110);

    fixture.call(kDecrementGlobalNumber, kCounter, 25);

    EXPECT_EQ(-110, fixture.game.getGlobalNumber(kCounter));
}

// A negative amount on the decrement routine is bounded by the ceiling.
TEST(GlobalNumber, decrement_past_the_upper_bound_leaves_the_value_unchanged) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, kMaxGlobalNumber);

    fixture.call(kDecrementGlobalNumber, kCounter, -1);

    EXPECT_EQ(kMaxGlobalNumber, fixture.game.getGlobalNumber(kCounter));
}

// -- Domain arithmetic -------------------------------------------------------

// The bounds check has to survive amounts that would overflow the argument's own
// width, so the extremes have to be rejected rather than folding around into an
// in-domain result.
TEST(GlobalNumber, extreme_amounts_are_rejected_without_overflowing) {
    GlobalNumberFixture fixture;
    const int intMax = std::numeric_limits<int>::max();
    const int intMin = std::numeric_limits<int>::min();

    fixture.game.setGlobalNumber(kCounter, 0);
    fixture.call(kIncrementGlobalNumber, kCounter, intMax);
    EXPECT_EQ(0, fixture.game.getGlobalNumber(kCounter));

    fixture.call(kIncrementGlobalNumber, kCounter, intMin);
    EXPECT_EQ(0, fixture.game.getGlobalNumber(kCounter));

    // Negating the most negative int is what makes this the interesting case for
    // the decrement routine.
    fixture.call(kDecrementGlobalNumber, kCounter, intMin);
    EXPECT_EQ(0, fixture.game.getGlobalNumber(kCounter));

    fixture.call(kDecrementGlobalNumber, kCounter, intMax);
    EXPECT_EQ(0, fixture.game.getGlobalNumber(kCounter));
}

TEST(GlobalNumber, an_amount_of_zero_leaves_the_value_alone) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 42);

    fixture.call(kIncrementGlobalNumber, kCounter, 0);
    EXPECT_EQ(42, fixture.game.getGlobalNumber(kCounter));

    fixture.call(kDecrementGlobalNumber, kCounter, 0);
    EXPECT_EQ(42, fixture.game.getGlobalNumber(kCounter));
}

// -- Reach -------------------------------------------------------------------

// Shipped content reads a counter back in the same logic that increments it, so
// the change has to be visible to the query routine straight away.
TEST(GlobalNumber, the_new_value_is_visible_to_the_query_routine) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 0);

    fixture.call(kIncrementGlobalNumber, kCounter, 1);

    EXPECT_EQ(1, fixture.readThroughRoutine(kCounter));
}

TEST(GlobalNumber, only_the_named_global_is_touched) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 0);
    fixture.game.setGlobalNumber(kOtherCounter, 7);

    fixture.call(kIncrementGlobalNumber, kCounter, 1);

    EXPECT_EQ(7, fixture.game.getGlobalNumber(kOtherCounter));
}

// The routines are documented as working on number globals only. A boolean of
// the same name keeps its value - the number side of such a name is deliberately
// not asserted, since number storage inserts on write and the original engine's
// behaviour for a non-number name is not established.
TEST(GlobalNumber, a_boolean_global_of_the_same_name_is_not_disturbed) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalBoolean("TEST_FLAG", true);

    fixture.call(kIncrementGlobalNumber, "TEST_FLAG", 1);

    EXPECT_TRUE(fixture.game.getGlobalBoolean("TEST_FLAG"));
}

// -- Game gating -------------------------------------------------------------

TEST(GlobalNumber, the_routines_are_registered_for_kotor_two) {
    GlobalNumberFixture fixture;

    EXPECT_EQ("IncrementGlobalNumber", fixture.routines.get(kIncrementGlobalNumber).name());
    EXPECT_EQ("DecrementGlobalNumber", fixture.routines.get(kDecrementGlobalNumber).name());
}

// Neither routine exists in the KotOR I script set, and that routine table has
// no entry at either number, so the lookup fails outright.
TEST(GlobalNumber, the_routines_are_absent_from_kotor_one) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines k1(resource::GameID::KotOR, &game, &engine.services());
    k1.init();

    EXPECT_THROW(k1.get(kIncrementGlobalNumber), std::out_of_range);
    EXPECT_THROW(k1.get(kDecrementGlobalNumber), std::out_of_range);
    EXPECT_EQ(-1, k1.getIndexByName("IncrementGlobalNumber"));
    EXPECT_EQ(-1, k1.getIndexByName("DecrementGlobalNumber"));

    // The get/set pair KotOR I does have is untouched by this change.
    EXPECT_EQ("GetGlobalNumber", k1.get(kGetGlobalNumber).name());
}

// -- Through the virtual machine ---------------------------------------------

// The routine reached by a compiled script, not just by a direct table call.
// Arguments are pushed so that argument 0 - the identifier - ends up on top of
// the stack, which is what pins the (string, int) order down end to end.
TEST(GlobalNumber, a_compiled_script_increments_through_the_virtual_machine) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 40);

    auto program = std::make_shared<script::ScriptProgram>("test_increment");
    program->add(script::Instruction::newCONSTI(2));
    program->add(script::Instruction::newCONSTS(kCounter));
    program->add(script::Instruction::newACTION(kIncrementGlobalNumber, 2));
    program->add(script::Instruction(script::InstructionType::RETN));

    auto execution = std::make_unique<script::ExecutionContext>();
    execution->routines = &fixture.routines;
    script::VirtualMachine(program, std::move(execution)).run();

    EXPECT_EQ(42, fixture.game.getGlobalNumber(kCounter));
}

TEST(GlobalNumber, a_compiled_script_decrements_through_the_virtual_machine) {
    GlobalNumberFixture fixture;
    fixture.game.setGlobalNumber(kCounter, 40);

    auto program = std::make_shared<script::ScriptProgram>("test_decrement");
    program->add(script::Instruction::newCONSTI(2));
    program->add(script::Instruction::newCONSTS(kCounter));
    program->add(script::Instruction::newACTION(kDecrementGlobalNumber, 2));
    program->add(script::Instruction(script::InstructionType::RETN));

    auto execution = std::make_unique<script::ExecutionContext>();
    execution->routines = &fixture.routines;
    script::VirtualMachine(program, std::move(execution)).run();

    EXPECT_EQ(38, fixture.game.getGlobalNumber(kCounter));
}
