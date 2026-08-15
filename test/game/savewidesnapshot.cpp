/* Copyright (c) 2026 The reone project contributors */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/saveprovenance.h"
#include "reone/game/savewidesnapshot.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/gvt.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

std::shared_ptr<Gff> readGff(const ByteBuffer &bytes) {
    ByteBuffer copy(bytes);
    MemoryInputStream stream(copy);
    GffReader reader(stream);
    reader.load();
    return reader.root();
}

IReputes::State factionState() {
    IReputes::State state;
    state.factions = {
        {"player", UINT32_MAX, true},
        {"hostile", 0, true},
        {"future", 1, false},
    };
    state.labels = {"player", "hostile", "future"};
    state.values = {
        {100, 100, 100},
        {12, 100, 77},
        {91, 33, 100},
    };
    return state;
}

SaveMetadataInput metadata(bool tsl) {
    SaveMetadataInput result;
    result.displayName = "The Exile — Δ";
    result.cheatUsed = 2;
    result.gameplayHint = 22;
    result.storyHint = 14;
    result.storyHints = {7, 31, 36, 52, 0, 0, 0, 82, 0, 0};
    result.liveContentNames[1] = "live-two";
    result.liveContent = 3;
    result.portraits = {tsl ? "po_pfhh01" : "po_pmhc04", "po_party1", ""};
    result.timestamp = 130950622286030000ULL;
    result.saveNumber = 96;
    return result;
}

struct RichState {
    std::shared_ptr<Creature> player;
    std::shared_ptr<Creature> npc;
    std::shared_ptr<Creature> puppet;
    std::shared_ptr<Item> item;
    std::shared_ptr<Location> location;
};

RichState configureRich(Game &game, bool tsl) {
    RichState result;
    auto area = game.newArea();
    result.player = game.newCreature();
    result.player->setName(tsl ? "Meetra" : "Revan");
    result.player->setMaxHitPoints(40);
    result.player->setCurrentHitPoints(17);
    TestGameModule::configureModuleSnapshot(
        game, area, result.player, tsl ? "003ebo" : "ebo_m12aa",
        tsl ? "003ebo" : "ebo_m12aa");

    Party::PersistedState state;
    state.pcName = tsl ? "Meetra" : "";
    state.itemComponent = 41;
    state.itemChemical = 17;
    state.swoopUpgrades = {1, 2, 3};
    state.playedSeconds = 56266;
    state.soloMode = true;
    state.npcAvailable[0] = true;
    state.npcSelectable[0] = false;
    state.influence[0] = 73;
    state.aiState = 2;
    state.followState = 3;
    state.galaxyPointCount = 16;
    state.planetAvailable[2] = true;
    state.planetSelectable[4] = true;
    state.selectedPlanet = 4;
    state.mapDisabled = true;
    state.regenerationDisabled = true;
    state.dialogMessages.push_back({"Carth", "We should go."});
    state.feedbackMessages.push_back({2, 5, "Feedback"});
    state.combatMessages.push_back({3, 7, "Combat"});
    if (tsl) {
        state.puppetAvailable[0] = true;
        state.puppetSelectable[0] = false;
        state.puppetIds.push_back(0);
    }
    game.party().setPersistedState(state);
    game.party().giveGold(1234);
    game.party().setXP(4321);
    Party::PazaakCardCounts cards {};
    const size_t cardCount = tsl ? Party::kK2PazaakCardCount
                                 : Party::kK1PazaakCardCount;
    for (size_t i = 0; i < cardCount; ++i) cards[i] = static_cast<int>(i % 4);
    Party::PazaakSideDeck sideDeck;
    sideDeck.fill(-1);
    game.party().setPazaakData(cards, sideDeck, cardCount);

    result.npc = game.newCreature();
    result.npc->setName("Companion");
    auto npcShadow = Gff::Builder().type(0xffffffff)
                         .field(Gff::Field::newCExoString(
                             "FutureCreature", "npc-shadow"))
                         .field(Gff::Field::newShort("CurrentHitPoints", 1))
                         .build();
    result.npc->captureSaveRecord(
        *npcShadow, {SaveRecordOriginKind::AvailableNpc, "0"});
    result.npc->setCurrentHitPoints(19);
    game.party().addAvailableMember(0, result.npc);
    game.party().clear();
    game.party().addMember(0, result.npc);
    game.party().addMember(kNpcPlayer, result.player);

    if (tsl) {
        result.puppet = game.newCreature();
        auto puppetShadow = Gff::Builder().type(0xffffffff)
                                .field(Gff::Field::newCExoString(
                                    "FuturePuppet", "puppet-shadow"))
                                .build();
        result.puppet->captureSaveRecord(
            *puppetShadow, {SaveRecordOriginKind::AvailablePuppet, "0"});
        game.party().addAvailablePuppet(0, result.puppet);
    }

    result.item = game.newOwnedItem();
    auto itemShadow = Gff::Builder().type(0)
                          .field(Gff::Field::newDword("ObjectId", 77))
                          .field(Gff::Field::newInt("BaseItem", 5))
                          .field(Gff::Field::newByte("Charges", 9))
                          .field(Gff::Field::newWord("StackSize", 3))
                          .field(Gff::Field::newCExoString(
                              "FutureItem", "item-shadow"))
                          .build();
    result.item->setStackSize(3);
    result.item->captureOwnerLocalSaveRecord(
        *itemShadow, {SaveRecordOriginKind::PartyInventoryItem, "inventory"});
    result.player->addItem(result.item);

    game.journal().restoreEntry("tat17_landing", 30, 4, 500);
    for (int i = 0; i < 9; ++i) {
        game.setGlobalBoolean("bool_" + std::to_string(i), (i % 2) == 0);
    }
    game.setGlobalNumber("number_max", 255);
    game.setGlobalNumber("number_zero", 0);
    game.setGlobalString("string_empty", "");
    game.setGlobalString("string_value", "value");
    result.location = std::make_shared<Location>(
        glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.25f, -0.75f, 0.5f));
    game.setGlobalLocation("location", result.location);

    auto globalShadow = Gff::Builder().type(0xffffffff)
                            .field(Gff::Field::newCExoString(
                                "FutureGlobal", "global-shadow"))
                            .build();
    auto partyShadow = Gff::Builder().type(0xffffffff)
                           .field(Gff::Field::newCExoString(
                               "FutureParty", "party-shadow"))
                           .field(Gff::Field::newInt("JNL_SortOrder", 2))
                           .build();
    auto staleItem = Gff::Builder().type(0)
                         .field(Gff::Field::newCExoString(
                             "StaleItem", "must-not-survive"))
                         .build();
    auto inventoryShadow = Gff::Builder().type(0xffffffff)
                               .field(Gff::Field::newCExoString(
                                   "FutureInventory", "inventory-shadow"))
                               .field(Gff::Field::newList("ItemList", {staleItem}))
                               .build();
    auto futureFaction = Gff::Builder().type(0)
                             .field(Gff::Field::newCExoString(
                                 "FutureDefinition", "definition-shadow"))
                             .build();
    auto factionShadow = Gff::Builder().type(0xffffffff)
                             .field(Gff::Field::newCExoString(
                                 "FutureFaction", "faction-shadow"))
                             .field(Gff::Field::newList(
                                 "FactionList", {futureFaction}))
                             .build();
    auto nfoShadow = Gff::Builder().type(0xffffffff)
                         .field(Gff::Field::newCExoString(
                             "SAVEGAMENAME", "stale-name"))
                         .field(Gff::Field::newCExoString(
                             "FutureNfo", "nfo-shadow"))
                         .field(Gff::Field::newByte("PCAUTOSAVE", 1))
                         .build();
    game.captureSaveResourceShadow({SaveResourceKind::GlobalVars, {}}, *globalShadow);
    game.captureSaveResourceShadow({SaveResourceKind::PartyTable, {}}, *partyShadow);
    game.captureSaveResourceShadow({SaveResourceKind::Inventory, {}}, *inventoryShadow);
    game.captureSaveResourceShadow({SaveResourceKind::FactionTable, {}}, *factionShadow);
    game.captureSaveResourceShadow({SaveResourceKind::Nfo, {}}, *nfoShadow);
    return result;
}

void configureReputes(TestEngine &engine) {
    auto &reputes = static_cast<MockReputes &>(engine.services().game.reputes);
    EXPECT_CALL(reputes, baseState())
        .Times(AnyNumber())
        .WillRepeatedly(Return(factionState()));
    EXPECT_CALL(reputes, state())
        .Times(AnyNumber())
        .WillRepeatedly(Return(factionState()));
}

struct TempArchive {
    std::filesystem::path path;

    TempArchive() {
        path = std::filesystem::temp_directory_path() /
               "reone_e3e_candidate_application.sav";
        ErfWriter writer;
        writer.add({"inventory", ResType::Res, {'o', 'l', 'd'}});
        writer.add({"repute", ResType::Fac, {'o', 'l', 'd'}});
        writer.add({"pc", ResType::Utc, {'o', 'l', 'd'}});
        writer.add({"availnpc7", ResType::Utc, {'o', 'l', 'd'}});
        writer.add({"availpup2", ResType::Utc, {'o', 'l', 'd'}});
        writer.add({"mod_extension", ResType::Res, {'k', 'e', 'e', 'p'}});
        writer.add({"inactive", ResType::Sav, {'m', 'o', 'd'}});
        FileOutputStream stream(path);
        writer.save(ErfWriter::FileType::MOD, stream);
    }

    ~TempArchive() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

} // namespace

TEST(SaveWideSnapshot, rich_k1_round_trips_all_common_state_and_shadows) {
    auto &engine = testEngine();
    configureReputes(engine);
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto rich = configureRich(game, false);

    auto first = SaveWideSnapshotBuilder(game, metadata(false)).build();
    auto second = SaveWideSnapshotBuilder(game, metadata(false)).build();

    ASSERT_TRUE(first) << first.message;
    ASSERT_TRUE(second) << second.message;
    EXPECT_EQ(first.snapshot->looseSlotResources, second.snapshot->looseSlotResources);
    EXPECT_EQ(first.snapshot->outerWorkingResources, second.snapshot->outerWorkingResources);
    EXPECT_EQ(first.snapshot->looseSlotResources.size(), 3);
    EXPECT_FALSE(first.snapshot->outerWorkingResources.count({"pc", ResType::Utc}));
    EXPECT_FALSE(first.snapshot->outerWorkingResources.count({"availpup0", ResType::Utc}));

    auto globals = readGff(first.snapshot->looseSlotResources.at({"globalvars", ResType::Res}));
    ASSERT_EQ(globals->signature(), std::optional<std::string>("GVT V3.2"));
    EXPECT_EQ(globals->getData("ValBoolean"),
              (ByteBuffer {static_cast<char>(0xaa), static_cast<char>(0x80)}));
    EXPECT_EQ(globals->getData("ValNumber"),
              (ByteBuffer {static_cast<char>(255), 0}));
    EXPECT_EQ(globals->getData("ValLocation").size(), 2400);
    EXPECT_EQ(globals->getString("FutureGlobal"), "global-shadow");
    auto parsedGlobals = parseGVT(*globals);
    ASSERT_EQ(parsedGlobals.locations.size(), 1);
    EXPECT_EQ(parsedGlobals.locations[0].second.second, glm::vec3(0.25f, -0.75f, 0.5f));

    auto party = readGff(first.snapshot->looseSlotResources.at({"partytable", ResType::Res}));
    EXPECT_EQ(party->signature(), std::optional<std::string>("PT  V3.2"));
    EXPECT_EQ(party->getList("PT_AVAIL_NPCS").size(), 9);
    EXPECT_EQ(party->getList("PT_PAZAAKCARDS").size(), 19);
    EXPECT_EQ(party->getUint("PT_GOLD"), 1234u);
    EXPECT_EQ(party->getInt("PT_XP_POOL"), 4321);
    EXPECT_EQ(party->getList("JNL_Entries").size(), 1);
    EXPECT_EQ(party->getString("FutureParty"), "party-shadow");
    EXPECT_FALSE(party->has("PT_INFLUENCE"));
    Game loaded(GameID::KotOR, "", engine.options(), engine.services(), console);
    TestGameModule::deserializePartyTable(loaded, *party);
    EXPECT_EQ(loaded.party().persistedState().playedSeconds, 56266u);
    ASSERT_EQ(loaded.party().persistedState().memberIds.size(), 1);
    EXPECT_EQ(loaded.party().persistedState().memberIds[0], 0);
    EXPECT_EQ(loaded.party().persistedState().leader, 0);
    EXPECT_EQ(loaded.party().pazaakCardCount(), 19);
    loaded.setGlobalBoolean("stale_from_a", true);
    TestGameModule::deserializeGlobalVariables(loaded, *globals);
    EXPECT_FALSE(loaded.globalBooleans().count("stale_from_a"));
    EXPECT_TRUE(loaded.getGlobalBoolean("bool_0"));
    EXPECT_EQ(loaded.getGlobalNumber("number_max"), 255);
    TestGameModule::replaceJournal(loaded, *party);
    ASSERT_EQ(loaded.journal().quests().size(), 1);
    EXPECT_EQ(loaded.journal().quests()[0].plotId, "tat17_landing");

    auto inventory = readGff(first.snapshot->outerWorkingResources.at({"inventory", ResType::Res}));
    EXPECT_EQ(inventory->signature(), std::optional<std::string>("INV V3.2"));
    ASSERT_EQ(inventory->getList("ItemList").size(), 1);
    EXPECT_FALSE(inventory->getList("ItemList")[0]->has("ObjectId"));
    EXPECT_EQ(inventory->getList("ItemList")[0]->getUint("StackSize"), 3u);
    EXPECT_EQ(inventory->getList("ItemList")[0]->getString("FutureItem"), "item-shadow");
    EXPECT_EQ(inventory->getString("FutureInventory"), "inventory-shadow");

    auto fac = readGff(first.snapshot->outerWorkingResources.at({"repute", ResType::Fac}));
    EXPECT_EQ(fac->signature(), std::optional<std::string>("FAC V3.2"));
    ASSERT_EQ(fac->getList("FactionList").size(), 3);
    EXPECT_EQ(fac->getList("FactionList")[2]->getUint("FactionParentID"), 1u);
    EXPECT_FALSE(fac->getList("FactionList")[2]->getBool("FactionGlobal"));
    EXPECT_EQ(fac->getList("FactionList")[0]->getString("FutureDefinition"),
              "definition-shadow");
    ASSERT_EQ(fac->getList("RepList").size(), 4);
    EXPECT_EQ(fac->getList("RepList")[0]->getUint("FactionID1"), 0u);
    EXPECT_EQ(fac->getList("RepList")[0]->getUint("FactionID2"), 1u);
    EXPECT_EQ(fac->getList("RepList")[0]->getUint("FactionRep"), 12u);

    auto npc = readGff(first.snapshot->outerWorkingResources.at({"availnpc0", ResType::Utc}));
    EXPECT_FALSE(npc->has("ObjectId"));
    EXPECT_EQ(npc->getInt("CurrentHitPoints"), 19);
    EXPECT_EQ(npc->getString("FutureCreature"), "npc-shadow");
    auto nfo = readGff(first.snapshot->looseSlotResources.at({"savenfo", ResType::Res}));
    EXPECT_EQ(nfo->getString("SAVEGAMENAME"), metadata(false).displayName);
    EXPECT_EQ(nfo->getString("LASTMODULE"), "ebo_m12aa");
    EXPECT_EQ(nfo->getUint("TIMEPLAYED"), 56266u);
    EXPECT_TRUE(nfo->has("STORYHINT"));
    EXPECT_FALSE(nfo->has("TIMESTAMP"));
    EXPECT_FALSE(nfo->has("PCAUTOSAVE"));
    EXPECT_EQ(nfo->getString("FutureNfo"), "nfo-shadow");

    bool last = false;
    rich.item->setStackSize(1);
    ASSERT_TRUE(rich.player->removeItem(rich.item, last));
    EXPECT_TRUE(last);
    auto withoutItem = SaveWideSnapshotBuilder(game, metadata(false)).build();
    ASSERT_TRUE(withoutItem) << withoutItem.message;
    auto emptyInventory = readGff(
        withoutItem.snapshot->outerWorkingResources.at({"inventory", ResType::Res}));
    EXPECT_TRUE(emptyInventory->getList("ItemList").empty());
    EXPECT_EQ(emptyInventory->getString("FutureInventory"), "inventory-shadow");

    rich.location->setFacing(glm::half_pi<float>());
    auto changed = SaveWideSnapshotBuilder(game, metadata(false)).build();
    ASSERT_TRUE(changed) << changed.message;
    auto changedGvt = readGff(changed.snapshot->looseSlotResources.at({"globalvars", ResType::Res}));
    auto changedGlobals = parseGVT(*changedGvt);
    ASSERT_EQ(changedGlobals.locations.size(), 1);
    EXPECT_NEAR(changedGlobals.locations[0].second.second.x, 0.0f, 0.00001f);
    EXPECT_NEAR(changedGlobals.locations[0].second.second.y, 1.0f, 0.00001f);
    EXPECT_FLOAT_EQ(changedGlobals.locations[0].second.second.z, 0.0f);
}

TEST(SaveWideSnapshot, k1_pc_utc_is_present_only_for_companion_control) {
    auto &engine = testEngine();
    configureReputes(engine);
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto rich = configureRich(game, false);
    auto state = game.party().persistedState();
    state.controlledNpc = 0;
    game.party().setPersistedState(state);
    game.party().setPlayer(rich.npc);
    game.party().setActualPlayer(rich.player);

    auto saved = SaveWideSnapshotBuilder(game, metadata(false)).build();

    ASSERT_TRUE(saved) << saved.message;
    ASSERT_TRUE(saved.snapshot->outerWorkingResources.count({"pc", ResType::Utc}));
    auto pc = readGff(saved.snapshot->outerWorkingResources.at({"pc", ResType::Utc}));
    EXPECT_FALSE(pc->has("ObjectId"));
    EXPECT_EQ(pc->getInt("CurrentHitPoints"), 17);
    auto party = readGff(saved.snapshot->looseSlotResources.at({"partytable", ResType::Res}));
    EXPECT_EQ(party->getInt("PT_CONTROLLED_NP"), 0);
}

TEST(SaveWideSnapshot, rich_k2_writes_title_specific_party_pc_puppet_and_nfo) {
    auto &engine = testEngine();
    configureReputes(engine);
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto rich = configureRich(game, true);

    auto saved = SaveWideSnapshotBuilder(game, metadata(true)).build();

    ASSERT_TRUE(saved) << saved.message;
    auto party = readGff(saved.snapshot->looseSlotResources.at({"partytable", ResType::Res}));
    EXPECT_EQ(party->getList("PT_AVAIL_NPCS").size(), 12);
    EXPECT_EQ(party->getList("PT_PAZAAKCARDS").size(), 24);
    EXPECT_EQ(party->getList("PT_AVAIL_PUPS").size(), 3);
    EXPECT_EQ(party->getList("PT_INFLUENCE").size(), 12);
    EXPECT_EQ(party->getUint("PT_ITEM_COMPONEN"), 41u);
    EXPECT_EQ(party->getUint("PT_ITEM_CHEMICAL"), 17u);
    EXPECT_TRUE(party->getBool("PT_DISABLEMAP"));
    EXPECT_EQ(party->getList("PT_COM_MSG_LIST").size(), 1);
    Game loaded(GameID::TSL, "", engine.options(), engine.services(), console);
    TestGameModule::deserializePartyTable(loaded, *party);
    EXPECT_EQ(loaded.party().persistedState().influence[0], 73);
    EXPECT_TRUE(loaded.party().persistedState().puppetAvailable[0]);
    EXPECT_EQ(loaded.party().pazaakCardCount(), 24);

    ASSERT_TRUE(saved.snapshot->outerWorkingResources.count({"pc", ResType::Utc}));
    ASSERT_TRUE(saved.snapshot->outerWorkingResources.count({"availpup0", ResType::Utc}));
    auto pc = readGff(saved.snapshot->outerWorkingResources.at({"pc", ResType::Utc}));
    EXPECT_EQ(pc->signature(), std::optional<std::string>("UTC V3.2"));
    EXPECT_FALSE(pc->has("ObjectId"));
    EXPECT_EQ(pc->getInt("CurrentHitPoints"), 17);
    auto loadedPc = game.newCreature();
    loadedPc->setMaxHitPoints(pc->getInt("MaxHitPoints"));
    loadedPc->setCurrentHitPoints(pc->getInt("CurrentHitPoints"));
    EXPECT_EQ(loadedPc->currentHitPoints(), 17);
    loadedPc->restorePrimaryPlayerHitPoints();
    EXPECT_EQ(loadedPc->currentHitPoints(), loadedPc->maxHitPoints());
    auto puppet = readGff(saved.snapshot->outerWorkingResources.at({"availpup0", ResType::Utc}));
    EXPECT_EQ(puppet->getString("FuturePuppet"), "puppet-shadow");

    auto nfo = readGff(saved.snapshot->looseSlotResources.at({"savenfo", ResType::Res}));
    EXPECT_EQ(nfo->signature(), std::optional<std::string>("NFO V3.2"));
    EXPECT_EQ(nfo->getUint64("TIMESTAMP"), metadata(true).timestamp);
    EXPECT_EQ(nfo->getUint("SAVENUMBER"), 96u);
    EXPECT_EQ(nfo->getString("PCNAME"), "Meetra");
    EXPECT_EQ(nfo->getUint("STORYHINT7"), 82u);
    EXPECT_FALSE(nfo->has("STORYHINT"));
    EXPECT_EQ(nfo->getString("PORTRAIT0"), "po_pfhh01");
}

TEST(SaveWideSnapshot, application_replaces_current_tombstones_stale_and_keeps_unknowns) {
    auto &engine = testEngine();
    configureReputes(engine);
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    configureRich(game, false);
    auto saved = SaveWideSnapshotBuilder(game, metadata(false)).build();
    ASSERT_TRUE(saved) << saved.message;

    TempArchive archive;
    auto base = std::make_shared<SaveWorkingState>(archive.path);
    auto candidate = SaveWorkingStateCandidate::fromCommitted(base);
    saved.snapshot->applyTo(candidate);

    EXPECT_TRUE(candidate.contains({"inventory", ResType::Res}));
    EXPECT_TRUE(candidate.contains({"repute", ResType::Fac}));
    EXPECT_TRUE(candidate.contains({"availnpc0", ResType::Utc}));
    EXPECT_FALSE(candidate.contains({"pc", ResType::Utc}));
    EXPECT_FALSE(candidate.contains({"availnpc7", ResType::Utc}));
    EXPECT_FALSE(candidate.contains({"availpup2", ResType::Utc}));
    EXPECT_TRUE(candidate.contains({"mod_extension", ResType::Res}));
    EXPECT_TRUE(candidate.contains({"inactive", ResType::Sav}));
    EXPECT_TRUE(candidate.validate());
}

TEST(SaveWideSnapshot, failed_build_exposes_no_partial_result_or_mutation) {
    auto &engine = testEngine();
    configureReputes(engine);
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    configureRich(game, false);
    game.setGlobalNumber("invalid", 256);
    const auto beforeGlobals = game.globalNumbers();

    auto failed = SaveWideSnapshotBuilder(game, metadata(false)).build();

    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.error, SaveWideSnapshotError::UnsupportedLiveState);
    EXPECT_FALSE(failed.snapshot);
    EXPECT_EQ(game.globalNumbers(), beforeGlobals);
    EXPECT_EQ(game.party().gold(), 1234);
    EXPECT_EQ(game.party().actualPlayer()->currentHitPoints(), 17);
}

TEST(SaveWideSnapshot, authored_player_source_reputation_is_derived_but_mutation_is_rejected) {
    auto &engine = testEngine();
    auto &reputes = static_cast<MockReputes &>(engine.services().game.reputes);
    auto base = factionState();
    base.values[0][1] = 55;
    auto live = base;
    EXPECT_CALL(reputes, baseState())
        .Times(AnyNumber())
        .WillRepeatedly(Return(base));
    EXPECT_CALL(reputes, state()).WillOnce(Return(live));

    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    configureRich(game, false);
    auto preserved = SaveWideSnapshotBuilder(game, metadata(false)).build();
    ASSERT_TRUE(preserved) << preserved.message;

    live.values[0][1] = 54;
    EXPECT_CALL(reputes, state()).WillOnce(Return(live));
    auto rejected = SaveWideSnapshotBuilder(game, metadata(false)).build();
    EXPECT_FALSE(rejected);
    EXPECT_EQ(SaveWideSnapshotError::UnsupportedLiveState, rejected.error);
    EXPECT_THAT(rejected.message, HasSubstr("modified player-source reputation"));
}
