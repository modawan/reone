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
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
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
    // "Take All" moves dropable items to the leader.
    footlocker->moveDropableItemsTo(*harness.game().party().player());

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
    footlocker->moveDropableItemsTo(*harness.game().party().player());

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

    footlocker->moveDropableItemsTo(*harness.game().party().player());
    EXPECT_EQ(kObjectInvalid, harness.possessedBy(footlockerVar, "K_COMPUTER_SPIKE"));
}

} // namespace
