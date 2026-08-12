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

#include <set>

#include "../fixtures/engine.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"

// Attack animation selection stays internal to the action implementation. The
// variant is rolled inside the action, so calling these helpers is the only way
// to exercise a specific roll.
#include "../../src/libs/game/action/attackanimations.h"

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

namespace {

// Authored attack animations this selector may choose from. Variant 0 is not
// authored in any family, in either game. K2 authors further non-cinematic
// variants for some families, which this selector deliberately leaves unused.
const std::set<std::string> kAuthoredAttackAnims {
    "g1a1", "g1a2",                               // stun baton
    "g2a1", "g2a2", "m2a1", "m2a2",               // single sword
    "g3a1", "g3a2", "m3a1", "m3a2",               // double bladed sword
    "g4a1", "g4a2", "m4a1", "m4a2",               // dual swords
    "g8a1", "g8a2",                               // unarmed
    "c2a1", "c2a2", "c2a3", "c2a4", "c2a5",       // single sword duel
    "c3a1", "c3a2", "c3a3", "c3a4", "c3a5",       // double bladed duel
    "c4a1", "c4a2", "c4a3", "c4a4", "c4a5",       // dual swords duel
    "c10a1", "c10a2", "c10a3", "c10a4", "c10a5"}; // complex unarmed duel

constexpr int kFirstVariantRoll = 1;
constexpr int kLastVariantRoll = 5;

} // namespace

TEST(AttackAnimation, non_cinematic_melee_attacks_never_request_variant_zero) {
    const CreatureWieldType wields[] {
        CreatureWieldType::SingleSword,
        CreatureWieldType::DoubleBladedSword,
        CreatureWieldType::DualSwords};

    for (auto wield : wields) {
        for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
            // A door or a placeable has no wield of its own.
            std::string generic = getMeleeAttackAnim(wield, CreatureWieldType::None, roll, /*duel=*/false);
            // A creature attacked outside a duel.
            std::string monster = getMeleeAttackAnim(wield, CreatureWieldType::SingleSword, roll, /*duel=*/false);
            std::string stunBaton = getStunBatonAttackAnim(roll);
            std::string unarmed = getUnarmedAttackAnim(
                CreatureWieldType::HandToHand, CreatureWieldType::None, roll, /*duel=*/false);

            for (const std::string &anim : {generic, monster, stunBaton, unarmed}) {
                EXPECT_THAT(anim, Not(EndsWith("a0"))) << "roll " << roll;
                EXPECT_EQ(1u, kAuthoredAttackAnims.count(anim)) << anim << " is not an authored animation";
            }
        }
    }
}

TEST(AttackAnimation, a_roll_of_three_selects_an_authored_variant) {
    // A roll of 3 used to reduce to variant 0, which exists in no animation
    // family, so the attacker played nothing at all.
    EXPECT_EQ("g1a1", getStunBatonAttackAnim(3));
    EXPECT_EQ("g2a1", getMeleeAttackAnim(CreatureWieldType::SingleSword, CreatureWieldType::None, 3, /*duel=*/false));
    EXPECT_EQ("m2a1", getMeleeAttackAnim(CreatureWieldType::SingleSword, CreatureWieldType::SingleSword, 3, /*duel=*/false));
    EXPECT_EQ("g8a1", getUnarmedAttackAnim(CreatureWieldType::HandToHand, CreatureWieldType::None, 3, /*duel=*/false));
}

TEST(AttackAnimation, stun_baton_attacks_select_authored_variants) {
    // Every roll has to land on an animation the models author.
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getStunBatonAttackAnim(roll));
    }

    EXPECT_EQ((std::set<std::string> {"g1a1", "g1a2"}), selected);
}

TEST(AttackAnimation, generic_single_sword_attacks_select_authored_variants) {
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getMeleeAttackAnim(
            CreatureWieldType::SingleSword, CreatureWieldType::None, roll, /*duel=*/false));
    }

    EXPECT_EQ((std::set<std::string> {"g2a1", "g2a2"}), selected);
}

TEST(AttackAnimation, non_duel_creature_attacks_select_authored_monster_variants) {
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getMeleeAttackAnim(
            CreatureWieldType::SingleSword, CreatureWieldType::SingleSword, roll, /*duel=*/false));
    }

    EXPECT_EQ((std::set<std::string> {"m2a1", "m2a2"}), selected);
}

TEST(AttackAnimation, unarmed_attacks_select_authored_variants) {
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getUnarmedAttackAnim(
            CreatureWieldType::HandToHand, CreatureWieldType::None, roll, /*duel=*/false));
        // A complex unarmed attacker outside a duel falls back to the same set.
        selected.insert(getUnarmedAttackAnim(
            CreatureWieldType::HandToHandComplex, CreatureWieldType::SingleSword, roll, /*duel=*/false));
    }

    EXPECT_EQ((std::set<std::string> {"g8a1", "g8a2"}), selected);
}

TEST(AttackAnimation, duels_still_select_all_five_cinematic_variants) {
    std::set<std::string> selected;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        selected.insert(getMeleeAttackAnim(
            CreatureWieldType::SingleSword, CreatureWieldType::DualSwords, roll, /*duel=*/true));
    }

    EXPECT_EQ((std::set<std::string> {"c2a1", "c2a2", "c2a3", "c2a4", "c2a5"}), selected);

    // Complex unarmed duels keep their own cinematic variants.
    std::set<std::string> unarmed;
    for (int roll = kFirstVariantRoll; roll <= kLastVariantRoll; ++roll) {
        unarmed.insert(getUnarmedAttackAnim(
            CreatureWieldType::HandToHandComplex, CreatureWieldType::HandToHandComplex, roll, /*duel=*/true));
    }

    EXPECT_EQ((std::set<std::string> {"c10a1", "c10a2", "c10a3", "c10a4", "c10a5"}), unarmed);
}
