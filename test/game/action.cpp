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
#include <set>

#include "../fixtures/engine.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
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

TEST(CombatRoundReferences, live_participants_resolve_and_reach_execution_state) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto action = game.newAction<AttackObjectAction>(target);

    const CombatRound &round = game.combat().addAction(action, *attacker);
    ASSERT_EQ(1u, round.actions.size());
    EXPECT_EQ(attacker, round.actions.front().attacker.resolve());
    EXPECT_EQ(target, round.actions.front().target.resolve());

    game.combat().update(0.0f);

    EXPECT_TRUE(round.canExecute(*action));
    EXPECT_EQ(1u, game.combat().roundCount());
}

TEST(CombatRoundReferences, retired_target_prunes_round_and_cancels_without_damage) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    target->setCurrentHitPoints(12);
    auto action = game.newAction<AttackObjectAction>(target);
    const CombatRound &round = game.combat().addAction(action, *attacker);

    game.destroyRuntimeObjectGraph(target);

    ASSERT_TRUE(target);
    EXPECT_FALSE(target->isRuntimeLive());
    EXPECT_FALSE(round.actions.front().target.resolve());
    game.combat().update(0.0f);

    EXPECT_TRUE(action->isCancelled());
    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(12, target->currentHitPoints());
    EXPECT_EQ(0u, game.combat().roundCount());
}

TEST(CombatRoundReferences, retired_attacker_prunes_round_despite_strong_storage) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto action = game.newAction<AttackObjectAction>(target);
    const CombatRound &round = game.combat().addAction(action, *attacker);

    game.destroyRuntimeObjectGraph(attacker);

    ASSERT_TRUE(attacker);
    EXPECT_FALSE(attacker->isRuntimeLive());
    EXPECT_FALSE(round.actions.front().attacker.resolve());
    game.combat().update(0.0f);

    EXPECT_TRUE(action->isCancelled());
    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(0u, game.combat().roundCount());
}

TEST(CombatRoundReferences, dead_but_live_target_is_a_gameplay_condition) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    target->setCurrentHitPoints(1);
    auto action = game.newAction<AttackObjectAction>(target);
    const CombatRound &round = game.combat().addAction(action, *attacker);

    target->damage(std::numeric_limits<int>::max(), attacker);
    ASSERT_TRUE(target->isDead());
    ASSERT_TRUE(target->isRuntimeLive());
    game.combat().update(0.0f);

    EXPECT_EQ(target, round.actions.front().target.resolve());
    EXPECT_FALSE(action->isCancelled());
    EXPECT_EQ(1u, game.combat().roundCount());
}

TEST(CombatRoundReferences, action_dependencies_cancel_attack_and_feat_targets) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto attackTarget = game.newCreature();
    auto featTarget = game.newCreature();
    auto attack = game.newAction<AttackObjectAction>(attackTarget);
    auto feat = game.newAction<UseFeatAction>(FeatType::PowerAttack, featTarget);

    game.destroyRuntimeObjectGraph(attackTarget);
    game.destroyRuntimeObjectGraph(featTarget);
    ASSERT_FALSE(attack->runtimeDependenciesLive());
    ASSERT_FALSE(feat->runtimeDependenciesLive());

    attack->execute(attack, *attacker, 0.0f);
    feat->execute(feat, *attacker, 0.0f);

    EXPECT_TRUE(attack->isCancelled());
    EXPECT_TRUE(feat->isCancelled());
    EXPECT_FALSE(attack->target());
    EXPECT_FALSE(feat->target());
}

TEST(CombatRoundReferences, completed_or_dequeued_actions_do_not_keep_rounds_alive) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto queuedAttacker = game.newCreature();
    auto queuedTarget = game.newCreature();
    auto queued = game.newAction<AttackObjectAction>(queuedTarget);
    queuedAttacker->addAction(queued);
    game.combat().addAction(queued, *queuedAttacker);
    ASSERT_EQ(1u, game.combat().roundCount());

    queuedAttacker->clearAllActions(/*force=*/true);
    game.combat().update(0.0f);

    EXPECT_TRUE(queued->isCancelled());
    EXPECT_TRUE(queued->isCompleted());
    EXPECT_EQ(0u, game.combat().roundCount());

    auto completedAttacker = game.newCreature();
    auto completedTarget = game.newCreature();
    auto completed = game.newAction<AttackObjectAction>(completedTarget);
    game.combat().addAction(completed, *completedAttacker);
    completed->complete();

    game.combat().update(0.0f);

    EXPECT_EQ(0u, game.combat().roundCount());

    auto cancelledAttacker = game.newCreature();
    auto cancelledTarget = game.newCreature();
    auto cancelled = game.newAction<AttackObjectAction>(cancelledTarget);
    game.combat().addAction(cancelled, *cancelledAttacker);
    cancelled->markCancelled();

    game.combat().update(0.0f);

    EXPECT_TRUE(cancelled->isCompleted());
    EXPECT_EQ(0u, game.combat().roundCount());
}

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

// A leader far enough away that a follower still has ground to cover, so an
// honored FollowLeader stays in progress rather than completing on arrival.
constexpr float kFarFromLeader = 10.0f * kDefaultFollowDistance;

} // namespace

// E. FollowLeader has no leader to follow while the party is empty - before it
// is first populated, after a module transition resets it, and once the last
// member is removed. The action is dropped there instead of dereferencing the
// absent leader, and nothing else is promoted in its place.
TEST(Action, follow_leader_is_discarded_when_the_party_has_no_leader) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto follower = makeApproachingSpeaker(game);
    ASSERT_TRUE(game.party().isEmpty());
    ASSERT_FALSE(game.party().getLeader());

    auto action = game.newAction<FollowLeaderAction>();
    action->execute(action, *follower, 1.0f);

    EXPECT_TRUE(action->isCompleted());
}

// F. The complement of E, and the check that the guard drops nothing it should
// not: with a leader in the party the action survives and its follower sets
// about the approach instead of being discarded.
TEST(Action, follow_leader_survives_when_the_party_has_a_leader) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto leader = game.newCreature();
    leader->setPosition(glm::vec3(kFarFromLeader, 0.0f, 0.0f));
    game.party().addMember(kNpcPlayer, leader);
    ASSERT_EQ(leader, game.party().getLeader());

    auto follower = makeApproachingSpeaker(game);

    auto action = game.newAction<FollowLeaderAction>();
    action->execute(action, *follower, 1.0f);

    EXPECT_FALSE(action->isCompleted());
}

// G. Leader-present completion is unchanged too: a follower already within
// following distance of the leader finishes the action.
TEST(Action, follow_leader_completes_once_the_follower_is_with_the_leader) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto leader = game.newCreature();
    game.party().addMember(kNpcPlayer, leader);
    auto follower = game.newCreature();
    ASSERT_EQ(leader->position(), follower->position());

    auto action = game.newAction<FollowLeaderAction>();
    action->execute(action, *follower, 1.0f);

    EXPECT_TRUE(action->isCompleted());
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
