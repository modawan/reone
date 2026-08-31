/* Copyright (c) 2026 The reone project contributors */

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"
#include "reone/game/action.h"
#include "reone/game/effect.h"
#include "reone/game/effect/modifyattacks.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/room.h"
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/resource/types.h"
#include "reone/scene/collision.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

class CountingAction : public reone::game::Action {
public:
    CountingAction(Game &game, ServicesView &services, int &executions) :
        reone::game::Action(game, services, ActionType::Invalid),
        _executions(executions) {
    }

    void execute(
        std::shared_ptr<reone::game::Action> self,
        Object &actor,
        float dt) override {
        ++_executions;
        complete();
    }

private:
    int &_executions;
};

/**
 * A game plus its routine table, so SwitchPlayerCharacter can be called the way
 * the shipped prologue and Leviathan scripts call it.
 */
class ControlHarness : boost::noncopyable {
public:
    explicit ControlHarness(GameID gameId) :
        _game(gameId, "", testEngine().options(), testEngine().services(), _console),
        _routines(gameId, &_game, &testEngine().services()) {
        auto &director = testEngine().resourceModule().director();
        // testEngine() is process-global. Discard callbacks installed by a
        // completed fixture before this harness exercises RemoveMember's
        // retail save-before-control-transfer behavior.
        Mock::VerifyAndClear(&director);
        EXPECT_CALL(director, committedSaveWorkingState())
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this]() { return _committed; }));
        EXPECT_CALL(director, adoptSaveWorkingState(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](auto state) {
                _committed = std::move(state);
            }));
        _routines.init();
    }

    ~ControlHarness() {
        Mock::VerifyAndClear(&testEngine().resourceModule().director());
    }

    /** A brand new game: no save has ever been loaded. */
    std::shared_ptr<Creature> startFreshGame() {
        auto player = _game.newCreature();
        _game.party().addMember(kNpcPlayer, player);
        _game.party().setPlayer(player);
        _game.party().setActualPlayer(player);
        return player;
    }

    std::shared_ptr<Creature> addRosterNpc(int npc) {
        auto creature = _game.newCreature();
        _game.party().addAvailableMember(npc, creature);
        return creature;
    }

    std::shared_ptr<Creature> addCompanion(int npc) {
        auto creature = addRosterNpc(npc);
        _game.party().addMember(npc, creature);
        return creature;
    }

    int switchTo(int npc) {
        Routine &routine = _routines.get(
            _routines.getIndexByName("SwitchPlayerCharacter"));
        ExecutionContext execution;
        execution.routines = &_routines;
        return routine.invoke({Variable::ofInt(npc)}, execution).intValue;
    }

    Party &party() { return _game.party(); }

    /** How many member entries name this creature. */
    size_t occurrences(const std::shared_ptr<Creature> &creature) {
        size_t count = 0;
        for (const auto &member : _game.party().members()) {
            if (member.creature == creature) {
                ++count;
            }
        }
        return count;
    }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
    std::shared_ptr<const SaveWorkingState> _committed {
        std::make_shared<const SaveWorkingState>()};
};

class PartyControl : public ::testing::TestWithParam<GameID> {};

/** A real Area around SwitchPlayerCharacter, with no resource load required. */
class AreaControlHarness : boost::noncopyable {
public:
    explicit AreaControlHarness(GameID gameId) :
        _room("control", glm::vec3(0.0f), nullptr, nullptr, nullptr) {

        _engine.init();
        ON_CALL(_engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(_sceneGraph));
        ON_CALL(_sceneGraph, testElevation(_, _))
            .WillByDefault(Invoke([this](
                                      const glm::vec3 &position,
                                      scene::Collision &collision) {
                collision.intersection = position;
                collision.user = &_room;
                return true;
            }));

        _game = std::make_unique<Game>(
            gameId, "", _engine.options(), _engine.services(), _console);
        auto &director = _engine.resourceModule().director();
        EXPECT_CALL(director, committedSaveWorkingState())
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this]() { return _committed; }));
        EXPECT_CALL(director, adoptSaveWorkingState(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](auto state) {
                _committed = std::move(state);
            }));

        _routines = std::make_unique<Routines>(
            gameId, _game.get(), &_engine.services());
        _routines->init();

        _player = _game->newCreature();
        _area = _game->newArea();
        TestGameModule::configureModuleSnapshot(
            *_game, _area, _player, "control_module", "control_area");

        _companion = _game->newCreature();
        _game->party().addAvailableMember(0, _companion);
        _game->party().addMember(0, _companion);
        _area->loadParty(glm::vec3(2.0f, 3.0f, 0.0f), 0.25f);
    }

    int switchTo(int npc) {
        Routine &routine = _routines->get(
            _routines->getIndexByName("SwitchPlayerCharacter"));
        ExecutionContext execution;
        execution.routines = _routines.get();
        return routine.invoke({Variable::ofInt(npc)}, execution).intValue;
    }

    Game &game() { return *_game; }
    Area &area() { return *_area; }
    Room &room() { return _room; }
    const std::shared_ptr<Creature> &player() const { return _player; }
    const std::shared_ptr<Creature> &companion() const { return _companion; }

private:
    TestEngine _engine;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    Room _room;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _player;
    std::shared_ptr<Creature> _companion;
    std::shared_ptr<const SaveWorkingState> _committed {
        std::make_shared<const SaveWorkingState>()};
};

} // namespace

TEST_P(PartyControl, aFreshGameEstablishesTheCanonicalPlayer) {
    // given nothing but character generation. Until this holds there is no
    // canonical PC for a script to hand control back to.
    ControlHarness harness(GetParam());
    auto player = harness.startFreshGame();

    EXPECT_EQ(player, harness.party().actualPlayer());
    EXPECT_EQ(player, harness.party().player());
    EXPECT_EQ(kNpcPlayer, harness.party().controlledNpc());
}

TEST_P(PartyControl, controlPassesToARosterNpcAndBackOnAFreshGame) {
    // given the shipped K2 prologue chain, which never loads a save:
    //   AddAvailableNPCByTemplate(8, "p_t3m4") -> SwitchPlayerCharacter(8)
    //   ... -> SwitchPlayerCharacter(-1) -> StartNewModule("101PER")
    ControlHarness harness(GetParam());
    auto player = harness.startFreshGame();
    auto t3 = harness.addRosterNpc(8);

    ASSERT_EQ(1, harness.switchTo(8));
    EXPECT_EQ(t3, harness.party().player());
    EXPECT_EQ(t3, harness.party().getLeader());
    EXPECT_EQ(8, harness.party().controlledNpc());

    // then handing control back restores the canonical PC rather than
    // transferring the stand-in into the next module
    ASSERT_EQ(1, harness.switchTo(kNpcPlayer));
    EXPECT_EQ(player, harness.party().player());
    EXPECT_EQ(player, harness.party().getLeader());
    EXPECT_EQ(kNpcPlayer, harness.party().controlledNpc());
}

TEST_P(PartyControl, unrelatedCompanionsSurviveAControlChange) {
    // given a companion travelling with the player, as on the Leviathan
    ControlHarness harness(GetParam());
    auto player = harness.startFreshGame();
    auto companion = harness.addCompanion(0);
    auto breaker = harness.addRosterNpc(5);

    ASSERT_EQ(1, harness.switchTo(5));
    EXPECT_EQ(1u, harness.occurrences(companion));
    EXPECT_TRUE(harness.party().isMember(0));

    ASSERT_EQ(1, harness.switchTo(kNpcPlayer));
    EXPECT_EQ(player, harness.party().getLeader());
    EXPECT_EQ(1u, harness.occurrences(companion));
    EXPECT_TRUE(harness.party().isMember(0));
}

TEST_P(PartyControl, takingControlOfACompanionLeavesItInThePartyOnlyOnce) {
    // given the incoming actor is already an active companion
    ControlHarness harness(GetParam());
    harness.startFreshGame();
    auto companion = harness.addCompanion(0);

    ASSERT_EQ(1, harness.switchTo(0));

    EXPECT_EQ(companion, harness.party().player());
    EXPECT_EQ(companion, harness.party().getLeader());
    EXPECT_EQ(1u, harness.occurrences(companion));
}

TEST_P(PartyControl, switchingToTheCurrentActorIsANoOp) {
    ControlHarness harness(GetParam());
    auto player = harness.startFreshGame();

    // already the canonical PC
    EXPECT_EQ(1, harness.switchTo(kNpcPlayer));
    EXPECT_EQ(player, harness.party().player());
    EXPECT_EQ(1u, harness.party().members().size());

    auto npc = harness.addRosterNpc(4);
    ASSERT_EQ(1, harness.switchTo(4));
    EXPECT_EQ(1, harness.switchTo(4));
    EXPECT_EQ(npc, harness.party().player());
    EXPECT_EQ(1u, harness.occurrences(npc));
}

TEST_P(PartyControl, switchingToAnUnknownNpcChangesNothing) {
    ControlHarness harness(GetParam());
    auto player = harness.startFreshGame();

    EXPECT_EQ(0, harness.switchTo(9));
    EXPECT_EQ(player, harness.party().player());
    EXPECT_EQ(kNpcPlayer, harness.party().controlledNpc());
}

TEST_P(PartyControl, sameAreaControlSwitchPreservesRuntimeExecutionAndEffects) {
    AreaControlHarness harness(GetParam());
    auto player = harness.player();
    auto companion = harness.companion();

    int queuedExecutions = 0;
    int delayedExecutions = 0;
    auto queued = harness.game().newAction<CountingAction>(queuedExecutions);
    auto delayed = harness.game().newAction<CountingAction>(delayedExecutions);
    player->addAction(queued);
    player->delayAction(delayed, 1.0f);

    player->applyEffect(
        std::make_shared<ModifyAttacksEffect>(1), DurationType::Permanent);
    EffectInstance referencedEffect;
    referencedEffect.effect = std::make_shared<Effect>(EffectType::Invalid);
    referencedEffect.id = harness.game().allocateEffectId();
    referencedEffect.subType = static_cast<uint16_t>(DurationType::Permanent);
    referencedEffect.creatorId = companion->id();
    referencedEffect.objectParameters[0] = companion->id();
    ASSERT_TRUE(harness.game().bindEffectCreator(referencedEffect));
    ASSERT_TRUE(player->restoreEffect(std::move(referencedEffect)));
    ASSERT_EQ(1, player->modifiedAttacks());
    ASSERT_EQ(2u, player->effects().size());

    TestGameModule::setAreaRuntimePath(*player, harness.area().pathfinder());
    player->startStuntMode();
    const auto generation = TestGameModule::savedGraphGeneration(harness.game());

    ASSERT_EQ(1, harness.switchTo(0));

    EXPECT_EQ(companion, harness.game().party().player());
    EXPECT_EQ(player, harness.game().party().actualPlayer());
    EXPECT_EQ(companion, harness.game().party().rosterCreature({RosterKind::Npc, 0}));
    EXPECT_EQ(generation, TestGameModule::savedGraphGeneration(harness.game()));
    EXPECT_EQ(1u, player->actions().size());
    EXPECT_EQ(1u, TestGameModule::delayedActionCount(*player));
    EXPECT_TRUE(TestGameModule::hasAreaRuntimePath(*player));
    EXPECT_TRUE(player->isStuntMode());
    EXPECT_EQ(1, player->modifiedAttacks());
    ASSERT_EQ(2u, player->effects().size());
    EXPECT_EQ(companion, player->effects()[1].boundCreator());
    EXPECT_EQ(companion, player->effects()[1].boundObjectParameter(0));

    player->update(0.5f);
    EXPECT_EQ(1, queuedExecutions);
    EXPECT_EQ(0, delayedExecutions);
    player->update(0.6f);
    EXPECT_EQ(1, delayedExecutions);
}

TEST_P(PartyControl, sameAreaControlSwitchDoesNotDetachOrDuplicateResidents) {
    AreaControlHarness harness(GetParam());
    auto player = harness.player();
    auto companion = harness.companion();

    ASSERT_EQ(&harness.room(), player->room());
    ASSERT_EQ(&harness.room(), companion->room());
    ASSERT_EQ(1u, harness.room().tenants().count(player.get()));
    ASSERT_EQ(1u, harness.room().tenants().count(companion.get()));

    const glm::vec3 outgoingPosition = player->position();
    ASSERT_EQ(1, harness.switchTo(0));

    const auto &creatures = harness.area().getObjectsByType(ObjectType::Creature);
    EXPECT_EQ(1, std::count(creatures.begin(), creatures.end(), player));
    EXPECT_EQ(1, std::count(creatures.begin(), creatures.end(), companion));
    EXPECT_EQ(outgoingPosition, companion->position());
    EXPECT_EQ(&harness.room(), player->room());
    EXPECT_EQ(&harness.room(), companion->room());
    EXPECT_EQ(1u, harness.room().tenants().count(player.get()));
    EXPECT_EQ(1u, harness.room().tenants().count(companion.get()));
}

INSTANTIATE_TEST_SUITE_P(Games, PartyControl,
                         testing::Values(GameID::KotOR, GameID::TSL));
