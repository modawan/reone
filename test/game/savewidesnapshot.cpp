/* Copyright (c) 2026 The reone project contributors */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/saveprovenance.h"
#include "reone/game/script/routines.h"
#include "reone/game/savewidesnapshot.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/gvt.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/script/executioncontext.h"

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

std::shared_ptr<TwoDA> makeRoundTripBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags",
                     "dietoroll", "equipableslots", "itemclass", "numdice",
                     "weapontype", "weaponwield", "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= 5; ++i) {
        builder.row({"", "", "", "", "", "", "I_Test", "", "", "", "", ""});
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> makeRoundTripAppearanceTable() {
    TwoDA::Builder builder;
    builder.columns({"modeltype", "walkdist", "rundist", "footsteptype",
                     "envmap", "race", "racetex"});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
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
    game.party().galaxyMap().reset(
        tsl ? GameID::TSL : GameID::KotOR, Party::kGalaxyPlanetCount);
    game.party().galaxyMap().setAvailable(2, true);
    game.party().galaxyMap().setSelectable(4, true);
    game.party().galaxyMap().restoreSelectedPlanet(4);
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
        *npcShadow,
        SerializedIdentityContext::detachedRecord("availnpc0.utc"),
        {SaveRecordOriginKind::AvailableNpc, "0"});
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
            *puppetShadow,
            SerializedIdentityContext::detachedRecord("availpup0.utc"),
            {SaveRecordOriginKind::AvailablePuppet, "0"});
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
    result.item->captureSaveRecord(
        *itemShadow,
        SerializedIdentityContext::detachedRecord("inventory.res"),
        {SaveRecordOriginKind::PartyInventoryItem, "inventory"});
    result.player->addItem(result.item);

    game.journal().restoreEntry("tat17_landing", 30, 4, 500);
    for (int i = 0; i < 9; ++i) {
        game.setGlobalBoolean("bool_" + std::to_string(i), (i % 2) == 0);
    }
    game.setGlobalNumber("number_negative_one", -1);
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
    EXPECT_EQ(loaded.getGlobalNumber("number_negative_one"), -1);
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

    Game reloaded(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto reloadedRich = configureRich(reloaded, false);
    bool removedLast = false;
    reloadedRich.item->setStackSize(1);
    ASSERT_TRUE(reloadedRich.player->removeItem(reloadedRich.item, removedLast));
    ASSERT_TRUE(removedLast);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeRoundTripBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    TestGameModule::deserializeInventory(reloaded, *inventory);
    ASSERT_EQ(reloadedRich.player->items().size(), 1);
    EXPECT_EQ(reloadedRich.player->items().front()->stackSize(), 3);

    auto roundTripped = SaveWideSnapshotBuilder(reloaded, metadata(false)).build();
    ASSERT_TRUE(roundTripped) << roundTripped.message;
    auto roundTripInventory = readGff(
        roundTripped.snapshot->outerWorkingResources.at({"inventory", ResType::Res}));
    ASSERT_EQ(roundTripInventory->getList("ItemList").size(), 1);
    EXPECT_FALSE(roundTripInventory->getList("ItemList").front()->has("ObjectId"));
    EXPECT_EQ(roundTripInventory->getList("ItemList").front()->getUint("StackSize"), 3u);

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

TEST(SaveWideSnapshot, thirteen_item_inventory_round_trip_is_singular_and_semantic) {
    auto &engine = testEngine();
    configureReputes(engine);
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto rich = configureRich(game, false);
    std::vector<uint32_t> expectedStacks {3};
    for (uint32_t index = 1; index < 13; ++index) {
        auto item = game.newOwnedItem();
        auto record = Gff::Builder().type(0)
            .field(Gff::Field::newInt("BaseItem", 5))
            .field(Gff::Field::newWord("StackSize", index + 1))
            .field(Gff::Field::newCExoString("Tag", "roundtrip_" + std::to_string(index)))
            .build();
        item->deserializeRuntimeState(
            *record,
            SerializedIdentityContext::detachedRecord("inventory.res"));
        item->setTag("roundtrip_" + std::to_string(index));
        item->setStackSize(index + 1);
        item->captureSaveRecord(
            *record,
            SerializedIdentityContext::detachedRecord("inventory.res"),
            {SaveRecordOriginKind::PartyInventoryItem, "inventory"});
        rich.player->addItem(item);
        expectedStacks.push_back(index + 1);
    }

    auto first = SaveWideSnapshotBuilder(game, metadata(false)).build();
    ASSERT_TRUE(first) << first.message;
    auto inventory = readGff(
        first.snapshot->outerWorkingResources.at({"inventory", ResType::Res}));
    ASSERT_EQ(inventory->getList("ItemList").size(), 13);
    for (const auto &record : inventory->getList("ItemList")) {
        EXPECT_FALSE(record->has("ObjectId"));
    }

    Game reloaded(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto reloadedRich = configureRich(reloaded, false);
    bool removedLast = false;
    reloadedRich.item->setStackSize(1);
    ASSERT_TRUE(reloadedRich.player->removeItem(reloadedRich.item, removedLast));
    ASSERT_TRUE(removedLast);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeRoundTripBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    TestGameModule::deserializeInventory(reloaded, *inventory);

    std::vector<uint32_t> actualStacks;
    for (const auto &item : reloadedRich.player->items()) {
        actualStacks.push_back(item->stackSize());
    }
    std::sort(expectedStacks.begin(), expectedStacks.end());
    std::sort(actualStacks.begin(), actualStacks.end());
    EXPECT_EQ(actualStacks, expectedStacks);

    auto second = SaveWideSnapshotBuilder(reloaded, metadata(false)).build();
    ASSERT_TRUE(second) << second.message;
    auto secondInventory = readGff(
        second.snapshot->outerWorkingResources.at({"inventory", ResType::Res}));
    ASSERT_EQ(secondInventory->getList("ItemList").size(), 13);
    for (const auto &record : secondInventory->getList("ItemList")) {
        EXPECT_FALSE(record->has("ObjectId"));
    }
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

TEST(SaveWideSnapshot,
     k2_zero_member_controlled_npc_round_trips_canonical_and_controlled_players) {
    TestEngine engine;
    engine.init();
    configureReputes(engine);
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);

    auto area = game.newArea();
    auto controlled = game.newCreature();
    controlled->setName("T3-M4");
    controlled->setTag("t3m4");
    controlled->setMaxHitPoints(30);
    controlled->setCurrentHitPoints(24);
    auto controlledShadow =
        Gff::Builder()
            .type(0xffffffff)
            .field(Gff::Field::newDword("ObjectId", 330))
            .field(Gff::Field::newCExoLocString("FirstName", -1, "T3-M4"))
            .build();
    controlled->captureSaveRecord(
        *controlledShadow,
        SerializedIdentityContext::moduleGraph("106per"),
        {SaveRecordOriginKind::ModulePlayer, {}});
    TestGameModule::configureModuleSnapshot(
        game, area, controlled, "106per", "106per");

    auto canonical = game.newCreature();
    canonical->setName("Ta'ahn Kaast");
    canonical->setTag("canonical_pc");
    canonical->setMaxHitPoints(47);
    canonical->setCurrentHitPoints(10);
    auto canonicalShadow =
        Gff::Builder()
            .type(0xffffffff)
            .field(Gff::Field::newCExoLocString("FirstName", -1, "Ta'ahn Kaast"))
            .build();
    canonical->captureSaveRecord(
        *canonicalShadow,
        SerializedIdentityContext::detachedRecord("pc.utc"),
        {SaveRecordOriginKind::PrimaryPlayerUtc, {}});

    Party::PersistedState state;
    state.pcName = "Ta'ahn Kaast";
    state.controlledNpc = 8;
    state.soloMode = false;
    state.npcAvailable[8] = true;
    state.npcSelectable[8] = true;
    game.party().setPersistedState(state);
    Party::PazaakCardCounts cards {};
    Party::PazaakSideDeck sideDeck;
    sideDeck.fill(-1);
    game.party().setPazaakData(cards, sideDeck, Party::kK2PazaakCardCount);
    game.party().setActualPlayer(canonical);
    ASSERT_TRUE(game.party().addAvailableMember(8, controlled));
    game.party().clear();
    ASSERT_TRUE(game.party().addMember(8, controlled));
    game.party().setPlayer(controlled);
    ASSERT_EQ(controlled, game.party().getAvailableMember(8));

    auto module = ModuleSnapshotBuilder(game, "game13").build();
    auto saved = SaveWideSnapshotBuilder(game, metadata(true)).build();

    ASSERT_TRUE(module) << module.message;
    ASSERT_TRUE(saved) << saved.message;
    auto party = readGff(
        saved.snapshot->looseSlotResources.at({"partytable", ResType::Res}));
    auto pc = readGff(
        saved.snapshot->outerWorkingResources.at({"pc", ResType::Utc}));
    EXPECT_EQ(8, party->getInt("PT_CONTROLLED_NP"));
    EXPECT_EQ(0u, party->getUint("PT_NUM_MEMBERS"));
    EXPECT_TRUE(party->getList("PT_MEMBERS").empty());
    EXPECT_FALSE(party->getBool("PT_SOLOMODE"));
    EXPECT_EQ("Ta'ahn Kaast", pc->getString("FirstName"));
    EXPECT_FALSE(pc->has("ObjectId"));
    ASSERT_EQ(1u, module.snapshot->ifo->getList("Mod_PlayerList").size());
    EXPECT_EQ("t3m4",
              module.snapshot->ifo->getList("Mod_PlayerList")[0]->getString("Tag"));

    Game reloaded(GameID::TSL, "", engine.options(), engine.services(), console);
    auto reloadedArea = reloaded.newArea();
    auto placeholder = reloaded.newCreature();
    TestGameModule::configureModuleSnapshot(
        reloaded, reloadedArea, placeholder, "106per", "106per");
    reloaded.party().clear();
    TestGameModule::deserializePartyTable(reloaded, *party);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeRoundTripAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits),
                getTextureByAppearance(_))
        .Times(AnyNumber());
    EXPECT_CALL(
        engine.resourceModule().director(),
        findSaveWorking(ResourceId("availnpc8", ResType::Utc)))
        .Times(0);
    auto reloadedIfo = readGff(module.snapshot->ifoBytes);
    TestGameModule::publishPartyRuntimeState(
        reloaded, *reloadedIfo, party, pc);

    ASSERT_TRUE(reloaded.party().player());
    ASSERT_TRUE(reloaded.party().actualPlayer());
    EXPECT_NE(reloaded.party().player(), reloaded.party().actualPlayer());
    EXPECT_EQ("t3m4", reloaded.party().player()->tag());
    EXPECT_EQ("T3-M4", reloaded.party().player()->name());
    EXPECT_EQ("canonical_pc", reloaded.party().actualPlayer()->tag());
    EXPECT_EQ("Ta'ahn Kaast", reloaded.party().actualPlayer()->name());
    EXPECT_EQ(reloaded.party().player(), reloaded.party().getLeader());
    EXPECT_EQ(reloaded.party().player(), reloaded.party().getMemberByNPC(8));
    EXPECT_FALSE(reloaded.party().isMember(*reloaded.party().actualPlayer()));
    EXPECT_EQ(1, reloaded.party().getSize());
    EXPECT_EQ(8, reloaded.party().persistedState().controlledNpc);
    EXPECT_TRUE(reloaded.party().persistedState().npcAvailable[8]);
    EXPECT_TRUE(reloaded.party().persistedState().npcSelectable[8]);

    auto rebuiltModule = ModuleSnapshotBuilder(reloaded, "game13").build();
    auto rebuilt = SaveWideSnapshotBuilder(reloaded, metadata(true)).build();
    ASSERT_TRUE(rebuiltModule) << rebuiltModule.message;
    ASSERT_TRUE(rebuilt) << rebuilt.message;
    auto rebuiltParty = readGff(
        rebuilt.snapshot->looseSlotResources.at({"partytable", ResType::Res}));
    auto rebuiltPc = readGff(
        rebuilt.snapshot->outerWorkingResources.at({"pc", ResType::Utc}));
    EXPECT_EQ(8, rebuiltParty->getInt("PT_CONTROLLED_NP"));
    EXPECT_EQ(0u, rebuiltParty->getUint("PT_NUM_MEMBERS"));
    EXPECT_TRUE(rebuiltParty->getList("PT_MEMBERS").empty());
    EXPECT_EQ("Ta'ahn Kaast", rebuiltPc->getString("FirstName"));
    ASSERT_EQ(1u, rebuiltModule.snapshot->ifo->getList("Mod_PlayerList").size());
    EXPECT_EQ("t3m4",
              rebuiltModule.snapshot->ifo->getList("Mod_PlayerList")[0]->getString("Tag"));
}

TEST(GlobalNumber, script_api_uses_retail_signed_low_byte_for_both_titles) {
    auto &engine = testEngine();
    StubConsole console;
    script::ExecutionContext execution;

    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        Game game(gameId, "", engine.options(), engine.services(), console);
        Routines routines(gameId, &game, &engine.services());
        routines.init();

        for (const auto &[input, expected] : std::vector<std::pair<int, int>> {
                 {-129, 127}, {-128, -128}, {-1, -1}, {0, 0}, {1, 1},
                 {127, 127}, {128, -128}, {255, -1}, {256, 0}}) {
            routines.get(581).invoke(
                {script::Variable::ofString("boundary"),
                 script::Variable::ofInt(input)},
                execution);
            auto result = routines.get(580).invoke(
                {script::Variable::ofString("boundary")}, execution);
            EXPECT_EQ(result.intValue, expected) << "input=" << input;
            EXPECT_EQ(game.getGlobalNumber("boundary"), expected)
                << "input=" << input;
        }
    }
}

TEST(SaveWideSnapshot, signed_global_numbers_round_trip_for_both_titles) {
    auto &engine = testEngine();
    configureReputes(engine);
    StubConsole console;
    const std::vector<std::pair<std::string, int>> boundaries {
        {"number_max", 127}, {"number_min", -128},
        {"number_negative_one", -1}, {"number_one", 1},
        {"number_zero", 0}};
    const ByteBuffer expectedBytes {
        0x7f, static_cast<char>(0x80), static_cast<char>(0xff), 1, 0};

    for (auto gameId : {GameID::KotOR, GameID::TSL}) {
        bool tsl = gameId == GameID::TSL;
        Game game(gameId, "", engine.options(), engine.services(), console);
        configureRich(game, tsl);
        for (const auto &[name, value] : boundaries) {
            game.setGlobalNumber(name, value);
        }

        auto saved = SaveWideSnapshotBuilder(game, metadata(tsl)).build();
        ASSERT_TRUE(saved) << saved.message;
        auto globals = readGff(
            saved.snapshot->looseSlotResources.at({"globalvars", ResType::Res}));
        EXPECT_EQ(globals->getData("ValNumber"), expectedBytes);

        auto parsed = parseGVT(*globals);
        EXPECT_EQ(parsed.numbers, boundaries);

        Game loaded(gameId, "", engine.options(), engine.services(), console);
        loaded.setGlobalNumber("stale", 42);
        TestGameModule::deserializeGlobalVariables(loaded, *globals);
        EXPECT_FALSE(loaded.globalNumbers().count("stale"));
        for (const auto &[name, value] : boundaries) {
            EXPECT_EQ(loaded.getGlobalNumber(name), value);
        }
    }
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
    game.setGlobalLocation("invalid", nullptr);
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
