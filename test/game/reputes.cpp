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

#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/reputes.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

// Synthetic factions. The relationships below are deliberately asymmetric so a
// query that reverses source and target reads a different cell.
constexpr Faction kBeta = static_cast<Faction>(1);
constexpr Faction kGamma = static_cast<Faction>(2);

// beta regards gamma as hostile, while gamma regards beta as friendly.
std::shared_ptr<TwoDA> makeReputeTable() {
    std::vector<std::string> columns {"label", "alpha", "beta", "gamma", "delta"};
    std::vector<TwoDA::Row> rows {
        TwoDA::newRow("0", {"alpha", "50", "50", "50", "50"}),
        TwoDA::newRow("1", {"beta", "50", "100", "0", "50"}),
        TwoDA::newRow("2", {"gamma", "50", "100", "100", "50"}),
        TwoDA::newRow("3", {"delta", "50", "50", "50", "50"})};
    return std::make_shared<TwoDA>(std::move(columns), std::move(rows));
}

std::unique_ptr<Reputes> makeReputes(TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("repute"))
        .WillOnce(Return(makeReputeTable()));
    auto reputes = std::make_unique<Reputes>(engine.services().resource.twoDas);
    reputes->init();
    return reputes;
}

} // namespace

TEST(Reputes, authored_directed_values_are_retained_in_both_directions) {
    TestEngine &engine = testEngine();

    auto reputes = makeReputes(engine);

    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ(100, reputes->getReputation(kGamma, kBeta));
}

TEST(Reputes, adjustment_changes_only_the_source_view_of_the_target) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    reputes->adjustReputation(kBeta, kGamma, 50);

    EXPECT_EQ(50, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ(100, reputes->getReputation(kGamma, kBeta));
}

TEST(Reputes, repeated_adjustments_accumulate) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    reputes->adjustReputation(kBeta, kGamma, 20);
    reputes->adjustReputation(kBeta, kGamma, 20);

    EXPECT_EQ(40, reputes->getReputation(kBeta, kGamma));
}

TEST(Reputes, adjustments_clamp_to_the_representable_range) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    reputes->adjustReputation(kBeta, kGamma, 1000);
    EXPECT_EQ(100, reputes->getReputation(kBeta, kGamma));

    reputes->adjustReputation(kBeta, kGamma, -1000);
    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
}

TEST(Reputes, a_positive_adjustment_lifts_hostility_and_a_negative_one_restores_it) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    // This is the disguise shape: the source faction stops treating the target
    // as an enemy, then treats it as one again once the disguise is dropped.
    EXPECT_TRUE(reputes->getIsEnemy(kBeta, kGamma));

    reputes->adjustReputation(kBeta, kGamma, 50);
    EXPECT_FALSE(reputes->getIsEnemy(kBeta, kGamma));

    reputes->adjustReputation(kBeta, kGamma, -50);
    EXPECT_TRUE(reputes->getIsEnemy(kBeta, kGamma));
}

TEST(Reputes, hostility_is_read_from_the_source_view_not_the_target_view) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    EXPECT_TRUE(reputes->getIsEnemy(kBeta, kGamma));
    EXPECT_FALSE(reputes->getIsEnemy(kGamma, kBeta));
}

TEST(Reputes, out_of_range_and_identical_factions_leave_the_matrix_untouched) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    reputes->adjustReputation(static_cast<Faction>(-1), kGamma, 50);
    reputes->adjustReputation(kBeta, static_cast<Faction>(99), 50);
    reputes->adjustReputation(static_cast<Faction>(99), kGamma, 50);
    reputes->adjustReputation(kBeta, kBeta, -50);

    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ(100, reputes->getReputation(kGamma, kBeta));
    EXPECT_EQ(100, reputes->getReputation(kBeta, kBeta));
}

TEST(Reputes, unknown_factions_report_the_neutral_default) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    EXPECT_EQ(50, reputes->getReputation(static_cast<Faction>(99), kGamma));
    EXPECT_EQ(50, reputes->getReputation(kBeta, static_cast<Faction>(99)));
}

TEST(Reputes, reinitialization_restores_the_authored_matrix) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);
    reputes->adjustReputation(kBeta, kGamma, 50);
    ASSERT_EQ(50, reputes->getReputation(kBeta, kGamma));

    EXPECT_CALL(engine.resourceModule().twoDas(), get("repute"))
        .WillOnce(Return(makeReputeTable()));
    reputes->init();

    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
}

TEST(AdjustReputationRoutine, adjusts_the_source_faction_view_of_the_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->setFaction(kGamma);
    auto sourceFactionMember = game.newCreature();
    sourceFactionMember->setFaction(kBeta);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;

    EXPECT_CALL(static_cast<MockReputes &>(engine.services().game.reputes),
                adjustReputation(kBeta, kGamma, 50));

    routines.get(209).invoke(
        {script::Variable::ofObject(target->id()),
         script::Variable::ofObject(sourceFactionMember->id()),
         script::Variable::ofInt(50)},
        execution);
}

TEST(AdjustReputationRoutine, a_non_creature_target_is_ignored) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newPlaceable();
    auto sourceFactionMember = game.newCreature();
    sourceFactionMember->setFaction(kBeta);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;

    EXPECT_CALL(static_cast<MockReputes &>(engine.services().game.reputes),
                adjustReputation(_, _, _))
        .Times(0);

    routines.get(209).invoke(
        {script::Variable::ofObject(target->id()),
         script::Variable::ofObject(sourceFactionMember->id()),
         script::Variable::ofInt(50)},
        execution);
}

TEST(DirectedDispositionRoutines, query_the_source_view_of_the_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->setFaction(kGamma);
    auto source = game.newCreature();
    source->setFaction(kBeta);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;
    auto &reputes = static_cast<MockReputes &>(engine.services().game.reputes);

    // GetIsEnemy(oTarget, oSource) asks whether oSource's faction regards
    // oTarget as an enemy, so oSource is the source of the query.
    EXPECT_CALL(reputes, getIsEnemy(Ref(*source), Ref(*target))).WillOnce(Return(true));
    EXPECT_CALL(reputes, getIsFriend(Ref(*source), Ref(*target))).WillOnce(Return(false));
    EXPECT_CALL(reputes, getIsNeutral(Ref(*source), Ref(*target))).WillOnce(Return(false));

    std::vector<script::Variable> args {
        script::Variable::ofObject(target->id()),
        script::Variable::ofObject(source->id())};

    EXPECT_EQ(1, routines.get(235).invoke(args, execution).intValue);
    EXPECT_EQ(0, routines.get(236).invoke(args, execution).intValue);
    EXPECT_EQ(0, routines.get(237).invoke(args, execution).intValue);
}

namespace {

std::shared_ptr<Gff> member(int npc, bool leader) {
    return Gff::Builder()
        .field(Gff::Field::newInt("PT_MEMBER_ID", npc))
        .field(Gff::Field::newByte("PT_IS_LEADER", leader))
        .build();
}

std::shared_ptr<Gff> availableNpc(bool available, bool selectable) {
    return Gff::Builder()
        .field(Gff::Field::newByte("PT_NPC_AVAIL", available))
        .field(Gff::Field::newByte("PT_NPC_SELECT", selectable))
        .build();
}

std::shared_ptr<Gff> availablePuppet(bool available, bool selectable) {
    return Gff::Builder()
        .field(Gff::Field::newByte("PT_PUP_AVAIL", available))
        .field(Gff::Field::newByte("PT_PUP_SELECT", selectable))
        .build();
}

std::shared_ptr<Gff> influence(int value) {
    return Gff::Builder()
        .field(Gff::Field::newInt("PT_NPC_INFLUENCE", value))
        .build();
}

std::shared_ptr<Gff> dialogMessage(std::string speaker, std::string text) {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("PT_DLG_MSG_SPKR", std::move(speaker)))
        .field(Gff::Field::newCExoString("PT_DLG_MSG_MSG", std::move(text)))
        .build();
}

std::shared_ptr<Gff> logMessage(
    std::string_view prefix,
    std::string colorLabel,
    uint8_t color,
    uint32_t type,
    std::string text) {
    return Gff::Builder()
        .field(Gff::Field::newByte(std::move(colorLabel), color))
        .field(Gff::Field::newDword(std::string(prefix) + "_TYPE", type))
        .field(Gff::Field::newCExoString(std::string(prefix) + "_MSG", std::move(text)))
        .build();
}

std::shared_ptr<Gff> partyTableA() {
    auto galaxy = Gff::Builder()
        .field(Gff::Field::newDword("GlxyMapNumPnts", 16))
        .field(Gff::Field::newDword("GlxyMapPlntMsk", (1u << 2) | (1u << 19)))
        .field(Gff::Field::newInt("GlxyMapSelPnt", 3))
        .build();
    return Gff::Builder()
        .field(Gff::Field::newCExoString("PT_PCNAME", "A"))
        .field(Gff::Field::newDword("PT_ITEM_COMPONEN", 9))
        .field(Gff::Field::newDword("PT_PLAYEDSECONDS", 123))
        .field(Gff::Field::newInt("PT_CONTROLLED_NP", 1))
        .field(Gff::Field::newByte("PT_SOLOMODE", 1))
        .field(Gff::Field::newByte("PT_NUM_MEMBERS", 1))
        .field(Gff::Field::newList("PT_MEMBERS", {member(1, true)}))
        .field(Gff::Field::newByte("PT_NUM_PUPPETS", 1))
        .field(Gff::Field::newList(
            "PT_PUPPETS",
            {Gff::Builder().field(Gff::Field::newInt("PT_PUPPET_ID", 2)).build()}))
        .field(Gff::Field::newList("PT_AVAIL_NPCS", {availableNpc(true, false)}))
        .field(Gff::Field::newList("PT_AVAIL_PUPS", {availablePuppet(true, false)}))
        .field(Gff::Field::newList("PT_INFLUENCE", {influence(42)}))
        .field(Gff::Field::newInt("PT_AISTATE", 4))
        .field(Gff::Field::newInt("PT_FOLLOWSTATE", 5))
        .field(Gff::Field::newList(
            "PT_DLG_MSG_LIST", {dialogMessage("Carth", "Dialog history")}))
        .field(Gff::Field::newList(
            "PT_FB_MSG_LIST",
            {logMessage("PT_FB_MSG", "PT_FB_MSG_COLOR", 0, 0x80, "Feedback history")}))
        .field(Gff::Field::newList(
            "PT_COM_MSG_LIST",
            {logMessage("PT_COM_MSG", "PT_COM_MSG_COOR", 1, 0x80, "Combat history")}))
        .field(Gff::Field::newStruct("GlxyMap", galaxy))
        .build();
}

std::shared_ptr<Gff> partyTableB() {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("PT_PCNAME", "B"))
        .field(Gff::Field::newDword("PT_PLAYEDMINUTES", 3))
        .field(Gff::Field::newByte("PT_NUM_MEMBERS", 0))
        .field(Gff::Field::newList("PT_MEMBERS", {}))
        .field(Gff::Field::newList("PT_AVAIL_NPCS", {availableNpc(false, true)}))
        .build();
}

struct Fixture {
    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game;

    explicit Fixture(GameID gameId) :
        game(gameId, "", engine.options(), engine.services(), console) {
    }
};

void load(Game &game, const std::shared_ptr<Gff> &table) {
    TestGameModule::deserializePartyTable(game, *table);
}

} // namespace

TEST(SaveWidePartyTable, parsing_is_separate_from_publication) {
    Fixture fixture(GameID::TSL);
    auto table = partyTableA();

    Party::PersistedState candidate = fixture.game.parsePartyTable(*table);

    EXPECT_TRUE(fixture.game.party().persistedState().memberIds.empty());
    EXPECT_EQ("", fixture.game.party().persistedState().pcName);
    EXPECT_EQ(std::vector<int>({1}), candidate.memberIds);
    EXPECT_EQ("A", candidate.pcName);

    fixture.game.replacePartyTable(std::move(candidate));
    EXPECT_EQ(std::vector<int>({1}), fixture.game.party().persistedState().memberIds);
    EXPECT_EQ("A", fixture.game.party().persistedState().pcName);
}

TEST(SaveWidePartyTable, k1_a_b_a_replaces_roster_availability_and_non_roster_state) {
    Fixture fixture(GameID::KotOR);
    auto a = partyTableA();
    auto b = partyTableB();

    load(fixture.game, a);
    const auto &firstA = fixture.game.party().persistedState();
    ASSERT_EQ(std::vector<int>({1}), firstA.memberIds);
    EXPECT_EQ(1, firstA.leader);
    EXPECT_TRUE(firstA.npcAvailable[0]);
    EXPECT_FALSE(firstA.npcSelectable[0]);
    EXPECT_EQ(4, firstA.aiState);
    EXPECT_TRUE(firstA.planetAvailable[2]);
    EXPECT_TRUE(firstA.planetSelectable[3]);
    EXPECT_EQ(3, firstA.selectedPlanet);

    load(fixture.game, b);
    const auto &loadedB = fixture.game.party().persistedState();
    EXPECT_TRUE(loadedB.memberIds.empty());
    EXPECT_FALSE(loadedB.npcAvailable[0]);
    EXPECT_TRUE(loadedB.npcSelectable[0]);
    EXPECT_EQ(0, loadedB.aiState);
    EXPECT_FALSE(loadedB.planetAvailable[2]);
    EXPECT_EQ(-1, loadedB.selectedPlanet);
    EXPECT_EQ(180u, loadedB.playedSeconds);

    load(fixture.game, a);
    const auto &secondA = fixture.game.party().persistedState();
    EXPECT_EQ(std::vector<int>({1}), secondA.memberIds);
    EXPECT_TRUE(secondA.npcAvailable[0]);
    EXPECT_EQ(4, secondA.aiState);
    EXPECT_TRUE(secondA.planetAvailable[2]);
    EXPECT_EQ("A", secondA.pcName);
}

TEST(SaveWidePartyTable, k2_retains_puppet_influence_and_resource_state) {
    Fixture fixture(GameID::TSL);
    load(fixture.game, partyTableA());
    const auto &state = fixture.game.party().persistedState();

    ASSERT_EQ(std::vector<int>({2}), state.puppetIds);
    EXPECT_TRUE(state.puppetAvailable[0]);
    EXPECT_FALSE(state.puppetSelectable[0]);
    EXPECT_EQ(42, state.influence[0]);
    EXPECT_EQ(9u, state.itemComponent);
    EXPECT_EQ(5, state.followState);
    ASSERT_EQ(1u, state.dialogMessages.size());
    EXPECT_EQ("Carth", state.dialogMessages[0].speaker);
    EXPECT_EQ("Dialog history", state.dialogMessages[0].text);
    ASSERT_EQ(1u, state.feedbackMessages.size());
    EXPECT_EQ(0u, state.feedbackMessages[0].color);
    EXPECT_EQ(0x80u, state.feedbackMessages[0].type);
    EXPECT_EQ("Feedback history", state.feedbackMessages[0].text);
    ASSERT_EQ(1u, state.combatMessages.size());
    EXPECT_EQ(1u, state.combatMessages[0].color);
    EXPECT_EQ(0x80u, state.combatMessages[0].type);
    EXPECT_EQ("Combat history", state.combatMessages[0].text);
    EXPECT_TRUE(fixture.game.party().isSoloMode());
}

TEST(SaveWidePartyTable, absent_k2_fields_replace_prior_values_with_defaults) {
    Fixture fixture(GameID::TSL);
    load(fixture.game, partyTableA());
    load(fixture.game, partyTableB());

    const auto &state = fixture.game.party().persistedState();
    EXPECT_TRUE(state.puppetIds.empty());
    EXPECT_FALSE(state.puppetAvailable[0]);
    EXPECT_TRUE(state.puppetSelectable[0]);
    EXPECT_EQ(-1, state.influence[0]);
    EXPECT_EQ(0u, state.itemComponent);
    EXPECT_EQ(0, state.followState);
    EXPECT_FALSE(fixture.game.party().isSoloMode());
}

namespace {

std::shared_ptr<Gff> makeFaction(
    std::string name,
    uint32_t parent = std::numeric_limits<uint32_t>::max(),
    std::optional<uint16_t> global = 1) {

    std::vector<Gff::Field> fields {
        Gff::Field::newCExoString("FactionName", std::move(name)),
        Gff::Field::newDword("FactionParentID", parent)};
    if (global) {
        fields.push_back(Gff::Field::newWord("FactionGlobal", *global));
    }
    return std::make_shared<Gff>(0, std::move(fields));
}

std::shared_ptr<Gff> makeRep(uint32_t target, uint32_t source, uint32_t value) {
    return Gff::Builder()
        .field(Gff::Field::newDword("FactionID1", target))
        .field(Gff::Field::newDword("FactionID2", source))
        .field(Gff::Field::newDword("FactionRep", value))
        .build();
}

std::shared_ptr<Gff> makeFac(
    std::vector<std::shared_ptr<Gff>> factions,
    std::vector<std::shared_ptr<Gff>> reputations,
    bool includeReputations = true) {

    Gff::Builder builder;
    builder.field(Gff::Field::newList("FactionList", std::move(factions)));
    if (includeReputations) {
        builder.field(Gff::Field::newList("RepList", std::move(reputations)));
    }
    return builder.build();
}

std::vector<std::shared_ptr<Gff>> fiveSavedFactions(std::string fifthName = "dynamic") {
    return {
        makeFaction("Player"),
        makeFaction("Saved_Beta", 17, std::nullopt),
        makeFaction("Saved_Gamma"),
        makeFaction("Saved_Delta"),
        makeFaction(std::move(fifthName))};
}

void expectBaseLookup(TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("repute"))
        .WillOnce(Return(makeReputeTable()));
}

} // namespace

TEST(ReputesFac, parsing_is_separate_from_publication) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);
    ASSERT_EQ(0, reputes->getReputation(kBeta, kGamma));

    expectBaseLookup(engine);
    auto state = reputes->parse(*makeFac(
        fiveSavedFactions(),
        {makeRep(2, 1, 77)}));

    ASSERT_TRUE(state);
    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));

    reputes->replace(std::move(*state));
    EXPECT_EQ(77, reputes->getReputation(kBeta, kGamma));
}

TEST(ReputesFac, restores_saved_definitions_and_defaults_missing_global_to_true) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    expectBaseLookup(engine);
    auto state = reputes->parse(*makeFac(fiveSavedFactions(), {}));
    ASSERT_TRUE(state);
    reputes->replace(std::move(*state));

    ASSERT_EQ(5u, reputes->factions().size());
    EXPECT_EQ("Saved_Beta", reputes->factions()[1].name);
    EXPECT_EQ(17u, reputes->factions()[1].parentId);
    EXPECT_TRUE(reputes->factions()[1].global);
}

TEST(ReputesFac, appends_only_base_rows_beyond_the_saved_faction_count) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    expectBaseLookup(engine);
    auto state = reputes->parse(*makeFac(
        {makeFaction("Player"), makeFaction("Saved_Beta")},
        {}));
    ASSERT_TRUE(state);
    reputes->replace(std::move(*state));

    ASSERT_EQ(4u, reputes->factions().size());
    EXPECT_EQ("Saved_Beta", reputes->factions()[1].name);
    EXPECT_EQ("gamma", reputes->factions()[2].name);
    EXPECT_EQ("delta", reputes->factions()[3].name);
}

TEST(ReputesFac, saved_pairs_win_and_sparse_dynamic_pairs_default_to_100) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    expectBaseLookup(engine);
    auto state = reputes->parse(*makeFac(
        fiveSavedFactions(),
        {makeRep(2, 1, 77)}));
    ASSERT_TRUE(state);
    reputes->replace(std::move(*state));

    EXPECT_EQ(77, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ(100, reputes->getReputation(static_cast<Faction>(4), kBeta));
}

TEST(ReputesFac, clamps_values_ignores_invalid_pairs_and_uses_last_duplicate) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    expectBaseLookup(engine);
    auto state = reputes->parse(*makeFac(
        fiveSavedFactions(),
        {makeRep(2, 1, 20),
         makeRep(2, 1, 60),
         makeRep(3, 2, std::numeric_limits<uint32_t>::max()),
         makeRep(1, 0, 1),
         makeRep(99, 1, 1)}));
    ASSERT_TRUE(state);
    reputes->replace(std::move(*state));

    EXPECT_EQ(60, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ(100, reputes->getReputation(kGamma, static_cast<Faction>(3)));
}

TEST(ReputesFac, malformed_candidate_does_not_publish_or_retain_another_save) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);

    expectBaseLookup(engine);
    auto stateA = reputes->parse(*makeFac(fiveSavedFactions("save_a"), {makeRep(2, 1, 77)}));
    ASSERT_TRUE(stateA);
    reputes->replace(std::move(*stateA));
    ASSERT_EQ(77, reputes->getReputation(kBeta, kGamma));

    expectBaseLookup(engine);
    EXPECT_FALSE(reputes->parse(*makeFac(fiveSavedFactions("bad"), {}, false)));
    expectBaseLookup(engine);
    reputes->replace(reputes->baseState());

    EXPECT_EQ(0, reputes->getReputation(kBeta, kGamma));
    ASSERT_EQ(4u, reputes->factions().size());
}

TEST(ReputesFac, save_switch_a_b_a_replaces_all_factions_and_pairs) {
    TestEngine &engine = testEngine();
    auto reputes = makeReputes(engine);
    auto loadFac = [&](std::string name, int value) {
        expectBaseLookup(engine);
        auto state = reputes->parse(*makeFac(
            fiveSavedFactions(std::move(name)),
            {makeRep(2, 1, value)}));
        ASSERT_TRUE(state);
        reputes->replace(std::move(*state));
    };

    loadFac("save_a", 77);
    EXPECT_EQ(77, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ("save_a", reputes->factions()[4].name);
    loadFac("save_b", 22);
    EXPECT_EQ(22, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ("save_b", reputes->factions()[4].name);
    loadFac("save_a", 77);
    EXPECT_EQ(77, reputes->getReputation(kBeta, kGamma));
    EXPECT_EQ("save_a", reputes->factions()[4].name);
}
void reone::game::TestGameModule::deserializeAvailableNpcs(Game &game) {
    game.deserializeAvailableNpcs();
}

void reone::game::TestGameModule::publishPartyRuntimeState(
    Game &game,
    Gff &ifoGff,
    const std::shared_ptr<Gff> &ptGff,
    const std::shared_ptr<Gff> &pcGff) {
    game.publishPartyRuntimeState(ifoGff, ptGff, pcGff);
}

void reone::game::TestGameModule::deserializeGlobalVariables(Game &game, Gff &gff) {
    game.deserializeGlobalVariables(gff);
}

void reone::game::TestGameModule::replaceJournal(Game &game, const Gff &gff) {
    game._journal.reset();
    game.deserializeJournal(gff);
}

void reone::game::TestGameModule::replaceInventory(Game &game, Gff &gff) {
    game._party.reset();
    auto player = game.newCreature();
    game._party.addMember(kNpcPlayer, player);
    game._party.setPlayer(player);
    game.deserializeInventory(gff);
}

namespace {

std::shared_ptr<Gff> gvt(
    std::string booleanName,
    bool booleanValue,
    std::string numberName,
    uint8_t numberValue) {

    auto name = [](std::string value) {
        return Gff::Builder()
            .field(Gff::Field::newCExoString("Name", std::move(value)))
            .build();
    };
    return Gff::Builder()
        .field(Gff::Field::newList("CatBoolean", {name(std::move(booleanName))}))
        .field(Gff::Field::newVoid("ValBoolean", {static_cast<char>(booleanValue ? 0x80 : 0)}))
        .field(Gff::Field::newList("CatNumber", {name(std::move(numberName))}))
        .field(Gff::Field::newVoid("ValNumber", {static_cast<char>(numberValue)}))
        .build();
}

std::shared_ptr<Gff> journalTable(std::string plot, int state, uint32_t date) {
    auto entry = Gff::Builder()
        .field(Gff::Field::newCExoString("JNL_PlotID", std::move(plot)))
        .field(Gff::Field::newInt("JNL_State", state))
        .field(Gff::Field::newDword("JNL_Date", date))
        .field(Gff::Field::newDword("JNL_Time", date + 1))
        .build();
    return Gff::Builder()
        .field(Gff::Field::newList("JNL_Entries", {std::move(entry)}))
        .build();
}

std::shared_ptr<Gff> inventory(
    std::initializer_list<std::pair<std::string, uint16_t>> itemStates) {

    std::vector<std::shared_ptr<Gff>> items;
    for (const auto &[tag, stackSize] : itemStates) {
        items.push_back(Gff::Builder()
                            .field(Gff::Field::newCExoString("Tag", tag))
                            .field(Gff::Field::newWord("StackSize", stackSize))
                            .build());
    }
    return Gff::Builder()
        .field(Gff::Field::newList("ItemList", std::move(items)))
        .build();
}

std::shared_ptr<TwoDA> appearanceTable() {
    TwoDA::Builder builder;
    builder.columns({"modeltype", "walkdist", "rundist", "footsteptype", "envmap", "race", "racetex"});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

Resource encodedGff(ResType type, const Gff &gff) {
    ByteBuffer bytes;
    MemoryOutputStream output(bytes);
    GffWriter(type, gff).save(output);
    return Resource {std::move(bytes)};
}

Resource encodedCreature(std::string tag) {
    auto gff = Gff::Builder()
        .field(Gff::Field::newCExoString("Tag", std::move(tag)))
        .field(Gff::Field::newDword("Appearance_Type", 0))
        .field(Gff::Field::newWord("SoundSetFile", 0xffff))
        .field(Gff::Field::newByte("BodyBag", 0xff))
        .field(Gff::Field::newByte("PerceptionRange", 0xff))
        .build();
    return encodedGff(ResType::Utc, *gff);
}

std::shared_ptr<Gff> availableTable(size_t availableIndex) {
    std::vector<std::shared_ptr<Gff>> states;
    for (size_t index = 0; index <= availableIndex; ++index) {
        states.push_back(availableNpc(index == availableIndex, index == availableIndex));
    }
    return Gff::Builder()
        .field(Gff::Field::newList("PT_AVAIL_NPCS", std::move(states)))
        .build();
}

std::shared_ptr<Gff> savedPlayer(std::string tag, uint32_t id, bool primary) {
    return Gff::Builder()
        .field(Gff::Field::newCExoString("Tag", std::move(tag)))
        .field(Gff::Field::newDword("ObjectId", id))
        .field(Gff::Field::newByte("Mod_IsPrimaryPlr", primary))
        .field(Gff::Field::newDword("Appearance_Type", 0))
        .field(Gff::Field::newWord("SoundSetFile", 0xffff))
        .field(Gff::Field::newByte("BodyBag", 0xff))
        .field(Gff::Field::newByte("PerceptionRange", 0xff))
        .build();
}

std::shared_ptr<Gff> emptyPartyTable(int controlledNpc = -1) {
    return Gff::Builder()
        .field(Gff::Field::newInt("PT_CONTROLLED_NP", controlledNpc))
        .field(Gff::Field::newByte("PT_NUM_MEMBERS", 0))
        .build();
}

} // namespace

TEST(SaveWideGlobals, a_b_a_replaces_changed_and_removed_values) {
    Fixture fixture(GameID::KotOR);
    auto a = gvt("a_bool", true, "a_num", 7);
    auto b = gvt("b_bool", false, "b_num", 9);

    TestGameModule::deserializeGlobalVariables(fixture.game, *a);
    EXPECT_TRUE(fixture.game.getGlobalBoolean("a_bool"));
    EXPECT_EQ(7, fixture.game.getGlobalNumber("a_num"));

    TestGameModule::deserializeGlobalVariables(fixture.game, *b);
    EXPECT_FALSE(fixture.game.getGlobalBoolean("a_bool"));
    EXPECT_EQ(0, fixture.game.getGlobalNumber("a_num"));
    EXPECT_FALSE(fixture.game.getGlobalBoolean("b_bool"));
    EXPECT_EQ(9, fixture.game.getGlobalNumber("b_num"));

    TestGameModule::deserializeGlobalVariables(fixture.game, *a);
    EXPECT_TRUE(fixture.game.getGlobalBoolean("a_bool"));
    EXPECT_EQ(7, fixture.game.getGlobalNumber("a_num"));
    EXPECT_EQ(0, fixture.game.getGlobalNumber("b_num"));
}

TEST(SaveWideJournal, a_b_a_replaces_entries_and_states) {
    Fixture fixture(GameID::KotOR);
    auto a = journalTable("plot_a", 10, 1);
    auto b = journalTable("plot_b", 20, 3);

    TestGameModule::replaceJournal(fixture.game, *a);
    EXPECT_EQ(10, fixture.game.journal().getEntryState("plot_a"));

    TestGameModule::replaceJournal(fixture.game, *b);
    EXPECT_EQ(0, fixture.game.journal().getEntryState("plot_a"));
    EXPECT_EQ(20, fixture.game.journal().getEntryState("plot_b"));

    TestGameModule::replaceJournal(fixture.game, *a);
    ASSERT_EQ(1u, fixture.game.journal().quests().size());
    EXPECT_EQ("plot_a", fixture.game.journal().quests().front().plotId);
    EXPECT_EQ(10, fixture.game.journal().quests().front().state);
    EXPECT_EQ(1u, fixture.game.journal().quests().front().date);
}

TEST(SaveWideInventory, a_b_a_replaces_items_and_preserves_stack_state) {
    Fixture fixture(GameID::KotOR);
    EXPECT_CALL(fixture.engine.resourceModule().twoDas(), get("baseitems"))
        .WillRepeatedly(Return(std::make_shared<TwoDA>(
            std::vector<std::string> {}, std::vector<TwoDA::Row> {})));
    auto a = inventory({{"common", 3}, {"a_only", 2}});
    auto b = inventory({{"common", 8}, {"b_only", 4}});

    TestGameModule::replaceInventory(fixture.game, *a);
    ASSERT_EQ(2u, fixture.game.party().player()->items().size());
    EXPECT_EQ("common", fixture.game.party().player()->items()[0]->tag());
    EXPECT_EQ(3, fixture.game.party().player()->items()[0]->stackSize());
    EXPECT_EQ("a_only", fixture.game.party().player()->items()[1]->tag());

    TestGameModule::replaceInventory(fixture.game, *b);
    ASSERT_EQ(2u, fixture.game.party().player()->items().size());
    EXPECT_EQ("common", fixture.game.party().player()->items()[0]->tag());
    EXPECT_EQ(8, fixture.game.party().player()->items()[0]->stackSize());
    EXPECT_EQ("b_only", fixture.game.party().player()->items()[1]->tag());

    TestGameModule::replaceInventory(fixture.game, *a);
    ASSERT_EQ(2u, fixture.game.party().player()->items().size());
    EXPECT_EQ("common", fixture.game.party().player()->items()[0]->tag());
    EXPECT_EQ(3, fixture.game.party().player()->items()[0]->stackSize());
    EXPECT_EQ("a_only", fixture.game.party().player()->items()[1]->tag());
}

TEST(SaveWideAvailableNpcs, a_b_a_replaces_indexed_working_state_records) {
    Fixture fixture(GameID::KotOR);
    EXPECT_CALL(fixture.engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(appearanceTable()));
    EXPECT_CALL(fixture.engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(fixture.engine.services().game.portraits),
                getTextureByAppearance(_))
        .Times(AnyNumber());

    auto loadNpc = [&](size_t index, std::string tag) {
        fixture.game.party().reset();
        auto table = availableTable(index);
        TestGameModule::deserializePartyTable(fixture.game, *table);
        EXPECT_CALL(fixture.engine.resourceModule().director(),
                    findSaveWorking(ResourceId(
                        str(boost::format("availnpc%d") % index), ResType::Utc)))
            .WillOnce(Return(encodedCreature(std::move(tag))));
        TestGameModule::deserializeAvailableNpcs(fixture.game);
    };

    loadNpc(0, "npc_a");
    ASSERT_TRUE(fixture.game.party().getAvailableMember(0));
    EXPECT_EQ("npc_a", fixture.game.party().getAvailableMember(0)->tag());

    loadNpc(1, "npc_b");
    EXPECT_FALSE(fixture.game.party().getAvailableMember(0));
    ASSERT_TRUE(fixture.game.party().getAvailableMember(1));
    EXPECT_EQ("npc_b", fixture.game.party().getAvailableMember(1)->tag());

    loadNpc(0, "npc_a");
    ASSERT_TRUE(fixture.game.party().getAvailableMember(0));
    EXPECT_EQ("npc_a", fixture.game.party().getAvailableMember(0)->tag());
    EXPECT_FALSE(fixture.game.party().getAvailableMember(1));
}

TEST(SaveWideAvailableNpcs, malformed_optional_record_is_ignored) {
    Fixture fixture(GameID::KotOR);
    auto table = availableTable(0);
    TestGameModule::deserializePartyTable(fixture.game, *table);
    EXPECT_CALL(fixture.engine.resourceModule().director(),
                findSaveWorking(ResourceId("availnpc0", ResType::Utc)))
        .WillOnce(Return(Resource {{'b', 'a', 'd'}}));

    TestGameModule::deserializeAvailableNpcs(fixture.game);

    EXPECT_FALSE(fixture.game.party().getAvailableMember(0));
}

TEST(SaveWideAvailablePuppets, k2_materializes_available_puppet_resource) {
    Fixture fixture(GameID::TSL);
    EXPECT_CALL(fixture.engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(appearanceTable()));
    EXPECT_CALL(fixture.engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(fixture.engine.services().game.portraits),
                getTextureByAppearance(_))
        .Times(AnyNumber());

    auto table = Gff::Builder()
                     .field(Gff::Field::newList("PT_AVAIL_PUPS", {availablePuppet(true, true)}))
                     .build();
    TestGameModule::deserializePartyTable(fixture.game, *table);
    EXPECT_CALL(fixture.engine.resourceModule().director(),
                findSaveWorking(ResourceId("availpup0", ResType::Utc)))
        .WillOnce(Return(encodedCreature("puppet_a")));

    TestGameModule::deserializeAvailableNpcs(fixture.game);
    ASSERT_TRUE(fixture.game.party().getAvailablePuppet(0));
    EXPECT_EQ("puppet_a", fixture.game.party().getAvailablePuppet(0)->tag());
}

TEST(SavedPlayerRestoration, primary_module_player_does_not_duplicate_pc_utc) {
    Fixture fixture(GameID::TSL);
    EXPECT_CALL(fixture.engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(appearanceTable()));
    EXPECT_CALL(fixture.engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(fixture.engine.services().game.portraits),
                getTextureByAppearance(_))
        .Times(AnyNumber());

    auto partyTable = emptyPartyTable();
    auto player = savedPlayer("primary", 100, true);
    auto ifo = Gff::Builder()
                   .field(Gff::Field::newList("Mod_PlayerList", {player}))
                   .build();

    TestGameModule::publishPartyRuntimeState(
        fixture.game, *ifo, partyTable, nullptr);

    ASSERT_TRUE(fixture.game.party().player());
    EXPECT_EQ(fixture.game.party().player(), fixture.game.party().actualPlayer());
    EXPECT_EQ(100u, fixture.game.party().player()->id());
    EXPECT_EQ(1, fixture.game.party().getSize());
}

TEST(SavedPlayerRestoration, controlled_companion_keeps_pc_utc_as_actual_player) {
    Fixture fixture(GameID::TSL);
    EXPECT_CALL(fixture.engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(appearanceTable()));
    EXPECT_CALL(fixture.engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(fixture.engine.services().game.portraits),
                getTextureByAppearance(_))
        .Times(AnyNumber());

    auto partyTable = Gff::Builder()
                          .field(Gff::Field::newInt("PT_CONTROLLED_NP", 0))
                          .field(Gff::Field::newByte("PT_NUM_MEMBERS", 1))
                          .field(Gff::Field::newList("PT_MEMBERS", {member(0, true)}))
                          .build();
    auto pc = Gff::Builder()
                  .field(Gff::Field::newCExoString("Tag", "actual_pc"))
                  .field(Gff::Field::newDword("Appearance_Type", 0))
                  .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                  .field(Gff::Field::newByte("BodyBag", 0xff))
                  .field(Gff::Field::newByte("PerceptionRange", 0xff))
                  .build();
    auto controlled = savedPlayer("controlled", 101, false);
    auto ifo = Gff::Builder()
                   .field(Gff::Field::newList("Mod_PlayerList", {controlled}))
                   .build();

    TestGameModule::publishPartyRuntimeState(
        fixture.game, *ifo, partyTable, pc);

    ASSERT_TRUE(fixture.game.party().player());
    ASSERT_TRUE(fixture.game.party().actualPlayer());
    EXPECT_NE(fixture.game.party().player(), fixture.game.party().actualPlayer());
    EXPECT_EQ(101u, fixture.game.party().player()->id());
    EXPECT_EQ(fixture.game.party().player(), fixture.game.party().getLeader());
    EXPECT_EQ("actual_pc", fixture.game.party().actualPlayer()->tag());
    EXPECT_EQ(fixture.game.party().actualPlayer(),
              fixture.game.party().getMemberByNPC(kNpcPlayer));
    EXPECT_EQ(2, fixture.game.party().getSize());
}
