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
#include "reone/game/action/startconversation.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

TEST(Action, use_talent_dispatch_to_use_feat) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    auto target = game.newCreature();
    auto talent = game.newTalent(TalentType::Feat, static_cast<int>(FeatType::PowerAttack));
    auto action = game.newAction<UseTalentOnObjectAction>(std::move(talent), target);
    auto subAction = action->subAction();
    ASSERT_TRUE(subAction);

    EXPECT_FALSE(action->isCompleted());
    EXPECT_FALSE(subAction->isCompleted());

    // Cycle through combat states
    for (int i = 0; i < 10; ++i) {
        action->execute(action, *target, 1.0f);
        game.combat().update(2.0f);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_TRUE(subAction->isCompleted());
}

TEST(Action, use_talent_dispatch_to_cast_spell) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto spell = std::make_shared<Spell>();
    spell->type = SpellType::LightSaberThrow;
    spell->castTime = 1.0f;
    spell->conjTime = 1.0f;

    EXPECT_CALL(engine.gameModule().spells(), get(SpellType::LightSaberThrow))
        .Times(AnyNumber())
        .WillRepeatedly(Return(spell));

    auto player = game.newCreature();
    auto target = game.newCreature();
    auto talent = game.newTalent(TalentType::Spell, static_cast<int>(SpellType::LightSaberThrow));
    auto action = game.newAction<UseTalentOnObjectAction>(std::move(talent), target);
    auto subAction = action->subAction();
    ASSERT_TRUE(subAction);

    EXPECT_FALSE(action->isCompleted());
    EXPECT_FALSE(subAction->isCompleted());

    // Cycle through combat states
    for (int i = 0; i < 10; ++i) {
        action->execute(action, *target, 1.0f);
        game.combat().update(2.0f);
    }

    EXPECT_TRUE(action->isCompleted());
    EXPECT_TRUE(subAction->isCompleted());
}

namespace {

constexpr char kScriptedDialog[] = "hk50";

// A conversation-start action queued on a speaker and aimed at a listener, in
// the shape scripts use: a named dialogue, as K2 103PER k_102exit and
// a_hk50forcedlg both do.
//
// The speaker is left with an approach still to make, which is what makes the
// action's fate observable either way: discarded actions complete, whereas an
// honored one stays in progress while its speaker walks over. The guard runs
// before the approach, so it behaves the same whichever start range a script
// asks for.
std::shared_ptr<StartConversationAction> makeScriptedStartConversation(
    Game &game,
    const std::shared_ptr<Object> &listener) {

    return game.newAction<StartConversationAction>(
        listener,
        kScriptedDialog,
        /*privateConversation=*/false,
        resource::ConversationType::Cinematic,
        /*ignoreStartRange=*/false);
}

// Restricted movement keeps that approach unfinished for a whole test, so
// assertions stay on the action rather than on pathfinding.
std::shared_ptr<Creature> makeApproachingSpeaker(Game &game) {
    auto speaker = game.newCreature();
    speaker->setMovementRestricted(true);
    return speaker;
}

void setScreen(Game &game, Game::Screen screen) {
    TestGameModule::setCurrentScreen(game, static_cast<int>(screen));
}

} // namespace

// A. An already-running conversation rejects a newly executed
// ActionStartConversation. The action is dropped outright rather than beginning
// the approach it would otherwise begin, no dialogue is resolved or loaded, and
// the running conversation keeps the screen.
TEST(Action, start_conversation_is_discarded_while_a_conversation_is_active) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().gffs(), get(_, resource::ResType::Dlg)).Times(0);

    auto speaker = makeApproachingSpeaker(game);
    auto listener = game.newCreature();
    setScreen(game, Game::Screen::Conversation);
    ASSERT_TRUE(game.isConversationActive());

    auto action = makeScriptedStartConversation(game, listener);
    action->execute(action, *speaker, 1.0f);

    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(Game::Screen::Conversation, game.currentScreen());
    EXPECT_TRUE(game.isConversationActive());
}

// B. The rejected action is dropped, not deferred. Once the running
// conversation ends, the action must not come back to life - if it did, K2
// 103PER would answer its own recovery call the instant a_tlkhk50 finished and
// HK-50 would immediately re-greet the player.
TEST(Action, discarded_start_conversation_does_not_run_after_the_conversation_ends) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().gffs(), get(_, resource::ResType::Dlg)).Times(0);

    auto speaker = makeApproachingSpeaker(game);
    auto listener = game.newCreature();
    setScreen(game, Game::Screen::Conversation);

    auto action = makeScriptedStartConversation(game, listener);
    speaker->addAction(action);

    // Executed while the conversation owns the screen: rejected on the spot.
    speaker->update(1.0f);
    EXPECT_TRUE(action->isCompleted());

    // A completed action is reaped on the following update, leaving the queue
    // empty rather than holding the conversation call open.
    speaker->update(1.0f);
    EXPECT_TRUE(speaker->actions().empty());

    // The conversation ends and the queue is pumped again.
    setScreen(game, Game::Screen::InGame);
    ASSERT_FALSE(game.isConversationActive());
    speaker->update(1.0f);
    speaker->update(1.0f);

    EXPECT_TRUE(speaker->actions().empty());
    EXPECT_EQ(Game::Screen::InGame, game.currentScreen());
}

// C. The complement of A, and the check that the guard rejects nothing it
// should not: with no conversation running, the very same action survives and
// its speaker sets about approaching the listener instead of being discarded.
TEST(Action, start_conversation_survives_when_no_conversation_is_active) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().gffs(), get(_, resource::ResType::Dlg)).Times(0);

    auto speaker = makeApproachingSpeaker(game);
    auto listener = game.newCreature();
    setScreen(game, Game::Screen::InGame);
    ASSERT_FALSE(game.isConversationActive());

    auto action = makeScriptedStartConversation(game, listener);
    action->execute(action, *speaker, 1.0f);

    EXPECT_FALSE(action->isCompleted());
}

// D. Routine 701 reports the same conversation-active state the guard uses.
TEST(Action, get_is_conversation_active_reports_the_shared_predicate) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    Routines routines(resource::GameID::TSL, &game, &engine.services());
    routines.init();
    script::Routine &routine = routines.get(701);
    ASSERT_EQ("GetIsConversationActive", routine.name());

    script::ExecutionContext ctx;

    setScreen(game, Game::Screen::InGame);
    EXPECT_EQ(0, routine.invoke({}, ctx).intValue);

    setScreen(game, Game::Screen::Conversation);
    EXPECT_EQ(1, routine.invoke({}, ctx).intValue);
}
