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

#include "../fixtures/engine.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/gui/ingame/itembacking.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {
std::shared_ptr<TwoDA> makeLightsaberBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= 12; ++i) {
        if (i == 1) {
            builder.row({"1.5", "2", "2", "2", "4", "48", "w_stunbaton", "1", "1", "1", "-1", ""});
        } else if (i == 3) {
            builder.row({"1.5", "2", "2", "2", "6", "48", "w_vbroswrd", "1", "1", "2", "-1", ""});
        } else if (i == 8) {
            builder.row({"1.5", "2", "2", "2", "8", "48", "w_lghtsbr", "1", "1", "2", "-1", ""});
        } else if (i == 12) {
            builder.row({"23", "2", "2", "2", "6", "48", "w_blstrpstl", "1", "4", "4", "-1", ""});
        } else {
            builder.row({"", "", "", "", "", "", "", "", "", "", "", ""});
        }
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Item> makeItem(Game &game, std::string tag, int baseItem, int stackSize) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", std::move(tag)))
                   .field(Gff::Field::newInt("BaseItem", baseItem))
                   .field(Gff::Field::newWord("StackSize", stackSize))
                   .field(Gff::Field::newCExoLocString("LocalizedName", -1, "Saved item name"))
                   .field(Gff::Field::newCExoLocString("DescIdentified", -1, "Saved item description"))
                   .field(Gff::Field::newByte("Identified", 1))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    item->setDropable(true);
    return item;
}

class ItemMenuBackingTest : public Test {
protected:
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game {GameID::KotOR, "", engine.options(), engine.services(), console};
    std::shared_ptr<Creature> owner, subject;
    void SetUp() override {
        EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeLightsaberBaseItemsTable()));
        owner = game.newCreature();
        subject = game.newCreature();
        game.party().setPlayer(owner);
        ASSERT_TRUE(game.party().setRosterAvailable({RosterKind::Npc, 0}, true));
        ASSERT_TRUE(game.party().addMember(0, subject));
        ASSERT_TRUE(game.party().addMember(kNpcPlayer, owner));
    }
    MenuItemView selected(const EquipmentView &view, int count) {
        for (const auto &item : view.items)
            if (item.stackSize == count)
                return item;
        ADD_FAILURE() << "No item with count " << count;
        return {};
    }
};
} // namespace

TEST_F(ItemMenuBackingTest, duplicate_tags_remain_distinct_and_shared_inventory_equips_the_selected_subject) {
    auto first = makeItem(game, "duplicate", 8, 2);
    auto second = makeItem(game, "duplicate", 3, 3);
    owner->addItem(first);
    owner->addItem(second);
    auto backing = newEquipmentMenuBacking(game, engine.services());
    auto view = backing->readEquipment(InventorySlots::rightWeapon);
    ASSERT_EQ(2u, view.items.size());
    EXPECT_NE(view.items[0].handle, view.items[1].handle);
    EXPECT_EQ("Saved item name", view.items[0].name);
    EXPECT_EQ(view.items[0].name, view.items[1].name);
    auto chosen = selected(view, 3);
    backing->equip(view.revision, chosen.handle, InventorySlots::rightWeapon);
    ASSERT_TRUE(backing->equipmentResult());
    EXPECT_EQ(EquipmentRequestOutcome::Applied, backing->equipmentResult()->outcome);
    EXPECT_EQ(2, first->stackSize());
    EXPECT_EQ(2, second->stackSize());
    auto equipped = subject->getEquippedItem(InventorySlots::rightWeapon);
    ASSERT_TRUE(equipped);
    EXPECT_EQ(3, equipped->baseItemType());
    EXPECT_EQ(subject->id(), equipped->owner());
    EXPECT_TRUE(owner->equipment().empty());
    EXPECT_EQ(3, chosen.stackSize); // read values are not authority-owned objects
    backing->equip(view.revision, chosen.handle, InventorySlots::rightWeapon);
    EXPECT_EQ(EquipmentRequestOutcome::Rejected, backing->equipmentResult()->outcome);
    EXPECT_EQ(2, second->stackSize());
}

TEST_F(ItemMenuBackingTest, transferred_and_retired_items_and_obsolete_subjects_reject_old_handles) {
    auto item = makeItem(game, "candidate", 8, 2);
    owner->addItem(item);
    auto backing = newEquipmentMenuBacking(game, engine.services());
    auto view = backing->readEquipment(InventorySlots::rightWeapon);
    ASSERT_FALSE(view.items.empty());
    auto foreign = game.newCreature();
    ASSERT_TRUE(transferItemTo(game, item, *foreign));
    backing->equip(view.revision, view.items.front().handle, InventorySlots::rightWeapon);
    EXPECT_EQ(EquipmentRequestOutcome::Rejected, backing->equipmentResult()->outcome);
    EXPECT_EQ(foreign->id(), item->owner());
    EXPECT_TRUE(subject->equipment().empty());
    ASSERT_TRUE(transferItemTo(game, item, *owner));
    view = backing->readEquipment(InventorySlots::rightWeapon);
    ASSERT_TRUE(owner->removeItemStack(item));
    game.destroyRuntimeObjectGraph(item);
    backing->equip(view.revision, view.items.front().handle, InventorySlots::rightWeapon);
    EXPECT_EQ(EquipmentRequestOutcome::Rejected, backing->equipmentResult()->outcome);
    EXPECT_FALSE(item->isRuntimeLive());
    auto next = makeItem(game, "candidate", 8, 1);
    owner->addItem(next);
    view = backing->readEquipment(InventorySlots::rightWeapon);
    ASSERT_FALSE(view.items.empty());
    game.party().clear();
    ASSERT_TRUE(game.party().addMember(kNpcPlayer, owner));
    backing->equip(view.revision, view.items.front().handle, InventorySlots::rightWeapon);
    EXPECT_EQ(EquipmentRequestOutcome::Rejected, backing->equipmentResult()->outcome);
    EXPECT_EQ(owner->id(), next->owner());
}

TEST_F(ItemMenuBackingTest, refreshing_one_backing_does_not_refresh_another_backings_handles) {
    auto first = makeItem(game, "same", 8, 2);
    owner->addItem(first);
    auto a = newEquipmentMenuBacking(game, engine.services());
    auto b = newEquipmentMenuBacking(game, engine.services());
    auto old = a->readEquipment(InventorySlots::rightWeapon);
    auto independent = b->readEquipment(InventorySlots::rightWeapon);
    a->readEquipment(InventorySlots::rightWeapon);
    a->equip(old.revision, old.items.front().handle, InventorySlots::rightWeapon);
    EXPECT_EQ(EquipmentRequestOutcome::Rejected, a->equipmentResult()->outcome);
    b->equip(independent.revision, independent.items.front().handle, InventorySlots::rightWeapon);
    EXPECT_EQ(EquipmentRequestOutcome::Applied, b->equipmentResult()->outcome);
}

TEST_F(ItemMenuBackingTest, changed_stack_and_replaced_equipment_invalidate_selections) {
    auto item = makeItem(game, "candidate", 8, 2);
    owner->addItem(item);
    auto backing = newEquipmentMenuBacking(game, engine.services());
    auto view = backing->readEquipment(InventorySlots::rightWeapon);
    ASSERT_EQ(1u, view.items.size());
    item->setStackSize(3);
    backing->equip(view.revision, view.items.front().handle, InventorySlots::rightWeapon);
    EXPECT_EQ(EquipmentRequestOutcome::Rejected, backing->equipmentResult()->outcome);
    EXPECT_EQ(3, item->stackSize());
    view = backing->readEquipment(InventorySlots::rightWeapon);
    auto other = makeItem(game, "new equipment", 3, 1);
    ASSERT_TRUE(subject->equip(InventorySlots::rightWeapon, other));
    backing->equip(view.revision, 0, InventorySlots::rightWeapon);
    EXPECT_EQ(EquipmentRequestOutcome::Rejected, backing->equipmentResult()->outcome);
    EXPECT_EQ(other, subject->getEquippedItem(InventorySlots::rightWeapon));
}
