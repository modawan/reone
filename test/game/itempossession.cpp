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
#include "reone/game/action/giveitem.h"
#include "reone/game/action/takeitem.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/savedruntime.h"
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

// The prologue hands control to T3-M4 in a companion slot while the canonical
// player character waits outside the party.
constexpr int kNpcCompanion = 8;
constexpr char kSpikeTag[] = "k_computer_spike";
// The row makeBaseItemsTable gives weapon stats to.
constexpr int kMeleeWeaponBaseItem = 3;

std::shared_ptr<TwoDA> makeBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar", "itemtype"});
    builder.row({"", "", "", "", "", "", "I_Credits", "", "", "", "", "", "23"});
    builder.row({"", "", "", "", "", "", "I_Datapad", "", "", "", "", "", ""});
    builder.row({"", "", "", "", "", "2", "I_Disguise", "", "", "", "-1", "", ""});
    // A melee weapon, so a test can equip one.
    builder.row({"1.5", "2", "2", "2", "6", "48", "w_vbroswrd", "1", "1", "2", "-1", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Item> makeItem(Game &game, std::string tag, int baseItem, int stackSize) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", std::move(tag)))
                   .field(Gff::Field::newInt("BaseItem", baseItem))
                   .field(Gff::Field::newWord("StackSize", stackSize))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    item->setDropable(true);
    return item;
}

/**
 * A game plus the routine table it is bound to, so a lookup is made the way a
 * compiled script makes it rather than by calling the implementation directly.
 */
class PossessionHarness : boost::noncopyable {
public:
    PossessionHarness() :
        _game(GameID::TSL, "", testEngine().options(), testEngine().services(), _console),
        _routines(GameID::TSL, &_game, &testEngine().services()) {
        _routines.init();
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        return routine.invoke(args, execution);
    }

    uint32_t possessedBy(const Variable &owner, const std::string &tag) {
        return call("GetItemPossessedBy", {owner, Variable::ofString(tag)}).objectId;
    }

    Game &game() { return _game; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
};

/** The prologue party: a canonical PC outside the party, a controlled companion leading. */
struct ControlledCompanionParty {
    std::shared_ptr<Creature> actualPlayer;
    std::shared_ptr<Creature> companion;
};

ControlledCompanionParty makeControlledCompanionParty(Game &game) {
    ControlledCompanionParty party;
    party.actualPlayer = game.newCreature();
    party.companion = game.newCreature();
    game.party().addMember(kNpcPlayer, party.actualPlayer);
    game.party().setPlayer(party.actualPlayer);
    game.party().setActualPlayer(party.actualPlayer);
    game.party().addAvailableMember(kNpcCompanion, party.companion);
    game.party().setControlledMember(kNpcCompanion, party.companion);
    return party;
}

void expectItemTables(TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
}

// The authored prologue check: looting the spike footlocker and asking for the
// spike on GetFirstPC. The item travels the real container transfer path.
TEST(ItemPossession, controlled_companion_finds_an_item_looted_into_the_shared_inventory) {
    TestEngine &engine = testEngine();
    expectItemTables(engine);
    PossessionHarness harness;
    auto party = makeControlledCompanionParty(harness.game());

    auto footlocker = harness.game().newPlaceable();
    footlocker->addItem(makeItem(harness.game(), kSpikeTag, 1, 2));
    // "Take All" moves every item to the leader.
    footlocker->moveAllItemsTo(*harness.game().party().player());

    // The loot really did land in the shared inventory, not on the companion.
    ASSERT_TRUE(footlocker->items().empty());
    ASSERT_TRUE(party.companion->items().empty());
    ASSERT_EQ(1u, party.actualPlayer->items().size());

    auto pc = harness.call("GetFirstPC", {});
    ASSERT_EQ(party.companion->id(), pc.objectId);

    EXPECT_NE(kObjectInvalid, harness.possessedBy(pc, "K_COMPUTER_SPIKE"));
}

// The authored spend: the returned object is the stack the party carries, and
// writing to it reduces what the party has.
TEST(ItemPossession, the_returned_item_reports_and_spends_the_shared_stack) {
    TestEngine &engine = testEngine();
    expectItemTables(engine);
    PossessionHarness harness;
    auto party = makeControlledCompanionParty(harness.game());

    auto footlocker = harness.game().newPlaceable();
    footlocker->addItem(makeItem(harness.game(), kSpikeTag, 1, 3));
    footlocker->moveAllItemsTo(*harness.game().party().player());

    auto pc = harness.call("GetFirstPC", {});
    auto spike = Variable::ofObject(harness.possessedBy(pc, "K_COMPUTER_SPIKE"));
    ASSERT_NE(kObjectInvalid, spike.objectId);

    EXPECT_EQ(3, harness.call("GetNumStackedItems", {spike}).intValue);
    EXPECT_EQ(3, harness.call("GetItemStackSize", {spike}).intValue);

    harness.call("SetItemStackSize", {spike, Variable::ofInt(1)});

    EXPECT_EQ(1, party.actualPlayer->items().front()->stackSize());
    EXPECT_EQ(1, harness.call("GetItemStackSize", {spike}).intValue);
}

// The object's own inventory still wins, so a companion carrying its own copy
// of a tag is answered with that copy and not the party's.
TEST(ItemPossession, an_objects_own_item_takes_precedence_over_the_shared_inventory) {
    TestEngine &engine = testEngine();
    expectItemTables(engine);
    PossessionHarness harness;
    auto party = makeControlledCompanionParty(harness.game());

    auto shared = makeItem(harness.game(), kSpikeTag, 1, 5);
    party.actualPlayer->addItem(shared);
    auto own = makeItem(harness.game(), kSpikeTag, 1, 1);
    party.companion->addItem(own);

    auto pc = harness.call("GetFirstPC", {});
    EXPECT_EQ(own->id(), harness.possessedBy(pc, "K_COMPUTER_SPIKE"));
    EXPECT_NE(shared->id(), harness.possessedBy(pc, "K_COMPUTER_SPIKE"));
}

// Equipment is still consulted before the shared inventory.
TEST(ItemPossession, equipped_items_take_precedence_over_the_shared_inventory) {
    TestEngine &engine = testEngine();
    expectItemTables(engine);
    PossessionHarness harness;
    auto party = makeControlledCompanionParty(harness.game());

    auto shared = makeItem(harness.game(), kSpikeTag, 1, 5);
    party.actualPlayer->addItem(shared);
    auto equipped = makeItem(harness.game(), kSpikeTag, kMeleeWeaponBaseItem, 1);
    ASSERT_TRUE(party.companion->equip(InventorySlots::rightWeapon, equipped));

    auto pc = harness.call("GetFirstPC", {});
    EXPECT_EQ(equipped->id(), harness.possessedBy(pc, "K_COMPUTER_SPIKE"));
}

// A creature outside the party keeps looking no further than itself: it finds
// what it carries and never reaches what the party carries.
TEST(ItemPossession, a_non_party_object_has_no_access_to_the_shared_inventory) {
    TestEngine &engine = testEngine();
    expectItemTables(engine);
    PossessionHarness harness;
    auto party = makeControlledCompanionParty(harness.game());

    party.actualPlayer->addItem(makeItem(harness.game(), kSpikeTag, 1, 5));

    auto thug = harness.game().newCreature();
    auto thugVar = Variable::ofObject(thug->id());
    EXPECT_EQ(kObjectInvalid, harness.possessedBy(thugVar, "K_COMPUTER_SPIKE"));

    auto knife = makeItem(harness.game(), "k_thug_knife", 1, 1);
    thug->addItem(knife);
    EXPECT_EQ(knife->id(), harness.possessedBy(thugVar, "K_THUG_KNIFE"));
}

// A container is not a party member either, so looting it empties it rather
// than letting the party read through it.
TEST(ItemPossession, a_placeable_keeps_its_own_inventory_lookup) {
    TestEngine &engine = testEngine();
    expectItemTables(engine);
    PossessionHarness harness;
    makeControlledCompanionParty(harness.game());

    auto footlocker = harness.game().newPlaceable();
    auto spike = makeItem(harness.game(), kSpikeTag, 1, 2);
    footlocker->addItem(spike);

    auto footlockerVar = Variable::ofObject(footlocker->id());
    EXPECT_EQ(spike->id(), harness.possessedBy(footlockerVar, "K_COMPUTER_SPIKE"));

    footlocker->moveAllItemsTo(*harness.game().party().player());
    EXPECT_EQ(kObjectInvalid, harness.possessedBy(footlockerVar, "K_COMPUTER_SPIKE"));
}

// The base items the announcement, stacking and container tests use: a
// stackable item of up to three, and two items that keep a stack of one.
constexpr int kStackOfThreeBaseItem = 1;
constexpr int kSingleBaseItem = 2;
constexpr int kOtherSingleBaseItem = 3;
constexpr int kStackLimit = 3;

std::shared_ptr<TwoDA> makeStackingBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar", "itemtype", "stacking"});
    builder.row({"", "", "", "", "", "", "I_Credits", "", "", "", "", "", "23", "100"});
    builder.row({"", "", "", "", "", "", "I_Medpac", "", "", "", "", "", "", "3"});
    builder.row({"", "", "", "", "", "", "I_Datapad", "", "", "", "", "", "", "1"});
    builder.row({"", "", "", "", "", "", "I_Blaster", "", "", "", "", "", "", "1"});
    return std::shared_ptr<TwoDA>(builder.build());
}

void expectStackingItemTables(TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeStackingBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
}

/**
 * A KotOR game in an active module, so the item events queue there, with the
 * player character under control holding the party's shared inventory.
 */
struct ModuleParty {
    TestEngine &engine;
    StubConsole console;
    Game game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;

    explicit ModuleParty(bool withPlayer = true) :
        engine(testEngine()),
        game(GameID::KotOR, "", engine.options(), engine.services(), console) {
        game.initLocalServices();
        area = game.newArea();
        TestGameModule::setActiveModuleArea(game, area);
        if (withPlayer) {
            player = game.newCreature();
            game.party().addMember(kNpcPlayer, player);
            game.party().setPlayer(player);
        }
    }

    /** The script events of a kind waiting in the module, in the order queued. */
    std::vector<std::pair<SavedEventRecord, SavedScriptEvent>> events(SavedSignalEventType type) const {
        std::vector<std::pair<SavedEventRecord, SavedScriptEvent>> result;
        for (const auto &record : game.module()->saveEventSnapshot()) {
            if (record.eventId != static_cast<uint32_t>(SavedEventType::SignalEvent)) {
                continue;
            }
            const auto *event = std::get_if<SavedScriptEvent>(&record.payload);
            if (event && event->type == static_cast<uint16_t>(type)) {
                result.emplace_back(record, *event);
            }
        }
        return result;
    }
};

// Acquisition announcements

// An item given to the player character is announced acquired by it once,
// naming it, and nobody lost it: a container held it.
TEST(ItemAnnouncements, an_item_given_to_the_player_is_announced_acquired_once) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto chest = party.game.newPlaceable();
    auto item = makeItem(party.game, "k_medpac", kSingleBaseItem, 1);
    chest->addItem(item);

    // The container gives, so the give needs no walk to the player.
    auto give = party.game.newAction<GiveItemAction>(item, party.player, GiveItemAction::kWholeStack, false);
    give->execute(give, *chest, 0.0f);

    ASSERT_EQ(party.player->id(), item->owner());
    const auto acquired = party.events(SavedSignalEventType::ItemAcquired);
    ASSERT_EQ(1u, acquired.size());
    ASSERT_EQ(2u, acquired[0].second.objects.size());
    EXPECT_EQ(item->id(), acquired[0].second.objects[0].id);
    EXPECT_EQ(party.player->id(), acquired[0].second.objects[1].id);
    EXPECT_TRUE(party.events(SavedSignalEventType::ItemLost).empty());
}

// An item the party gives a container is lost by the creature that possessed
// it, acquired by no one, and disturbs the container as added.
TEST(ItemAnnouncements, an_item_given_to_a_container_is_lost_by_the_player_and_disturbs_the_container) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto item = makeItem(party.game, "k_datapad", kSingleBaseItem, 1);
    party.player->addItem(item);
    auto chest = party.game.newPlaceable();

    auto give = party.game.newAction<GiveItemAction>(item, chest, GiveItemAction::kWholeStack, false);
    give->execute(give, *party.game.newPlaceable(), 0.0f);

    ASSERT_EQ(chest->id(), item->owner());
    const auto lost = party.events(SavedSignalEventType::ItemLost);
    ASSERT_EQ(1u, lost.size());
    EXPECT_EQ(party.player->id(), lost[0].first.caller.id);
    ASSERT_EQ(1u, lost[0].second.objects.size());
    EXPECT_EQ(item->id(), lost[0].second.objects[0].id);
    EXPECT_TRUE(party.events(SavedSignalEventType::ItemAcquired).empty());
    const auto disturbed = party.events(SavedSignalEventType::InventoryDisturbed);
    ASSERT_EQ(1u, disturbed.size());
    EXPECT_EQ(chest->id(), disturbed[0].first.object.id);
    EXPECT_EQ(party.player->id(), disturbed[0].first.caller.id);
    ASSERT_EQ(1u, disturbed[0].second.integers.size());
    EXPECT_EQ(static_cast<int32_t>(InventoryDisturbType::Added), disturbed[0].second.integers[0]);
}

// Each module load announces every item of the shared inventory kept, once,
// by the creature under control.
TEST(ItemAnnouncements, a_module_load_announces_each_party_item_kept_once) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto datapad = makeItem(party.game, "k_datapad", kSingleBaseItem, 1);
    auto blaster = makeItem(party.game, "k_blaster", kOtherSingleBaseItem, 1);
    party.player->addItem(datapad);
    party.player->addItem(blaster);

    party.game.party().takeItemsOnModuleLoad();

    const auto kept = party.events(SavedSignalEventType::ItemAcquired);
    ASSERT_EQ(2u, kept.size());
    std::set<uint32_t> items;
    for (const auto &[record, event] : kept) {
        ASSERT_EQ(2u, event.objects.size());
        items.insert(event.objects[0].id);
        EXPECT_EQ(party.player->id(), event.objects[1].id);
    }
    EXPECT_EQ((std::set<uint32_t> {datapad->id(), blaster->id()}), items);
}

// With no one under control, a module load has no one to take the party's
// items, and announces nothing.
TEST(ItemAnnouncements, a_module_load_with_no_one_under_control_announces_nothing) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party(/*withPlayer=*/false);

    party.game.party().takeItemsOnModuleLoad();

    EXPECT_TRUE(party.events(SavedSignalEventType::ItemAcquired).empty());
}

// A companion made available while no one holds the shared inventory keeps
// what it carries; taking control, it is announced keeping it, once.
TEST(ItemAnnouncements, a_companion_with_no_party_to_receive_its_items_keeps_them_and_takes_control) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party(/*withPlayer=*/false);
    auto companion = party.game.newCreature();
    auto datapad = makeItem(party.game, "k_datapad", kSingleBaseItem, 1);
    companion->addItem(datapad);

    ASSERT_TRUE(party.game.party().addAvailableMember(kNpcCompanion, companion));
    ASSERT_EQ(1u, companion->items().size());
    EXPECT_EQ(datapad, companion->items().front());

    party.game.party().switchControlTo(kNpcCompanion, companion, nullptr, false);

    EXPECT_EQ(companion, party.game.party().player());
    const auto kept = party.events(SavedSignalEventType::ItemAcquired);
    ASSERT_EQ(1u, kept.size());
    ASSERT_EQ(2u, kept[0].second.objects.size());
    EXPECT_EQ(datapad->id(), kept[0].second.objects[0].id);
    EXPECT_EQ(companion->id(), kept[0].second.objects[1].id);
}

// Stacking rules

// An identical item joins a stack as far as the stack holds it; the rest keeps
// a stack of its own.
TEST(ItemStacking, an_identical_item_joins_a_stack_up_to_its_limit) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto creature = party.game.newCreature();
    auto first = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, 2);
    auto second = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, 2);

    creature->addItem(first);
    creature->addItem(second);

    ASSERT_EQ(2u, creature->items().size());
    EXPECT_EQ(kStackLimit, first->stackSize());
    EXPECT_EQ(1, second->stackSize());
}

// A whole item that fits joins the stack and is gone.
TEST(ItemStacking, an_identical_item_that_fits_is_merged_away) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto creature = party.game.newCreature();
    auto first = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, 1);
    auto second = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, 2);
    const uint32_t secondId = second->id();

    EXPECT_EQ(first, creature->addItem(first));
    EXPECT_EQ(first, creature->addItem(second));

    ASSERT_EQ(1u, creature->items().size());
    EXPECT_EQ(kStackLimit, first->stackSize());
    EXPECT_EQ(nullptr, party.game.getObjectById(secondId));
}

// Items that differ, here by tag, never share a stack, and an item kept apart
// joins none.
TEST(ItemStacking, differing_items_and_items_kept_apart_keep_their_own_stacks) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto creature = party.game.newCreature();
    auto medpac = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, 1);
    auto other = makeItem(party.game, "k_medpac_other", kStackOfThreeBaseItem, 1);
    auto apart = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, 1);

    creature->addItem(medpac);
    creature->addItem(other);
    creature->addItem(apart, ItemStacking::Own);

    EXPECT_EQ(3u, creature->items().size());
    EXPECT_EQ(1, medpac->stackSize());
    EXPECT_EQ(1, other->stackSize());
    EXPECT_EQ(1, apart->stackSize());
}

// The shared inventory keeps itself in order by base item; another creature
// takes each new item first.
TEST(ItemStacking, the_shared_inventory_keeps_its_order_and_a_creature_takes_new_items_first) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto blaster = makeItem(party.game, "k_blaster", kOtherSingleBaseItem, 1);
    auto datapad = makeItem(party.game, "k_datapad", kSingleBaseItem, 1);
    party.player->addItem(blaster);
    party.player->addItem(datapad);

    ASSERT_EQ(2u, party.player->items().size());
    EXPECT_EQ(datapad, party.player->items()[0]);
    EXPECT_EQ(blaster, party.player->items()[1]);

    auto stranger = party.game.newCreature();
    auto firstGiven = makeItem(party.game, "k_datapad", kSingleBaseItem, 1);
    auto secondGiven = makeItem(party.game, "k_blaster", kOtherSingleBaseItem, 1);
    stranger->addItem(firstGiven);
    stranger->addItem(secondGiven);

    ASSERT_EQ(2u, stranger->items().size());
    EXPECT_EQ(secondGiven, stranger->items()[0]);
    EXPECT_EQ(firstGiven, stranger->items()[1]);
}

// Container give and take

// Part of a stack given to a container is split off it: the container holds
// the part, the party keeps the rest, and the part is lost by the player.
TEST(ContainerGiveTake, part_of_a_stack_given_to_a_container_is_split_off) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto medpacs = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, kStackLimit);
    party.player->addItem(medpacs);
    auto chest = party.game.newPlaceable();

    auto give = party.game.newAction<GiveItemAction>(medpacs, chest, 1, false);
    give->execute(give, *party.game.newPlaceable(), 0.0f);

    EXPECT_EQ(party.player->id(), medpacs->owner());
    EXPECT_EQ(kStackLimit - 1, medpacs->stackSize());
    ASSERT_EQ(1u, chest->items().size());
    const auto part = chest->items().front();
    EXPECT_NE(medpacs, part);
    EXPECT_EQ(1, part->stackSize());
    const auto lost = party.events(SavedSignalEventType::ItemLost);
    ASSERT_EQ(1u, lost.size());
    EXPECT_EQ(party.player->id(), lost[0].first.caller.id);
    ASSERT_EQ(1u, lost[0].second.objects.size());
    EXPECT_EQ(part->id(), lost[0].second.objects[0].id);
}

// An item given to a container that holds an identical stack joins it.
TEST(ContainerGiveTake, an_item_given_to_a_container_joins_its_stack) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto chest = party.game.newPlaceable();
    auto stored = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, 1);
    chest->addItem(stored);
    auto given = makeItem(party.game, "k_medpac", kStackOfThreeBaseItem, 1);
    party.player->addItem(given);

    auto give = party.game.newAction<GiveItemAction>(given, chest, GiveItemAction::kWholeStack, false);
    give->execute(give, *party.game.newPlaceable(), 0.0f);

    ASSERT_EQ(1u, chest->items().size());
    EXPECT_EQ(stored, chest->items().front());
    EXPECT_EQ(2, stored->stackSize());
    EXPECT_TRUE(party.player->items().empty());
}

// The player takes an item from a container: it leaves the container for the
// shared inventory and is announced acquired by the player once.
TEST(ContainerGiveTake, an_item_taken_from_a_container_is_acquired_once) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto chest = party.game.newPlaceable();
    auto datapad = makeItem(party.game, "k_datapad", kSingleBaseItem, 1);
    chest->addItem(datapad);

    auto take = party.game.newAction<TakeItemAction>(datapad, chest);
    take->execute(take, *party.player, 0.0f);

    EXPECT_TRUE(chest->items().empty());
    EXPECT_EQ(party.player->id(), datapad->owner());
    EXPECT_EQ(party.player->id(), datapad->possessorId());
    const auto acquired = party.events(SavedSignalEventType::ItemAcquired);
    ASSERT_EQ(1u, acquired.size());
    ASSERT_EQ(2u, acquired[0].second.objects.size());
    EXPECT_EQ(datapad->id(), acquired[0].second.objects[0].id);
    EXPECT_EQ(party.player->id(), acquired[0].second.objects[1].id);
}

// Taking everything moves every item of a container to the shared
// inventory, the last first, and empties it.
TEST(ContainerGiveTake, taking_everything_empties_the_container_into_the_shared_inventory) {
    TestEngine &engine = testEngine();
    expectStackingItemTables(engine);
    ModuleParty party;
    auto chest = party.game.newPlaceable();
    auto datapad = makeItem(party.game, "k_datapad", kSingleBaseItem, 1);
    auto blaster = makeItem(party.game, "k_blaster", kOtherSingleBaseItem, 1);
    chest->addItem(datapad);
    chest->addItem(blaster);

    const auto moved = chest->moveAllItemsTo(*party.player);

    EXPECT_TRUE(chest->items().empty());
    ASSERT_EQ(2u, moved.size());
    ASSERT_EQ(2u, party.player->items().size());
    EXPECT_EQ(datapad, party.player->items()[0]);
    EXPECT_EQ(blaster, party.player->items()[1]);
    for (const auto &handed : moved) {
        EXPECT_EQ(handed.item, handed.held);
        EXPECT_EQ(party.player->id(), handed.item->possessorId());
    }
}

} // namespace
