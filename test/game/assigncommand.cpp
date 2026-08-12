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
#include "reone/game/action/randomwalk.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/executionstate.h"
#include "reone/script/program.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace {

// ScriptProgram gives the first instruction it is handed the same offset the
// virtual machine starts a program at.
constexpr uint32_t kEntryOffset = 13;

/**
 * A saved command whose whole body is a single call to the named routine, the
 * shape AssignCommand and DelayCommand call sites take in shipped scripts.
 */
std::shared_ptr<script::ExecutionContext> newCommandCalling(
    Routines &routines,
    const std::string &routineName) {

    auto program = std::make_shared<script::ScriptProgram>("assigned_command");
    program->add(script::Instruction::newACTION(routines.getIndexByName(routineName), 0));
    program->add(script::Instruction(script::InstructionType::RETN));

    auto state = std::make_shared<script::ExecutionState>();
    state->program = std::move(program);
    state->insOffset = kEntryOffset;

    auto command = std::make_shared<script::ExecutionContext>();
    command->routines = &routines;
    command->savedState = std::move(state);
    return command;
}

/**
 * As above, but for a routine taking one float, so two commands in a sequence
 * can be told apart by the action each of them queues.
 */
std::shared_ptr<script::ExecutionContext> newCommandCallingWithFloat(
    Routines &routines,
    const std::string &routineName,
    float argument) {

    auto program = std::make_shared<script::ScriptProgram>("assigned_command");
    program->add(script::Instruction::newCONSTF(argument));
    program->add(script::Instruction::newACTION(routines.getIndexByName(routineName), 1));
    program->add(script::Instruction(script::InstructionType::RETN));

    auto state = std::make_shared<script::ExecutionState>();
    state->program = std::move(program);
    state->insOffset = kEntryOffset;

    auto command = std::make_shared<script::ExecutionContext>();
    command->routines = &routines;
    command->savedState = std::move(state);
    return command;
}

void assignCommand(
    Routines &routines,
    uint32_t subjectId,
    std::shared_ptr<script::ExecutionContext> command) {

    std::vector<script::Variable> args {
        script::Variable::ofObject(subjectId),
        script::Variable::ofAction(std::move(command))};

    script::Routine &routine = routines.get(routines.getIndexByName("AssignCommand"));
    ASSERT_EQ("AssignCommand", routine.name());

    script::ExecutionContext execution;
    execution.routines = &routines;
    routine.invoke(args, execution);
}

} // namespace

TEST(AssignCommand, should_run_the_assigned_command_in_place) {
    // given
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines routines(resource::GameID::KotOR, &game, &engine.services());
    routines.init();

    auto subject = game.newCreature();
    subject->addAction(game.newAction<RandomWalkAction>());
    ASSERT_EQ(1u, subject->actions().size());

    // ClearAllActions acts on the caller, so it shows the instant it runs. A
    // command that was queued instead would be sitting behind the random walk
    // with the queue untouched.
    auto command = newCommandCalling(routines, "ClearAllActions");

    // when
    assignCommand(routines, subject->id(), std::move(command));

    // then
    EXPECT_TRUE(subject->actions().empty());
}

TEST(AssignCommand, should_queue_action_routines_the_command_contains) {
    // given
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines routines(resource::GameID::KotOR, &game, &engine.services());
    routines.init();

    auto subject = game.newCreature();
    ASSERT_TRUE(subject->actions().empty());

    auto command = newCommandCalling(routines, "ActionRandomWalk");

    // when
    assignCommand(routines, subject->id(), std::move(command));

    // then
    // Running in place does not stop an Action routine inside the command from
    // queueing: what lands is the walk the command asked for, and not the
    // command itself.
    ASSERT_EQ(1u, subject->actions().size());
    EXPECT_EQ(ActionType::RandomWalk, subject->actions().front()->type());
}

TEST(AssignCommand, should_run_the_command_as_the_subject_not_the_saving_caller) {
    // given
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines routines(resource::GameID::KotOR, &game, &engine.services());
    routines.init();

    auto saver = game.newCreature();
    auto subject = game.newCreature();
    saver->addAction(game.newAction<RandomWalkAction>());
    subject->addAction(game.newAction<RandomWalkAction>());

    // The context that saved the command belongs to someone else entirely.
    auto command = newCommandCalling(routines, "ClearAllActions");
    command->args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(saver->id()));

    // when
    assignCommand(routines, subject->id(), std::move(command));

    // then
    EXPECT_TRUE(subject->actions().empty());
    EXPECT_EQ(1u, saver->actions().size());
}

TEST(AssignCommand, should_keep_the_order_of_commands_assigned_in_sequence) {
    // given
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines routines(resource::GameID::KotOR, &game, &engine.services());
    routines.init();

    auto subject = game.newCreature();
    ASSERT_TRUE(subject->actions().empty());

    // when
    // Two commands, each queueing a different action, so the order they landed
    // in is readable off the queue.
    assignCommand(routines, subject->id(), newCommandCalling(routines, "ActionRandomWalk"));
    assignCommand(routines, subject->id(), newCommandCallingWithFloat(routines, "ActionWait", 0.5f));

    // then
    // Running in place has to leave the sequence as written. Dispatching onto
    // the front of the queue instead would hand these back reversed.
    ASSERT_EQ(2u, subject->actions().size());
    EXPECT_EQ(ActionType::RandomWalk, subject->actions()[0]->type());
    EXPECT_EQ(ActionType::Wait, subject->actions()[1]->type());
}

TEST(AssignCommand, should_not_leak_the_rewritten_caller_into_the_saved_command) {
    // given
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines routines(resource::GameID::KotOR, &game, &engine.services());
    routines.init();

    auto saver = game.newCreature();
    auto first = game.newCreature();
    auto second = game.newCreature();
    saver->addAction(game.newAction<RandomWalkAction>());
    first->addAction(game.newAction<RandomWalkAction>());
    second->addAction(game.newAction<RandomWalkAction>());

    auto command = newCommandCalling(routines, "ClearAllActions");
    command->args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(saver->id()));

    // when
    // The same saved command, assigned twice to different subjects.
    assignCommand(routines, first->id(), command);
    assignCommand(routines, second->id(), command);

    // then
    // Each run rewrites the Caller on its own copy. Rewriting the saved command
    // in place would pin it to the first subject and misdirect every run after.
    EXPECT_TRUE(first->actions().empty());
    EXPECT_TRUE(second->actions().empty());
    EXPECT_EQ(1u, saver->actions().size());
}
