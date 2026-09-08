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

#include "reone/game/equipmentoperation.h"
#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
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
        } else if (i == 6) {
            builder.row({"1.5", "2", "2", "2", "8", "48", "w_dblsbr", "1", "1", "3", "-1", ""});
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
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    item->setDropable(true);
    return item;
}

class EquipmentEngine : public TestEngine {
public:
    EquipmentEngine() { init(); }
};

class EquipmentOperation : public Test {
protected:
    EquipmentEngine engine;
    StubConsole console;
    Game game {GameID::KotOR, "", engine.options(), engine.services(), console};
    void SetUp() override {
        EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeLightsaberBaseItemsTable()));
    }
};
} // namespace

TEST_F(EquipmentOperation, equips_replaces_and_returns_items_to_explicit_inventory) {
    auto owner = game.newCreature();
    auto subject = game.newCreature();
    auto first = makeItem(game, "first", 8, 1);
    auto second = makeItem(game, "second", 3, 1);
    owner->addItem(first);
    owner->addItem(second);
    EXPECT_EQ(EquipmentOperationOutcome::Applied,
              applyEquipmentOperation(game, *subject, *owner, first, InventorySlots::rightWeapon));
    EXPECT_EQ(subject->id(), first->owner());
    EXPECT_EQ(EquipmentOperationOutcome::Applied,
              applyEquipmentOperation(game, *subject, *owner, second, InventorySlots::rightWeapon));
    EXPECT_EQ(owner->id(), first->owner());
    EXPECT_EQ(subject->id(), second->owner());
    EXPECT_EQ(second, subject->getEquippedItem(InventorySlots::rightWeapon));
    EXPECT_EQ(EquipmentOperationOutcome::Applied,
              applyEquipmentOperation(game, *subject, *owner, nullptr, InventorySlots::rightWeapon));
    EXPECT_EQ(2u, owner->items().size());
    EXPECT_TRUE(subject->equipment().empty());
    EXPECT_TRUE(subject->items().empty());
    EXPECT_EQ(EquipmentOperationOutcome::Unchanged,
              applyEquipmentOperation(game, *subject, *owner, nullptr, InventorySlots::rightWeapon));
}

TEST_F(EquipmentOperation, preserves_stacked_candidates_and_clears_both_hands) {
    auto owner = game.newCreature();
    auto subject = game.newCreature();
    auto stack = makeItem(game, "saber", 8, 3);
    owner->addItem(stack);
    EXPECT_EQ(EquipmentOperationOutcome::Applied,
              applyEquipmentOperation(game, *subject, *owner, stack, InventorySlots::leftWeapon));
    EXPECT_TRUE(subject->getEquippedItem(InventorySlots::rightWeapon));
    EXPECT_FALSE(subject->getEquippedItem(InventorySlots::leftWeapon));
    EXPECT_EQ(2, stack->stackSize());
    EXPECT_EQ(EquipmentOperationOutcome::Applied,
              applyEquipmentOperation(game, *subject, *owner, stack, InventorySlots::leftWeapon));
    auto off = subject->getEquippedItem(InventorySlots::leftWeapon);
    ASSERT_TRUE(off);
    EXPECT_NE(stack, off);
    EXPECT_EQ(1, stack->stackSize());
    EXPECT_EQ(EquipmentOperationOutcome::Applied,
              applyEquipmentOperation(game, *subject, *owner, nullptr, InventorySlots::rightWeapon));
    EXPECT_TRUE(subject->equipment().empty());
    EXPECT_EQ(1u, owner->items().size());
    EXPECT_EQ(3, stack->stackSize());
    EXPECT_EQ(owner->id(), stack->owner());
    EXPECT_FALSE(off->isRuntimeLive()); // merge consumes the exact split incarnation
}

TEST_F(EquipmentOperation, clears_paired_hand_for_restricted_weapons_and_supports_alternate_slots) {
    for (int baseItem : {1, 6}) {
        for (auto slots : {std::pair {InventorySlots::rightWeapon, InventorySlots::leftWeapon},
                           std::pair {InventorySlots::rightWeapon2, InventorySlots::leftWeapon2}}) {
            Game alternate(GameID::TSL, "", engine.options(), engine.services(), console);
            Game &operationGame = slots.first == InventorySlots::rightWeapon2 ? alternate : game;
            auto subject = operationGame.newCreature();
            auto owner = operationGame.newCreature();
            auto main = makeItem(operationGame, "main", 8, 1);
            auto off = makeItem(operationGame, "off", 3, 1);
            auto baton = makeItem(operationGame, "restricted", baseItem, 1);
            ASSERT_TRUE(subject->equip(slots.first, main));
            ASSERT_TRUE(subject->equip(slots.second, off));
            owner->addItem(baton);
            EXPECT_EQ(EquipmentOperationOutcome::Applied,
                      applyEquipmentOperation(operationGame, *subject, *owner, baton, slots.first));
            EXPECT_FALSE(subject->getEquippedItem(slots.second));
            EXPECT_EQ(baton, subject->getEquippedItem(slots.first));
            EXPECT_EQ(2u, owner->items().size());
            EXPECT_EQ(owner->id(), main->owner());
            EXPECT_EQ(owner->id(), off->owner());
            EXPECT_EQ(EquipmentOperationOutcome::Applied,
                      applyEquipmentOperation(operationGame, *subject, *owner, nullptr, slots.first));
            EXPECT_EQ(3u, owner->items().size());
            EXPECT_EQ(EquipmentOperationOutcome::Applied,
                      applyEquipmentOperation(operationGame, *subject, *owner, main, slots.first));
            EXPECT_EQ(EquipmentOperationOutcome::Applied,
                      applyEquipmentOperation(operationGame, *subject, *owner, off, slots.second));
            EXPECT_EQ(1u, owner->items().size());
            EXPECT_EQ(owner->id(), baton->owner());
        }
    }
}

TEST_F(EquipmentOperation, rejects_invalid_foreign_and_retired_requests_without_mutation) {
    auto subject = game.newCreature();
    auto owner = game.newCreature();
    auto foreign = game.newCreature();
    auto main = makeItem(game, "main", 8, 1);
    auto incompatible = makeItem(game, "blaster", 12, 2);
    ASSERT_TRUE(subject->equip(InventorySlots::rightWeapon, main));
    owner->addItem(incompatible);
    EXPECT_EQ(EquipmentOperationOutcome::Rejected,
              applyEquipmentOperation(game, *subject, *owner, incompatible, InventorySlots::leftWeapon));
    EXPECT_EQ(EquipmentOperationOutcome::Rejected,
              applyEquipmentOperation(game, *subject, *foreign, incompatible, InventorySlots::rightWeapon));
    EXPECT_EQ(EquipmentOperationOutcome::Rejected,
              applyEquipmentOperation(game, *subject, *owner, nullptr, -1));
    EXPECT_EQ(2, incompatible->stackSize());
    EXPECT_EQ(owner->id(), incompatible->owner());
    EXPECT_EQ(main, subject->getEquippedItem(InventorySlots::rightWeapon));
    game.destroyRuntimeObjectGraph(incompatible);
    EXPECT_EQ(EquipmentOperationOutcome::Rejected,
              applyEquipmentOperation(game, *subject, *owner, incompatible, InventorySlots::rightWeapon));
    game.destroyRuntimeObjectGraph(subject);
    EXPECT_EQ(EquipmentOperationOutcome::Rejected,
              applyEquipmentOperation(game, *subject, *owner, nullptr, InventorySlots::rightWeapon));
}
