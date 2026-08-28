/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/game.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;

namespace {

std::shared_ptr<Gff> creatureRecord(
    std::string tag,
    uint32_t objectId,
    uint32_t appearance = 1,
    std::string name = "Companion") {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("Tag", std::move(tag)))
        .field(Gff::Field::newResRef("TemplateResRef", "p_companion"))
        .field(Gff::Field::newCExoLocString("FirstName", -1, std::move(name)))
        .field(Gff::Field::newCExoLocString("LastName", -1, ""))
        .field(Gff::Field::newDword("Appearance_Type", appearance))
        .field(Gff::Field::newWord("PortraitId", appearance + 10))
        .field(Gff::Field::newByte("Gender", 0))
        .field(Gff::Field::newByte("Race", 6))
        .field(Gff::Field::newWord("SoundSetFile", appearance + 20))
        .field(Gff::Field::newDword("ObjectId", objectId))
        .build();
}

std::shared_ptr<Gff> areaGit(
    std::vector<std::shared_ptr<Gff>> creatures) {
    return Gff::Builder()
        .field(Gff::Field::newList("Creature List", std::move(creatures)))
        .build();
}

class RosterHarness {
public:
    explicit RosterHarness(GameID gameId = GameID::TSL) :
        game(gameId, "", testEngine().options(), testEngine().services(), console) {
    }

    std::shared_ptr<Creature> bindDetached(
        RosterIdentity identity,
        const std::shared_ptr<Gff> &record) {
        auto creature = game.newCreature();
        const std::string resourceName =
            (identity.kind == RosterKind::Npc ? "availnpc" : "availpup") +
            std::to_string(identity.slot) + ".utc";
        const auto context =
            SerializedIdentityContext::detachedRecord(resourceName);
        creature->captureSaveRecord(
            *record,
            context,
            {identity.kind == RosterKind::Npc
                 ? SaveRecordOriginKind::AvailableNpc
                 : SaveRecordOriginKind::AvailablePuppet,
             std::to_string(identity.slot)});
        EXPECT_TRUE(game.party().bindRosterCreature(identity, creature));
        return creature;
    }

    void prepare(const Gff &git) {
        TestGameModule::prepareRosterMaterialization(
            game, &git, SerializedIdentityContext::templateResource("area"));
    }

    std::shared_ptr<Creature> materialize(
        const Gff &record,
        const RosterGitMaterialization &decision) {
        auto creature = game.newCreature();
        creature->captureSaveRecord(
            record,
            SerializedIdentityContext::detachedRecord("staged-roster-view"));
        game.stageRosterGitCreature(decision.identity, creature);
        return creature;
    }

    StubConsole console;
    Game game;
};

} // namespace

TEST(RosterMaterialization, git_and_detached_npc_reconcile_despite_unrelated_numbers) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    auto detached = creatureRecord("companion_a", 41);
    auto old = harness.bindDetached({RosterKind::Npc, 0}, detached);
    auto gitCreature = creatureRecord("companion_a", 900);
    auto git = areaGit({gitCreature});

    harness.prepare(*git);
    auto decision = harness.game.rosterGitMaterialization(
        *gitCreature, SerializedIdentityContext::templateResource("area"));
    ASSERT_EQ(RosterGitAction::MaterializeAndBind, decision.action);
    auto materialized = harness.materialize(*gitCreature, decision);
    TestGameModule::commitRosterMaterialization(harness.game);

    EXPECT_EQ(materialized, harness.game.party().getAvailableMember(0));
    EXPECT_NE(old, materialized);
    EXPECT_NE(detached->getUint("ObjectId"), gitCreature->getUint("ObjectId"));
    EXPECT_FALSE(harness.game.getObjectById(old->id()));
}

TEST(RosterMaterialization, authored_tag_survives_mutable_character_state_changes) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    harness.bindDetached(
        {RosterKind::Npc, 0},
        creatureRecord("companion_a", 41, 1, "Original Name"));
    auto gitCreature = creatureRecord(
        "COMPANION_A", 900, 99, "Progressed Name");
    auto git = areaGit({gitCreature});

    harness.prepare(*git);

    EXPECT_EQ(
        RosterGitAction::MaterializeAndBind,
        harness.game
            .rosterGitMaterialization(
                *gitCreature,
                SerializedIdentityContext::templateResource("area"))
            .action);
}

TEST(RosterMaterialization, duplicate_authored_roster_tags_are_rejected) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    state.npcAvailable[1] = true;
    harness.game.party().setPersistedState(state);
    harness.bindDetached(
        {RosterKind::Npc, 0}, creatureRecord("shared_tag", 41));
    harness.bindDetached(
        {RosterKind::Npc, 1}, creatureRecord("SHARED_TAG", 42));
    auto git = areaGit({creatureRecord("shared_tag", 900)});

    EXPECT_THROW(harness.prepare(*git), ValidationException);
}

TEST(RosterMaterialization, duplicate_git_tags_for_one_roster_slot_are_rejected) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    harness.bindDetached(
        {RosterKind::Npc, 0}, creatureRecord("companion_a", 41));
    auto git = areaGit(
        {creatureRecord("companion_a", 900),
         creatureRecord("COMPANION_A", 901)});

    EXPECT_THROW(harness.prepare(*git), ValidationException);
}

TEST(RosterMaterialization, numeric_coincidence_does_not_discover_roster_identity) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    auto detached = creatureRecord("companion_a", 77);
    harness.bindDetached({RosterKind::Npc, 0}, detached);
    auto ordinary = creatureRecord("ordinary_guard", 77, 3, "Guard");
    auto git = areaGit({ordinary});

    harness.prepare(*git);
    auto decision = harness.game.rosterGitMaterialization(
        *ordinary, SerializedIdentityContext::templateResource("area"));

    EXPECT_EQ(RosterGitAction::Ordinary, decision.action);
    EXPECT_EQ(77u, detached->getUint("ObjectId"));
    EXPECT_EQ(77u, ordinary->getUint("ObjectId"));
}

TEST(RosterMaterialization, two_slots_with_overlapping_serialized_numbers_stay_distinct) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    state.npcAvailable[1] = true;
    harness.game.party().setPersistedState(state);
    harness.bindDetached(
        {RosterKind::Npc, 0}, creatureRecord("companion_a", 55, 1, "A"));
    harness.bindDetached(
        {RosterKind::Npc, 1}, creatureRecord("companion_b", 55, 2, "B"));
    auto gitA = creatureRecord("companion_a", 100, 1, "A");
    auto gitB = creatureRecord("companion_b", 101, 2, "B");
    auto git = areaGit({gitA, gitB});

    harness.prepare(*git);
    auto decisionA = harness.game.rosterGitMaterialization(
        *gitA, SerializedIdentityContext::templateResource("area"));
    auto decisionB = harness.game.rosterGitMaterialization(
        *gitB, SerializedIdentityContext::templateResource("area"));
    auto runtimeA = harness.materialize(*gitA, decisionA);
    auto runtimeB = harness.materialize(*gitB, decisionB);
    TestGameModule::commitRosterMaterialization(harness.game);

    EXPECT_EQ(runtimeA, harness.game.party().getAvailableMember(0));
    EXPECT_EQ(runtimeB, harness.game.party().getAvailableMember(1));
    EXPECT_NE(runtimeA, runtimeB);
}

TEST(RosterMaterialization, active_member_omits_historical_git_copy) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    state.memberIds = {0};
    harness.game.party().setPersistedState(state);
    auto detached = creatureRecord("companion_a", 41);
    auto active = harness.bindDetached({RosterKind::Npc, 0}, detached);
    harness.game.party().addMember(0, active);
    auto gitCreature = creatureRecord("companion_a", 900);
    auto git = areaGit({gitCreature});

    harness.prepare(*git);
    auto decision = harness.game.rosterGitMaterialization(
        *gitCreature, SerializedIdentityContext::templateResource("area"));
    TestGameModule::commitRosterMaterialization(harness.game);

    EXPECT_EQ(RosterGitAction::OmitAndReuse, decision.action);
    EXPECT_EQ(active, decision.existing);
    EXPECT_EQ(active, harness.game.party().getAvailableMember(0));
    EXPECT_EQ(active, harness.game.party().getMemberByNPC(0));
}

TEST(RosterMaterialization, saved_git_identity_is_an_alias_of_active_roster_identity) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    state.memberIds = {0};
    harness.game.party().setPersistedState(state);
    auto detached = creatureRecord("companion_a", 41);
    auto active = harness.bindDetached({RosterKind::Npc, 0}, detached);
    harness.game.party().addMember(0, active);
    auto gitCreature = creatureRecord("companion_a", 900);
    auto git = areaGit({gitCreature});
    const auto context = SerializedIdentityContext::moduleGraph("saved-area");
    harness.game.reserveSavedObjectIds(
        *git, context, SerializedGraphRoot::AreaGit);
    harness.game.registerSavedObjectIdentity(800, active, context);
    TestGameModule::prepareRosterMaterialization(
        harness.game, git.get(), context);

    auto decision =
        harness.game.rosterGitMaterialization(*gitCreature, context);
    TestGameModule::commitRosterMaterialization(harness.game);

    EXPECT_EQ(RosterGitAction::OmitAndReuse, decision.action);
    EXPECT_EQ(active, harness.game.getObjectBySavedId(800));
    EXPECT_EQ(active, harness.game.getObjectBySavedId(900));
    EXPECT_EQ(active, harness.game.party().getAvailableMember(0));
    ASSERT_TRUE(active->serializedObjectIdentity());
    EXPECT_EQ(800u, active->serializedObjectIdentity()->id);
    ModuleObjectIdContext outbound(context);
    outbound.retainObject(*active, true);
    EXPECT_EQ(800u, outbound.objectId(*active));
}

TEST(RosterMaterialization, unavailable_detached_record_does_not_claim_git_creature) {
    RosterHarness harness;
    Party::PersistedState state;
    harness.game.party().setPersistedState(state);
    auto detached = creatureRecord("companion_a", 41);
    harness.bindDetached({RosterKind::Npc, 0}, detached);
    auto gitCreature = creatureRecord("companion_a", 900);
    auto git = areaGit({gitCreature});

    harness.prepare(*git);

    EXPECT_EQ(
        RosterGitAction::Ordinary,
        harness.game
            .rosterGitMaterialization(
                *gitCreature,
                SerializedIdentityContext::templateResource("area"))
            .action);
}

TEST(RosterMaterialization, authored_area_matches_a_fresh_runtime_roster_binding) {
    RosterHarness harness;
    EXPECT_CALL(
        testEngine().resourceModule().director(),
        findSaveWorking(testing::_))
        .Times(testing::AnyNumber());
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    auto fresh = harness.game.newCreature();
    fresh->setTag("fresh_companion");
    fresh->setName("Fresh Companion");
    ASSERT_TRUE(harness.game.party().bindRosterCreature(
        {RosterKind::Npc, 0}, fresh));
    auto gitCreature = Gff::Builder()
                           .field(Gff::Field::newCExoString(
                               "Tag", "fresh_companion"))
                           .field(Gff::Field::newCExoLocString(
                               "FirstName", -1, "Fresh Companion"))
                           .field(Gff::Field::newDword(
                               "Appearance_Type", 0))
                           .field(Gff::Field::newWord("PortraitId", 0))
                           .field(Gff::Field::newByte("Gender", 0))
                           .field(Gff::Field::newByte("Race", 0))
                           .field(Gff::Field::newByte("SubraceIndex", 0))
                           .field(Gff::Field::newWord("SoundSetFile", 99))
                           .field(Gff::Field::newDword("ObjectId", 900))
                           .build();
    auto git = areaGit({gitCreature});

    harness.prepare(*git);

    EXPECT_EQ(
        RosterGitAction::MaterializeAndBind,
        harness.game
            .rosterGitMaterialization(
                *gitCreature,
                SerializedIdentityContext::templateResource("area"))
            .action);
}

TEST(RosterMaterialization, puppet_uses_a_separate_logical_identity_namespace) {
    RosterHarness harness;
    Party::PersistedState state;
    state.puppetAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    harness.bindDetached(
        {RosterKind::Puppet, 0},
        creatureRecord("remote_puppet", 41, 7, "Remote"));
    auto gitCreature = creatureRecord("remote_puppet", 900, 7, "Remote");
    auto git = areaGit({gitCreature});

    harness.prepare(*git);
    auto decision = harness.game.rosterGitMaterialization(
        *gitCreature, SerializedIdentityContext::templateResource("area"));
    auto materialized = harness.materialize(*gitCreature, decision);
    TestGameModule::commitRosterMaterialization(harness.game);

    EXPECT_EQ(RosterKind::Puppet, decision.identity.kind);
    EXPECT_EQ(materialized, harness.game.party().getAvailablePuppet(0));
    EXPECT_FALSE(harness.game.party().getAvailableMember(0));
}

TEST(RosterMaterialization, aborted_destination_does_not_publish_staged_binding) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    auto old = harness.bindDetached(
        {RosterKind::Npc, 0}, creatureRecord("companion_a", 41));
    auto gitCreature = creatureRecord("companion_a", 900);
    auto git = areaGit({gitCreature});

    harness.prepare(*git);
    auto decision = harness.game.rosterGitMaterialization(
        *gitCreature, SerializedIdentityContext::templateResource("area"));
    auto partial = harness.materialize(*gitCreature, decision);
    TestGameModule::abortRosterMaterialization(harness.game);

    EXPECT_EQ(old, harness.game.party().getAvailableMember(0));
    EXPECT_NE(partial, harness.game.party().getAvailableMember(0));
}

TEST(RosterMaterialization, repeated_reconciliation_replaces_one_binding_without_duplicates) {
    RosterHarness harness;
    Party::PersistedState state;
    state.npcAvailable[0] = true;
    harness.game.party().setPersistedState(state);
    auto detached = creatureRecord("companion_a", 41);
    auto original = harness.bindDetached({RosterKind::Npc, 0}, detached);
    auto gitCreature = creatureRecord("companion_a", 900);
    auto git = areaGit({gitCreature});

    harness.prepare(*git);
    auto firstDecision = harness.game.rosterGitMaterialization(
        *gitCreature, SerializedIdentityContext::templateResource("area"));
    auto first = harness.materialize(*gitCreature, firstDecision);
    TestGameModule::commitRosterMaterialization(harness.game);

    harness.prepare(*git);
    auto secondDecision = harness.game.rosterGitMaterialization(
        *gitCreature, SerializedIdentityContext::templateResource("area"));
    auto second = harness.materialize(*gitCreature, secondDecision);
    TestGameModule::commitRosterMaterialization(harness.game);

    EXPECT_EQ(second, harness.game.party().getAvailableMember(0));
    EXPECT_NE(original, first);
    EXPECT_NE(first, second);
    EXPECT_FALSE(harness.game.party().rosterIdentity(*original));
    EXPECT_FALSE(harness.game.party().rosterIdentity(*first));
    EXPECT_FALSE(harness.game.getObjectById(original->id()));
    EXPECT_FALSE(harness.game.getObjectById(first->id()));
    const std::optional<RosterIdentity> expected {
        RosterIdentity {RosterKind::Npc, 0}};
    EXPECT_EQ(expected, harness.game.party().rosterIdentity(*second));
}
