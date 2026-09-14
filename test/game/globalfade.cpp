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

#include <limits>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/script/virtualmachine.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace {
using Direction = GlobalFade::Direction;
using Source = GlobalFade::Source;

TEST(GlobalFade, delayed_in_replaces_immediate_out_without_a_clear_state) {
    GlobalFade fade;
    fade.request(Direction::Out);
    ASSERT_FLOAT_EQ(1.0f, fade.opacity());
    fade.request(Direction::In, 3.0f, 1.5f);
    EXPECT_FLOAT_EQ(1.0f, fade.opacity());
    fade.update(2.8f);
    EXPECT_FLOAT_EQ(1.0f, fade.opacity());
    fade.update(0.85f);
    EXPECT_NEAR(0.5f, fade.opacity(), 1e-6f);
    fade.update(1.0f);
    EXPECT_FLOAT_EQ(0.0f, fade.opacity());
}

TEST(GlobalFade, replacement_restarts_direction_color_and_wait_not_current_opacity) {
    GlobalFade fade;
    fade.request(Direction::Out, 0, 2, {1, 0, 0});
    fade.update(0.9f);
    ASSERT_NEAR(0.5f, fade.opacity(), 1e-6f);
    fade.request(Direction::In, 1, 1, {0, 0, 1});
    EXPECT_FLOAT_EQ(1, fade.opacity());
    EXPECT_EQ(glm::vec3(0, 0, 1), fade.color());
    fade.request(Direction::Out, 1, 0, {0, 1, 0});
    EXPECT_FLOAT_EQ(0, fade.opacity());
    fade.update(1.0f);
    EXPECT_FLOAT_EQ(1, fade.opacity());
    fade.update(4.0f);
    EXPECT_FLOAT_EQ(1, fade.opacity());
}

TEST(GlobalFade, strict_wait_boundary_includes_initial_tenth_and_skips_large_or_invalid_deltas) {
    GlobalFade fade;
    fade.request(Direction::Out, 0.1f, 0);
    EXPECT_FLOAT_EQ(0, fade.opacity()); // equality is not past wait
    for (float dt : {0.0f, -1.0f, 5.0f, 20.0f,
                     std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        fade.update(dt);
        EXPECT_FLOAT_EQ(0, fade.opacity());
    }
    fade.update(0.001f);
    EXPECT_FLOAT_EQ(1, fade.opacity());
    fade.request(Direction::Out, 0, 10);
    EXPECT_NEAR(.01f, fade.opacity(), 1e-6f);
    fade.update(4.999f);
    EXPECT_NEAR(.5099f, fade.opacity(), 1e-6f);
}

TEST(GlobalFade, invalid_request_arguments_are_finite_nonnegative_and_color_is_bounded) {
    GlobalFade fade;
    fade.request(Direction::Out, -1, std::numeric_limits<float>::infinity(),
                 {std::numeric_limits<float>::quiet_NaN(), 2, -1});
    EXPECT_FLOAT_EQ(1, fade.opacity());
    EXPECT_EQ(glm::vec3(0, 1, 0), fade.color());
    fade.request(Direction::In, std::numeric_limits<float>::quiet_NaN(), -1);
    EXPECT_FLOAT_EQ(0, fade.opacity());
}

TEST(GlobalFade, lock_rejects_out_stop_and_automatic_reveal_but_consumes_hold) {
    GlobalFade fade;
    fade.request(Direction::Out);
    fade.lockUntilScript();
    fade.holdForDialog();
    auto dialog = fade.admitDialog();
    fade.revealDialog(dialog);
    EXPECT_FALSE(fade.heldForDialog());
    EXPECT_TRUE(fade.locked());
    EXPECT_FALSE(fade.stop());
    EXPECT_FALSE(fade.request(Direction::Out, 0, 5, {1, 0, 0}, Source::Script));
    EXPECT_FALSE(fade.request(Direction::In));
    EXPECT_FLOAT_EQ(1, fade.opacity());
    EXPECT_TRUE(fade.request(Direction::In, 0, 0, {}, Source::Script));
    EXPECT_FALSE(fade.locked());
    fade.update(1);
    EXPECT_FLOAT_EQ(0, fade.opacity());
}

TEST(GlobalFade, movie_policy_freezes_elapsed_rejects_visual_requests_and_still_unlocks) {
    GlobalFade fade;
    fade.request(Direction::Out, 0, 2);
    float before = fade.opacity();
    fade.lockUntilScript();
    fade.setMovieOverride(true);
    fade.update(2);
    EXPECT_FLOAT_EQ(before, fade.opacity());
    EXPECT_FALSE(fade.request(Direction::In, 0, 0, {}, Source::Script));
    EXPECT_FALSE(fade.locked());
    fade.setMovieOverride(false);
    fade.update(.5f);
    EXPECT_NEAR(before + .25f, fade.opacity(), 1e-6f); // no replay of the rejected in
}

TEST(GlobalFade, ordinary_arrival_replaces_early_authored_request_once) {
    GlobalFade fade;
    auto arrival = fade.beginArrival();
    fade.request(Direction::In, 3, 1.5f, {}, Source::Script);
    fade.finishLoading(arrival);
    fade.settleArrival(arrival);
    fade.update(.9f);
    EXPECT_NEAR(.5f, fade.opacity(), 1e-6f);
    fade.request(Direction::Out);
    fade.settleArrival(arrival);
    EXPECT_FLOAT_EQ(1, fade.opacity());
}

TEST(GlobalFade, hold_suppresses_arrival_and_does_not_itself_make_black) {
    GlobalFade fade;
    fade.holdForDialog();
    EXPECT_FLOAT_EQ(0, fade.opacity());
    auto arrival = fade.beginArrival();
    fade.settleArrival(arrival);
    fade.update(4);
    EXPECT_FLOAT_EQ(1, fade.opacity());
    EXPECT_TRUE(fade.heldForDialog());
}

TEST(GlobalFade, entry_consumed_during_arrival_does_not_get_another_default_reveal) {
    GlobalFade fade;
    auto arrival = fade.beginArrival();
    auto dialog = fade.admitDialog();
    fade.revealDialog(dialog);
    fade.request(Direction::Out); // first entry's authored action wins
    fade.finishLoading(arrival);
    fade.settleArrival(arrival);
    fade.update(3);
    EXPECT_FLOAT_EQ(1, fade.opacity());
    EXPECT_FALSE(fade.heldForDialog());
}

TEST(GlobalFade, replaced_and_retired_tickets_cannot_consume_new_hold_or_ready) {
    GlobalFade fade;
    auto oldArrival = fade.beginArrival();
    auto oldDialog = fade.admitDialog();
    auto newArrival = fade.beginArrival();
    auto newDialog = fade.admitDialog();
    fade.finishDialog(oldDialog);
    fade.settleArrival(oldArrival);
    EXPECT_TRUE(fade.heldForDialog());
    EXPECT_TRUE(fade.arrivalPending());
    auto replacement = fade.admitDialog(true);
    fade.finishDialog(newDialog);
    EXPECT_TRUE(fade.heldForDialog());
    fade.revealDialog(replacement);
    EXPECT_FALSE(fade.heldForDialog());
    fade.settleArrival(newArrival);
    fade.update(1);
    EXPECT_FLOAT_EQ(0, fade.opacity());
}

TEST(GlobalFade, abandoned_admission_recovers_but_expired_old_work_cannot_release_replacement) {
    GlobalFade fade;
    fade.request(Direction::Out);
    fade.holdForDialog();
    auto abandoned = fade.admitDialog();
    abandoned.reset();
    fade.update(1);
    EXPECT_FALSE(fade.heldForDialog());
    EXPECT_FLOAT_EQ(.9f, fade.opacity()); // new request receives no preceding frame time
    fade.update(1);
    EXPECT_FLOAT_EQ(0, fade.opacity());
    auto old = fade.admitDialog();
    auto current = fade.admitDialog(true);
    fade.request(Direction::Out);
    fade.holdForDialog();
    old.reset();
    fade.update(1);
    EXPECT_TRUE(fade.heldForDialog());
    EXPECT_FLOAT_EQ(1, fade.opacity());
}

class GlobalFadeVM : public TestWithParam<resource::GameID> {};

TEST_P(GlobalFadeVM, real_vm_fades_do_not_suspend_immediate_or_delayed_script_work) {
    using namespace reone::script;
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GetParam(), "", engine.options(), engine.services(), console);
    Routines routines(GetParam(), &game, &engine.services());
    routines.init();
    EXPECT_EQ("HoldWorldFadeInForDialog", routines.get(760).name());
    EXPECT_EQ(GetParam() == resource::GameID::KotOR ? "QueueMovie" : "SetFadeUntilScript", routines.get(769).name());
    auto caller = game.newModule();
    auto program = std::make_shared<ScriptProgram>("fade_progress");
    for (int action : {720, 719}) {
        for (float arg : {0.2f, 0.4f, 0.6f, action == 719 ? 1.5f : 0.0f, action == 719 ? 3.0f : 0.0f}) {
            program->add(Instruction::newCONSTF(arg));
        }
        program->add(Instruction::newACTION(action, 5));
    }
    program->add(Instruction::newSTORE_STATE(0, 0));
    program->add(Instruction::newJMP(31));
    program->add(Instruction::newCONSTI(1));
    program->add(Instruction::newCONSTI(42));
    program->add(Instruction::newCONSTO(kObjectSelf));
    program->add(Instruction::newACTION(routines.getIndexByName("SetLocalBoolean"), 3));
    program->add(Instruction(InstructionType::RETN));
    program->add(Instruction::newCONSTF(.5f));
    program->add(Instruction::newACTION(7, 2));
    program->add(Instruction::newCONSTI(1));
    program->add(Instruction::newCONSTI(43));
    program->add(Instruction::newCONSTO(kObjectSelf));
    program->add(Instruction::newACTION(routines.getIndexByName("SetLocalBoolean"), 3));
    program->add(Instruction(InstructionType::RETN));
    auto context = std::make_unique<ExecutionContext>();
    context->routines = &routines;
    context->args.emplace_back(ArgKind::Caller, Variable::ofObject(caller->id()));
    VirtualMachine(program, std::move(context)).run();
    EXPECT_TRUE(caller->getLocalBoolean(43));
    EXPECT_FALSE(caller->getLocalBoolean(42));
    EXPECT_FLOAT_EQ(1, game.globalFade().opacity());
    EXPECT_EQ(glm::vec3(.6f, .4f, .2f), game.globalFade().color());
    caller->Object::update(.25f);
    EXPECT_FALSE(caller->getLocalBoolean(42));
    caller->Object::update(.25f);
    EXPECT_TRUE(caller->getLocalBoolean(42));
    EXPECT_FLOAT_EQ(1, game.globalFade().opacity());
}

TEST_P(GlobalFadeVM, registered_hold_and_lock_routines_reject_and_unlock_through_vm) {
    using namespace reone::script;
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GetParam(), "", engine.options(), engine.services(), console);
    Routines routines(GetParam(), &game, &engine.services());
    routines.init();
    auto run = [&](std::initializer_list<int> actions) {
        auto program = std::make_shared<ScriptProgram>("fade_lock");
        for (auto id : actions) program->add(Instruction::newACTION(id, 0));
        program->add(Instruction(InstructionType::RETN));
        auto context = std::make_unique<ExecutionContext>();
        context->routines = &routines;
        VirtualMachine(program, std::move(context)).run();
    };
    run({720, 760});
    ASSERT_TRUE(game.globalFade().heldForDialog());
    if (GetParam() == resource::GameID::TSL) run({769, 720});
    auto dialog = game.globalFade().admitDialog();
    game.globalFade().revealDialog(dialog);
    EXPECT_FALSE(game.globalFade().heldForDialog());
    EXPECT_EQ(GetParam() == resource::GameID::TSL, game.globalFade().locked());
    run({719});
    EXPECT_FALSE(game.globalFade().locked());
    EXPECT_FLOAT_EQ(0, game.globalFade().opacity());
}

TEST_P(GlobalFadeVM, reachable_nonfinite_and_negative_arguments_are_deterministic) {
    using namespace reone::script;
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GetParam(), "", engine.options(), engine.services(), console);
    Routines routines(GetParam(), &game, &engine.services());
    routines.init();
    auto program = std::make_shared<ScriptProgram>("fade_invalid");
    for (float arg : {-1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(),
                     std::numeric_limits<float>::infinity(), -1.0f}) {
        program->add(Instruction::newCONSTF(arg));
    }
    program->add(Instruction::newACTION(720, 5));
    program->add(Instruction(InstructionType::RETN));
    auto context = std::make_unique<ExecutionContext>();
    context->routines = &routines;
    VirtualMachine(program, std::move(context)).run();
    EXPECT_EQ(glm::vec3(0, 1, 0), game.globalFade().color());
    EXPECT_FLOAT_EQ(1, game.globalFade().opacity());
}

INSTANTIATE_TEST_SUITE_P(BothGames, GlobalFadeVM, Values(resource::GameID::KotOR, resource::GameID::TSL));

TEST(GlobalFadeGame, presentation_advances_during_game_pause_and_menu_changes_do_not_reset_it) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    game.globalFade().request(Direction::Out, 0, 2);
    game.setPaused(true);
    game.update(.9f);
    EXPECT_NEAR(.5f, game.globalFade().opacity(), 1e-6f);
    game.openInGame();
    game.update(1);
    EXPECT_FLOAT_EQ(1, game.globalFade().opacity());
}

TEST(GlobalFadeGame, accepted_failed_start_and_cancel_consume_only_their_own_hold) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    game.globalFade().request(Direction::Out);
    game.globalFade().holdForDialog();
    auto actor = game.newCreature();
    auto action = game.newAction<StartConversationAction>(nullptr, "missing");
    actor->addAction(action);
    EXPECT_TRUE(game.globalFade().dialogPending());
    auto rejected = game.newAction<StartConversationAction>(nullptr, "unrelated");
    actor->addAction(rejected);
    rejected->execute(rejected, *actor, 0);
    EXPECT_TRUE(game.globalFade().heldForDialog());
    action->execute(action, *actor, 0);
    EXPECT_FALSE(game.globalFade().heldForDialog());
    EXPECT_TRUE(action->isCompleted());
    game.globalFade().holdForDialog();
    auto cancelled = game.newAction<StartConversationAction>(nullptr, "cancelled");
    actor->addAction(cancelled);
    actor->clearAllActions(true);
    EXPECT_FALSE(game.globalFade().heldForDialog());
    EXPECT_FALSE(game.globalFade().dialogPending());
}

TEST(GlobalFadeGame, failed_resource_lookup_only_releases_a_current_accepted_attempt) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newPlaceable();
    game.globalFade().holdForDialog();
    auto ticket = game.globalFade().admitDialog();
    game.startDialog(owner, "missing"); // no admission: cannot release ticket's H
    EXPECT_TRUE(game.globalFade().heldForDialog());
    game.startDialog(owner, "missing", ticket);
    EXPECT_FALSE(game.globalFade().heldForDialog());
    EXPECT_FALSE(game.globalFade().dialogPending());
    EXPECT_EQ(Game::Screen::None, game.currentScreen());
}

TEST(GlobalFadeGame, missing_parsed_dialog_is_an_owned_failed_start_before_screen_publication) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newPlaceable();
    game.globalFade().holdForDialog();
    auto ticket = game.globalFade().admitDialog();
    EXPECT_CALL(engine.resourceModule().gffs(), get("unparsed", resource::ResType::Dlg))
        .WillOnce(Return(resource::Gff::Builder().build()));
    game.startDialog(owner, "unparsed", ticket);
    EXPECT_FALSE(game.globalFade().heldForDialog());
    EXPECT_FALSE(game.globalFade().dialogPending());
    EXPECT_EQ(Game::Screen::None, game.currentScreen());
}

TEST(GlobalFadeGame, movie_execution_suspends_global_time_and_rebases_both_boundaries) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    auto movie = std::make_shared<NiceMock<movie::MockMovie>>();
    EXPECT_CALL(engine.resourceModule().movies(), get("fade_movie")).WillOnce(Return(movie));
    game.globalFade().request(Direction::Out, 0, 2);
    auto opacity = game.globalFade().opacity();
    game.playVideo("fade_movie");
    EXPECT_TRUE(game.consumeTimingDiscontinuity());
    EXPECT_FALSE(game.consumeTimingDiscontinuity());
    EXPECT_CALL(*movie, update(1)).Times(2);
    EXPECT_CALL(*movie, render()).Times(1);
    EXPECT_CALL(*movie, isFinished()).WillOnce(Return(false)).WillOnce(Return(true));
    game.update(1);
    game.render(); // movie-only path must bypass the global compositor
    EXPECT_FLOAT_EQ(opacity, game.globalFade().opacity());
    game.globalFade().lockUntilScript();
    EXPECT_FALSE(game.globalFade().request(Direction::In, 0, 0, {}, Source::Script));
    EXPECT_FALSE(game.globalFade().locked());
    game.update(1);
    EXPECT_FALSE(game.movie());
    EXPECT_TRUE(game.consumeTimingDiscontinuity());
    EXPECT_FLOAT_EQ(opacity, game.globalFade().opacity());
    game.update(.5f);
    EXPECT_NEAR(opacity + .25f, game.globalFade().opacity(), 1e-6f);
}

} // namespace
