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

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/types.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

// An arbitrary companion slot, standing in for the kind of member TSL lets the
// player take control of while the canonical PC stays behind.
constexpr int kNpcCompanion = 4;

/**
 * A game plus the routine table it is bound to, so a party routine can be
 * called the way a compiled script calls it - through the registered table
 * rather than through the implementation function directly.
 */
class RoutineHarness : boost::noncopyable {
public:
    RoutineHarness(GameID gameId) :
        _game(gameId, "", testEngine().options(), testEngine().services(), _console),
        _routines(gameId, &_game, &testEngine().services()) {

        _routines.init();
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        return routine.invoke(args, execution);
    }

    Game &game() { return _game; }
    Routines &routines() { return _routines; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

} // namespace

// The ordinary case: the player character leads, and GetPartyLeader hands back
// that exact object. The retail Ebon Hawk boarding script compares this against
// GetEnteringObject, so an approximate answer is as useless as no answer.
TEST(PartyRoutines, get_party_leader_returns_the_current_leader) {
    RoutineHarness harness(GameID::TSL);
    auto leader = harness.game().newCreature();
    harness.game().party().addMember(kNpcPlayer, leader);
    harness.game().party().setPlayer(leader);
    ASSERT_EQ(leader, harness.game().party().getLeader());

    Variable result = harness.call("GetPartyLeader", {});

    EXPECT_EQ(VariableType::Object, result.type);
    EXPECT_EQ(leader->id(), result.objectId);
}

// TSL lets the player drive a companion while the canonical PC remains a
// distinct object. The routine reports whoever is actually being controlled,
// which is the party leader - not the PC.
//
// The leading member is seated directly rather than through SetPartyLeader,
// whose onLeaderChanged step plays a sound set and notifies the area, and so
// needs a loaded module. All this routine reads is which member leads, and
// that state is identical either way.
TEST(PartyRoutines, get_party_leader_follows_a_companion_leader_not_the_canonical_pc) {
    RoutineHarness harness(GameID::TSL);
    auto pc = harness.game().newCreature();
    auto companion = harness.game().newCreature();
    ASSERT_NE(pc->id(), companion->id());

    harness.game().party().addAvailableMember(kNpcCompanion, companion);
    harness.game().party().addMember(kNpcCompanion, companion);
    harness.game().party().addMember(kNpcPlayer, pc);
    harness.game().party().setPlayer(pc);
    ASSERT_EQ(companion, harness.game().party().getLeader());
    ASSERT_EQ(pc, harness.game().party().player());

    Variable result = harness.call("GetPartyLeader", {});

    EXPECT_EQ(companion->id(), result.objectId);
    EXPECT_NE(pc->id(), result.objectId);
}

// "Returns object Invalid on error" - nwscript.nss. With nobody in the party
// there is no leader to report, and the routine must say so rather than
// inventing one.
TEST(PartyRoutines, get_party_leader_is_object_invalid_without_a_leader) {
    RoutineHarness harness(GameID::TSL);
    ASSERT_TRUE(harness.game().party().isEmpty());

    Variable result = harness.call("GetPartyLeader", {});

    EXPECT_EQ(VariableType::Object, result.type);
    EXPECT_EQ(kObjectInvalid, result.objectId);
}

// Routine 845 is TSL-only. K1 content never calls it and K1's table must not
// grow an entry for it.
TEST(PartyRoutines, get_party_leader_is_registered_for_tsl_only) {
    RoutineHarness tsl(GameID::TSL);
    RoutineHarness kotor(GameID::KotOR);

    EXPECT_EQ(845, tsl.routines().getIndexByName("GetPartyLeader"));
    EXPECT_EQ(-1, kotor.routines().getIndexByName("GetPartyLeader"));
}

// The gate the Ebon Hawk boarding script actually runs: the object that walked
// into the trigger is compared for identity against the party leader. This is
// the comparison that silently failed while the routine was a stub.
TEST(PartyRoutines, get_party_leader_matches_the_entering_leader_but_not_a_bystander) {
    RoutineHarness harness(GameID::TSL);
    auto leader = harness.game().newCreature();
    auto bystander = harness.game().newCreature();
    harness.game().party().addMember(kNpcPlayer, leader);
    harness.game().party().setPlayer(leader);

    uint32_t reported = harness.call("GetPartyLeader", {}).objectId;

    EXPECT_EQ(leader->id(), reported);
    EXPECT_NE(bystander->id(), reported);
}
