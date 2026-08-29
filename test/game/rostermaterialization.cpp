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
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/gff.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

std::shared_ptr<Gff> creatureRecord(
    std::string tag,
    std::string name = "Companion") {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("Tag", std::move(tag)))
        .field(Gff::Field::newResRef("TemplateResRef", "p_companion"))
        .field(Gff::Field::newCExoLocString("FirstName", -1, std::move(name)))
        .field(Gff::Field::newCExoLocString("LastName", -1, ""))
        .field(Gff::Field::newDword("Appearance_Type", 1))
        .field(Gff::Field::newWord("PortraitId", 11))
        .field(Gff::Field::newByte("Gender", 0))
        .field(Gff::Field::newByte("Race", 6))
        .field(Gff::Field::newWord("SoundSetFile", 21))
        .build();
}

ByteBuffer encodeUtc(const Gff &gff) {
    return GffWriter(GffFileFormat::v32("UTC "), gff).toBytes();
}

std::shared_ptr<TwoDA> creatureAppearanceTable() {
    TwoDA::Builder builder;
    builder.columns({
        "modeltype", "walkdist", "rundist", "footsteptype",
        "envmap", "race", "racetex"});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Gff> decodeGff(const ByteBuffer &bytes) {
    ByteBuffer copy(bytes);
    MemoryInputStream stream(copy);
    GffReader reader(stream);
    reader.load();
    return reader.root();
}

class RosterHarness {
public:
    explicit RosterHarness(GameID gameId = GameID::TSL) :
        game(gameId, "", testEngine().options(), testEngine().services(), console),
        routines(gameId, &game, &testEngine().services()) {
        // Object tests share this mock and leave narrow saturated lookups
        // behind. Roster fixtures establish their own resource contract.
        testing::Mock::VerifyAndClear(
            &testEngine().resourceModule().gffs());
        ON_CALL(testEngine().sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sharedSceneGraph()));
        EXPECT_CALL(testEngine().resourceModule().twoDas(), get("appearance"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(creatureAppearanceTable()));
        EXPECT_CALL(testEngine().resourceModule().twoDas(), get("soundset"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(nullptr));
        EXPECT_CALL(
            testEngine().resourceModule().director(),
            committedSaveWorkingState())
            .Times(AnyNumber())
            .WillRepeatedly(Return(nullptr));
        EXPECT_CALL(
            testEngine().resourceModule().director(),
            adoptSaveWorkingState(_))
            .Times(AnyNumber());
        routines.init();
    }

    ~RosterHarness() {
        // testEngine() is process-global. Remove callbacks which capture this
        // harness's per-test save state before that state disappears.
        testing::Mock::VerifyAndClear(
            &testEngine().resourceModule().director());
    }

    static NiceMock<scene::MockSceneGraph> &sharedSceneGraph() {
        static NiceMock<scene::MockSceneGraph> graph;
        return graph;
    }

    std::shared_ptr<Creature> creature(std::string tag) {
        auto result = game.newCreature();
        result->setTag(std::move(tag));
        return result;
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        ExecutionContext execution;
        execution.routines = &routines;
        auto &routine = routines.get(routines.getIndexByName(name));
        return routine.invoke(args, execution);
    }

    StubConsole console;
    Game game;
    Routines routines;
};

} // namespace

TEST(RosterBinding, same_tag_npc_puppet_and_module_creature_coexist) {
    RosterHarness harness;
    auto npc = harness.creature("remote");
    auto puppet = harness.creature("remote");
    ASSERT_TRUE(harness.game.party().addAvailableMember(1, npc));
    ASSERT_TRUE(harness.game.party().addAvailablePuppet(0, puppet));
    ASSERT_TRUE(harness.game.party().addMember(1, npc));

    auto git = creatureRecord("remote", "Module Remote");
    auto context = SerializedIdentityContext::templateResource("test-area");
    auto moduleCreature = harness.game.newCreature(*git, context);
    moduleCreature->deserialize(*git, context);

    EXPECT_EQ(npc, harness.game.party().rosterCreature({RosterKind::Npc, 1}));
    EXPECT_EQ(npc, harness.game.party().getMemberByNPC(1));
    EXPECT_EQ(
        puppet,
        harness.game.party().rosterCreature({RosterKind::Puppet, 0}));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*moduleCreature));
    EXPECT_NE(npc, puppet);
    EXPECT_NE(npc, moduleCreature);
    EXPECT_NE(puppet, moduleCreature);
}

TEST(RosterBinding, same_tag_module_double_cannot_steal_upgraded_companion) {
    RosterHarness harness;
    auto companion = harness.creature("g0t0");
    companion->setLocalNumber(7, 137);
    ASSERT_TRUE(harness.game.party().addAvailableMember(3, companion));

    auto doubleCreature = harness.creature("g0t0");
    doubleCreature->setLocalNumber(7, 12);

    EXPECT_EQ(companion, harness.game.party().getAvailableMember(3));
    EXPECT_EQ(137, companion->getLocalNumber(7));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*doubleCreature));
}

TEST(RosterBinding, explicit_binding_uses_slot_not_tag_or_object_number) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[4] = true;
    harness.game.party().setPersistedState(state);
    auto first = harness.creature("first");
    auto exact = harness.creature("unrelated_tag");
    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 4}, first));

    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 4}, exact));

    EXPECT_EQ(exact, harness.game.party().getAvailableMember(4));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*first));
    EXPECT_EQ(
        (RosterIdentity {RosterKind::Npc, 4}),
        *harness.game.party().rosterIdentity(*exact));
}

TEST(RosterBinding, set_available_npc_id_binds_the_exact_script_object) {
    RosterHarness harness;
    auto &director = testEngine().resourceModule().director();
    auto committed = std::make_shared<const SaveWorkingState>();
    EXPECT_CALL(director, committedSaveWorkingState())
        .WillOnce(Return(committed));
    EXPECT_CALL(director, adoptSaveWorkingState(_))
        .WillOnce(Invoke([&committed](auto state) { committed = std::move(state); }));
    Party::PersistedState state;
    state.npcAvailable[4] = true;
    harness.game.party().setPersistedState(state);
    auto first = harness.creature("same_tag");
    auto exact = harness.creature("same_tag");
    first->setAppearance(111);
    exact->setAppearance(222);
    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 4}, first));

    harness.call(
        "SetAvailableNPCId",
        {Variable::ofInt(4), Variable::ofObject(exact->id())});

    EXPECT_EQ(exact, harness.game.party().getAvailableMember(4));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*first));

    harness.game.saveNpcState(4);
    auto saved = committed->find({"availnpc4", ResType::Utc});
    ASSERT_TRUE(saved);
    EXPECT_EQ(222u, decodeGff(saved->data)->getUint("Appearance_Type"));
}

TEST(RosterBinding, set_available_npc_id_can_explicitly_clear_binding) {
    RosterHarness harness;
    auto creature = harness.creature("companion");
    ASSERT_TRUE(harness.game.party().addAvailableMember(4, creature));

    harness.call(
        "SetAvailableNPCId",
        {Variable::ofInt(4), Variable::ofObject(kObjectInvalid)});

    EXPECT_TRUE(harness.game.party().isMemberAvailable(4));
    EXPECT_FALSE(harness.game.party().getAvailableMember(4));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*creature));
}

TEST(RosterBinding, one_runtime_creature_cannot_bind_two_logical_slots) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    state.npcAvailable[1] = true;
    harness.game.party().setPersistedState(state);
    auto creature = harness.creature("shared");

    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 0}, creature));
    EXPECT_FALSE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 1}, creature));
    EXPECT_FALSE(harness.game.party().rosterCreature({RosterKind::Npc, 1}));
}

TEST(RosterBinding, failed_runtime_binding_does_not_publish_availability) {
    RosterHarness harness;
    auto creature = harness.creature("companion");
    ASSERT_TRUE(harness.game.party().addAvailableMember(0, creature));

    EXPECT_FALSE(harness.game.party().addAvailableMember(1, creature));
    EXPECT_FALSE(harness.game.party().isMemberAvailable(1));
    EXPECT_EQ(creature, harness.game.party().getAvailableMember(0));
}

TEST(RosterBinding, puppet_rebinding_transfers_the_runtime_puppet_role) {
    RosterHarness harness;
    auto first = harness.creature("remote");
    auto replacement = harness.creature("remote");
    ASSERT_TRUE(harness.game.party().addAvailablePuppet(0, first));
    ASSERT_TRUE(first->isPuppet());

    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Puppet, 0}, replacement));

    EXPECT_FALSE(first->isPuppet());
    EXPECT_TRUE(replacement->isPuppet());
    EXPECT_FALSE(harness.game.party().rosterIdentity(*first));
    EXPECT_EQ(
        (RosterIdentity {RosterKind::Puppet, 0}),
        *harness.game.party().rosterIdentity(*replacement));
}

TEST(RosterBinding, canonical_player_cannot_be_claimed_by_roster_slot) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    auto player = harness.creature("player");
    harness.game.party().setPlayer(player);
    harness.game.party().setActualPlayer(player);

    EXPECT_FALSE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 0}, player));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*player));
}

TEST(RosterBinding, availability_is_partytable_state_not_binding_presence) {
    RosterHarness harness;
    auto creature = harness.creature("companion");
    ASSERT_TRUE(harness.game.party().addAvailableMember(0, creature));
    ASSERT_TRUE(harness.game.party().isMemberAvailable(0));
    ASSERT_TRUE(harness.game.party().clearRosterCreature(
        {RosterKind::Npc, 0}, creature.get()));

    EXPECT_TRUE(harness.game.party().isMemberAvailable(0));
    EXPECT_TRUE(harness.game.party().persistedState().npcAvailable[0]);
    EXPECT_FALSE(harness.game.party().getAvailableMember(0));

    EXPECT_TRUE(harness.game.party().removeAvailableMember(0));
    EXPECT_FALSE(harness.game.party().isMemberAvailable(0));
    EXPECT_FALSE(harness.game.party().persistedState().npcAvailable[0]);
}

TEST(RosterBinding, npc_selectability_is_distinct_persisted_partytable_state) {
    RosterHarness harness;

    EXPECT_EQ(
        -1,
        harness.call("GetNPCSelectability", {Variable::ofInt(4)}).intValue);

    ASSERT_TRUE(harness.game.party().setRosterAvailable(
        {RosterKind::Npc, 4}, true));
    EXPECT_EQ(
        1,
        harness.call("GetNPCSelectability", {Variable::ofInt(4)}).intValue);

    harness.call(
        "SetNPCSelectability",
        {Variable::ofInt(4), Variable::ofInt(0)});

    EXPECT_TRUE(harness.game.party().isMemberAvailable(4));
    EXPECT_FALSE(harness.game.party().isRosterSelectable(
        {RosterKind::Npc, 4}));
    EXPECT_EQ(
        0,
        harness.call("GetNPCSelectability", {Variable::ofInt(4)}).intValue);
}

TEST(RosterBinding, loading_partytable_state_clears_transient_object_bindings) {
    RosterHarness harness;
    auto old = harness.creature("old_runtime");
    ASSERT_TRUE(harness.game.party().addAvailableMember(0, old));
    Party::PersistedState loaded;
    loaded.npcAvailable[0] = true;

    harness.game.party().loadPersistedState(loaded);

    EXPECT_TRUE(harness.game.party().isMemberAvailable(0));
    EXPECT_FALSE(harness.game.party().getAvailableMember(0));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*old));
}

TEST(RosterBinding, add_member_binds_the_exact_available_creature) {
    RosterHarness harness;
    auto stored = harness.creature("stored");
    auto selected = harness.creature("selected");
    ASSERT_TRUE(harness.game.party().addAvailableMember(2, stored));

    ASSERT_TRUE(harness.game.party().addMember(2, selected));

    EXPECT_EQ(selected, harness.game.party().getMemberByNPC(2));
    EXPECT_EQ(selected, harness.game.party().getAvailableMember(2));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*stored));
}

TEST(RosterBinding, add_party_member_routine_publishes_membership_and_binding) {
    RosterHarness harness;
    auto detached = harness.creature("detached");
    auto moduleCreature = harness.creature("module");
    ASSERT_TRUE(harness.game.party().addAvailableMember(2, detached));

    auto result = harness.call(
        "AddPartyMember",
        {Variable::ofInt(2), Variable::ofObject(moduleCreature->id())});

    EXPECT_EQ(1, result.intValue);
    EXPECT_EQ(moduleCreature, harness.game.party().getMemberByNPC(2));
    EXPECT_EQ(moduleCreature, harness.game.party().getAvailableMember(2));
}

TEST(RosterBinding, add_member_rejects_an_unavailable_slot) {
    RosterHarness harness;
    auto creature = harness.creature("unavailable");

    EXPECT_FALSE(harness.game.party().addMember(0, creature));
    EXPECT_FALSE(harness.game.party().isMember(0));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*creature));
}

TEST(RosterBinding, controlled_slot_binding_keeps_player_view_coherent) {
    RosterHarness harness;
    Party::PersistedState state;
    state.controlledNpc = 2;
    state.npcAvailable[2] = true;
    harness.game.party().setPersistedState(state);
    auto first = harness.creature("controlled_first");
    auto replacement = harness.creature("controlled_replacement");
    harness.game.party().setPlayer(first);
    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 2}, first));

    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 2}, replacement));

    EXPECT_EQ(replacement, harness.game.party().player());
    EXPECT_EQ(
        replacement,
        harness.game.party().rosterCreature({RosterKind::Npc, 2}));
}

TEST(RosterBinding, removing_active_member_preserves_availability_and_binding) {
    RosterHarness harness;
    auto creature = harness.creature("companion");
    ASSERT_TRUE(harness.game.party().addAvailableMember(0, creature));
    ASSERT_TRUE(harness.game.party().addMember(0, creature));

    ASSERT_TRUE(harness.game.party().removeMember(0));

    EXPECT_FALSE(harness.game.party().isMember(0));
    EXPECT_TRUE(harness.game.party().isMemberAvailable(0));
    EXPECT_EQ(creature, harness.game.party().getAvailableMember(0));
}

TEST(RosterBinding, controlled_member_cannot_be_removed_as_a_follower) {
    RosterHarness harness;
    auto canonical = harness.creature("player");
    auto controlled = harness.creature("controlled");
    harness.game.party().setPlayer(canonical);
    harness.game.party().setActualPlayer(canonical);
    ASSERT_TRUE(harness.game.party().addAvailableMember(2, controlled));
    harness.game.party().setControlledMember(2, controlled);

    EXPECT_FALSE(harness.game.party().removeMember(2));
    EXPECT_TRUE(harness.game.party().isMember(2));
    EXPECT_EQ(controlled, harness.game.party().player());
    EXPECT_EQ(controlled, harness.game.party().getAvailableMember(2));
}

TEST(RosterBinding, controlling_active_companion_retires_its_assigned_puppet) {
    RosterHarness harness;
    auto canonical = harness.creature("player");
    auto controlled = harness.creature("controlled");
    auto puppet = harness.creature("remote");
    harness.game.party().setPlayer(canonical);
    harness.game.party().setActualPlayer(canonical);
    ASSERT_TRUE(harness.game.party().addMember(kNpcPlayer, canonical));
    ASSERT_TRUE(harness.game.party().addAvailableMember(2, controlled));
    ASSERT_TRUE(harness.game.party().addAvailablePuppet(0, puppet));
    ASSERT_TRUE(harness.game.party().assignPuppet(0, 2));
    ASSERT_TRUE(harness.game.party().addMember(2, controlled));
    ASSERT_TRUE(harness.game.party().isPuppet(0));

    harness.game.party().setControlledMember(2, controlled);

    EXPECT_EQ(controlled, harness.game.party().player());
    EXPECT_EQ(2, harness.game.party().controlledNpc());
    EXPECT_TRUE(harness.game.party().isMember(2));
    EXPECT_FALSE(harness.game.party().isPuppet(0));
    EXPECT_FALSE(harness.game.party().getAvailablePuppet(0));
    EXPECT_TRUE(harness.game.party().isRosterAvailable({RosterKind::Puppet, 0}));
}

TEST(RosterBinding, runtime_destruction_clears_binding_not_availability) {
    RosterHarness harness;
    auto creature = harness.creature("companion");
    ASSERT_TRUE(harness.game.party().addAvailableMember(0, creature));
    ASSERT_TRUE(harness.game.party().addMember(0, creature));

    harness.game.destroyRuntimeObjectGraph(creature);

    EXPECT_FALSE(harness.game.party().rosterCreature({RosterKind::Npc, 0}));
    EXPECT_FALSE(harness.game.party().isMember(0));
    EXPECT_TRUE(harness.game.party().isMemberAvailable(0));
    EXPECT_FALSE(harness.game.getObjectById(creature->id()));
}

TEST(RosterBinding, puppet_kill_clears_binding_and_active_role_not_availability) {
    RosterHarness harness;
    auto puppet = harness.creature("remote");
    ASSERT_TRUE(harness.game.party().addAvailablePuppet(0, puppet));
    ASSERT_TRUE(harness.game.party().addPuppet(0, puppet));

    EXPECT_TRUE(harness.game.killRosterCreature({RosterKind::Puppet, 0}));

    EXPECT_FALSE(harness.game.party().rosterCreature({RosterKind::Puppet, 0}));
    EXPECT_FALSE(harness.game.party().isPuppet(0));
    EXPECT_TRUE(harness.game.party().isRosterAvailable({RosterKind::Puppet, 0}));
    EXPECT_FALSE(puppet->isPuppet());
    EXPECT_FALSE(harness.game.getObjectById(puppet->id()));
}

TEST(RosterBinding, available_unbound_slot_materializes_from_detached_record) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[5] = true;
    harness.game.party().setPersistedState(state);
    auto record = creatureRecord("lazy_npc");
    replaceSaveField(
        *record, Gff::Field::newDword("ObjectId", 136));
    auto bytes = encodeUtc(*record);
    EXPECT_CALL(
        testEngine().resourceModule().director(),
        findSaveWorking(ResourceId("availnpc5", ResType::Utc)))
        .WillOnce(Return(Resource {bytes}));

    auto creature = harness.game.party().getAvailableMember(5, true);

    ASSERT_TRUE(creature);
    EXPECT_EQ("lazy_npc", creature->tag());
    EXPECT_EQ(creature, harness.game.party().getAvailableMember(5));
    ASSERT_TRUE(creature->saveRecordProvenance());
    ASSERT_TRUE(creature->saveRecordProvenance()->identity);
    EXPECT_EQ(
        SerializedIdentityDomain::DetachedRecord,
        creature->saveRecordProvenance()->identity->context.domain);
}

TEST(RosterBinding, unavailable_stale_record_is_never_materialized) {
    RosterHarness harness;
    Party::PersistedState state;
    harness.game.party().setPersistedState(state);

    EXPECT_FALSE(harness.game.party().getAvailableMember(5, true));
    EXPECT_FALSE(harness.game.party().rosterCreature({RosterKind::Npc, 5}));
}

TEST(RosterBinding, add_by_object_persists_detached_state_without_binding_input) {
    RosterHarness harness;
    auto &director = testEngine().resourceModule().director();
    auto committed = std::make_shared<const SaveWorkingState>();
    EXPECT_CALL(director, committedSaveWorkingState())
        .WillOnce(Return(committed));
    EXPECT_CALL(director, adoptSaveWorkingState(_))
        .WillOnce(Invoke([&committed](auto state) { committed = std::move(state); }));
    auto moduleCreature = harness.creature("module_companion");
    moduleCreature->setLocalNumber(7, 111);

    auto result = harness.call(
        "AddAvailableNPCByObject",
        {Variable::ofInt(6), Variable::ofObject(moduleCreature->id())});

    EXPECT_EQ(1, result.intValue);
    EXPECT_TRUE(harness.game.party().isMemberAvailable(6));
    EXPECT_FALSE(harness.game.party().rosterCreature({RosterKind::Npc, 6}));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*moduleCreature));
    EXPECT_TRUE(committed->contains({"availnpc6", ResType::Utc}));
}

TEST(RosterBinding, rebuild_party_table_is_the_explicit_authored_tag_operation) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[3] = true;
    state.puppetAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    auto area = harness.game.newArea();
    auto gotoCreature = harness.creature("G0T0");
    auto remote = harness.creature("remote");
    auto unrelatedRemote = harness.creature("remote");
    area->add(gotoCreature);
    area->add(remote);
    area->add(unrelatedRemote);
    TestGameModule::configureModuleSnapshot(
        harness.game, area, nullptr, "003ebo", "003ebo");

    harness.call("RebuildPartyTable", {});

    EXPECT_EQ(
        gotoCreature,
        harness.game.party().rosterCreature({RosterKind::Npc, 3}));
    EXPECT_EQ(
        remote,
        harness.game.party().rosterCreature({RosterKind::Puppet, 0}));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*unrelatedRemote));
}

TEST(RosterBinding, add_available_pup_by_object_writes_detached_record_only) {
    RosterHarness harness;
    auto &director = testEngine().resourceModule().director();
    auto committed = std::make_shared<const SaveWorkingState>();
    EXPECT_CALL(director, committedSaveWorkingState())
        .WillOnce(Return(committed));
    EXPECT_CALL(director, adoptSaveWorkingState(_))
        .WillOnce(Invoke([&committed](auto state) { committed = std::move(state); }));
    auto modulePuppet = harness.creature("remote");

    auto result = harness.call(
        "AddAvailablePUPByObject",
        {Variable::ofInt(0), Variable::ofObject(modulePuppet->id())});

    EXPECT_EQ(1, result.intValue);
    EXPECT_TRUE(harness.game.party().isRosterAvailable({RosterKind::Puppet, 0}));
    EXPECT_FALSE(harness.game.party().rosterCreature({RosterKind::Puppet, 0}));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*modulePuppet));
    EXPECT_TRUE(committed->contains({"availpup0", ResType::Utc}));
}

TEST(RosterBinding, detached_and_module_graphs_do_not_share_saved_identity) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    auto detached = harness.creature("same_character");
    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 0}, detached));
    auto moduleObject = harness.creature("same_character");
    const auto context = SerializedIdentityContext::moduleGraph("saved-area");
    auto graph = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 900))
                     .build();
    harness.game.reserveSavedObjectIds(
        *graph, context, SerializedGraphRoot::AreaGit);
    harness.game.registerSavedObjectIdentity(900, moduleObject, context);

    EXPECT_EQ(moduleObject, harness.game.getObjectBySavedId(900));
    EXPECT_NE(detached, harness.game.getObjectBySavedId(900));
    EXPECT_THROW(
        harness.game.registerSavedObjectIdentity(901, moduleObject, context),
        ValidationException);
}

TEST(RosterBinding, k1_rejects_puppet_namespace_and_tenth_npc_slot) {
    RosterHarness harness(GameID::KotOR);

    EXPECT_FALSE(harness.game.party().isRosterIdentityValid(
        {RosterKind::Puppet, 0}));
    EXPECT_FALSE(harness.game.party().isRosterIdentityValid(
        {RosterKind::Npc, 9}));
    EXPECT_TRUE(harness.game.party().isRosterIdentityValid(
        {RosterKind::Npc, 8}));
    auto creature = harness.creature("k1_companion");
    auto saved = decodeGff(
        SaveWideSnapshotBuilder::availableNpcRecord(harness.game, *creature));
    EXPECT_FALSE(saved->has("AssignedPup"));
}

TEST(RosterBinding, k2_puppet_assignment_uses_separate_slot_namespace) {
    RosterHarness harness;
    auto npc = harness.creature("remote");
    auto puppet = harness.creature("remote");
    ASSERT_TRUE(harness.game.party().addAvailableMember(1, npc));
    ASSERT_TRUE(harness.game.party().addAvailablePuppet(0, puppet));

    ASSERT_TRUE(harness.game.party().addMember(1, npc));
    ASSERT_TRUE(harness.game.party().addPuppet(0, puppet));
    auto assigned = harness.call(
        "AssignPUP", {Variable::ofInt(0), Variable::ofInt(1)});

    EXPECT_EQ(1, assigned.intValue);
    EXPECT_TRUE(harness.game.party().isPuppet(0));
    EXPECT_EQ(1, *harness.game.party().assignedNpcForPuppet(0));
    EXPECT_EQ(npc, harness.game.party().puppetOwner(0));
    EXPECT_TRUE(puppet->isPuppet());
    EXPECT_EQ(
        npc->id(),
        harness.call("GetPUPOwner", {Variable::ofObject(puppet->id())})
            .objectId);
    EXPECT_EQ(
        1,
        harness.call("GetIsPuppet", {Variable::ofObject(puppet->id())})
            .intValue);
    EXPECT_EQ(
        (RosterIdentity {RosterKind::Npc, 1}),
        *harness.game.party().rosterIdentity(*npc));
    EXPECT_EQ(
        (RosterIdentity {RosterKind::Puppet, 0}),
        *harness.game.party().rosterIdentity(*puppet));
    auto savedNpc = decodeGff(
        SaveWideSnapshotBuilder::availableNpcRecord(harness.game, *npc));
    EXPECT_EQ(0, savedNpc->getInt("AssignedPup", -1));
}

TEST(RosterBinding, puppet_assignment_is_creature_state_not_a_unique_slot_map) {
    RosterHarness harness;
    auto first = harness.creature("first_owner");
    auto second = harness.creature("second_owner");
    auto puppet = harness.creature("remote");
    ASSERT_TRUE(harness.game.party().addAvailableMember(0, first));
    ASSERT_TRUE(harness.game.party().addAvailableMember(1, second));
    ASSERT_TRUE(harness.game.party().addAvailablePuppet(0, puppet));
    ASSERT_TRUE(harness.game.party().addMember(0, first));
    ASSERT_TRUE(harness.game.party().addMember(1, second));

    ASSERT_TRUE(harness.game.party().assignPuppet(0, 0));
    ASSERT_TRUE(harness.game.party().assignPuppet(0, 1));

    EXPECT_EQ(0, first->assignedPuppet());
    EXPECT_EQ(0, second->assignedPuppet());
    EXPECT_EQ(first, harness.game.party().puppetOwner(0));
}

TEST(RosterBinding, member_lifecycle_activates_and_kills_assigned_puppet) {
    RosterHarness harness;
    auto &director = testEngine().resourceModule().director();
    auto committed = std::make_shared<const SaveWorkingState>();
    EXPECT_CALL(director, committedSaveWorkingState())
        .Times(2)
        .WillRepeatedly(Invoke([&committed]() { return committed; }));
    EXPECT_CALL(director, adoptSaveWorkingState(_))
        .Times(2)
        .WillRepeatedly(Invoke(
            [&committed](auto state) { committed = std::move(state); }));
    auto npc = harness.creature("owner");
    auto puppet = harness.creature("remote");
    npc->setAppearance(271);
    puppet->setAppearance(314);
    ASSERT_TRUE(harness.game.party().addAvailableMember(0, npc));
    ASSERT_TRUE(harness.game.party().addAvailablePuppet(0, puppet));
    ASSERT_TRUE(harness.game.party().assignPuppet(0, 0));

    ASSERT_TRUE(harness.game.party().addMember(0, npc));
    EXPECT_TRUE(harness.game.party().isPuppet(0));
    EXPECT_EQ(puppet, harness.game.party().getAvailablePuppet(0));

    ASSERT_TRUE(harness.game.party().removeMember(0));
    EXPECT_FALSE(harness.game.party().isPuppet(0));
    EXPECT_FALSE(harness.game.party().getAvailablePuppet(0));
    EXPECT_TRUE(harness.game.party().isRosterAvailable({RosterKind::Puppet, 0}));
    EXPECT_FALSE(harness.game.getObjectById(puppet->id()));
    auto savedNpc = committed->find({"availnpc0", ResType::Utc});
    auto savedPuppet = committed->find({"availpup0", ResType::Utc});
    ASSERT_TRUE(savedNpc);
    ASSERT_TRUE(savedPuppet);
    EXPECT_EQ(271u, decodeGff(savedNpc->data)->getUint("Appearance_Type"));
    EXPECT_EQ(314u, decodeGff(savedPuppet->data)->getUint("Appearance_Type"));
}
