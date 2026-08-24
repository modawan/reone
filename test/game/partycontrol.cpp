/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

/**
 * A game plus its routine table, so SwitchPlayerCharacter can be called the way
 * the shipped prologue and Leviathan scripts call it.
 */
class ControlHarness : boost::noncopyable {
public:
    explicit ControlHarness(GameID gameId) :
        _game(gameId, "", testEngine().options(), testEngine().services(), _console),
        _routines(gameId, &_game, &testEngine().services()) {
        _routines.init();
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
};

class PartyControl : public ::testing::TestWithParam<GameID> {};

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

INSTANTIATE_TEST_SUITE_P(Games, PartyControl,
                         testing::Values(GameID::KotOR, GameID::TSL));
