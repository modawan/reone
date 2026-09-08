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
#include "../fixtures/itempresentation.h"
#include "reone/game/game.h"
#include "reone/game/gui/ingame/itembacking.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"

namespace {

class SPItemPresentation : public SharedPresentation {};

TEST_F(SPItemPresentation, sp_backing_applies_the_real_equipment_presenters_selection) {
    auto &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto baseItems = std::shared_ptr<TwoDA>(TwoDA::Builder()
                                                .columns({"equipableslots", "itemclass"})
                                                .row({"2", "armor"})
                                                .row({"2", "armor"})
                                                .build());
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(baseItems));
    auto player = game.newCreature();
    game.party().setPlayer(player);
    ASSERT_TRUE(game.party().addMember(kNpcPlayer, player));
    auto record = Gff::Builder().field(Gff::Field::newInt("BaseItem", 0)).field(Gff::Field::newWord("StackSize", 2)).field(Gff::Field::newCExoLocString("LocalizedName", -1, "Saved armor")).field(Gff::Field::newCExoString("Tag", "same_tag")).build();
    auto item = game.newItem();
    item->deserialize(*record, SerializedIdentityContext::templateResource());
    player->addItem(item);
    auto secondRecord = Gff::Builder().field(Gff::Field::newInt("BaseItem", 1)).field(Gff::Field::newWord("StackSize", 3)).field(Gff::Field::newCExoLocString("LocalizedName", -1, "Other saved armor")).field(Gff::Field::newCExoString("Tag", "same_tag")).build();
    auto second = game.newItem();
    second->deserialize(*secondRecord, SerializedIdentityContext::templateResource());
    player->addItem(second);
    ScreenResources guis(options, scene, graphics, resources);
    auto backing = newEquipmentMenuBacking(game, engine.services());
    Equipment screen(GameID::KotOR, options, services(guis), resources.services().strings, backing, []() {});
    screen.init();
    screen.openItems();
    auto list = std::static_pointer_cast<ListBox>(guis.screens.at("equip")->findControl("LB_ITEMS"));
    ASSERT_EQ(3, list->getItemCount());
    EXPECT_EQ("Saved armor", list->getItemAt(1).text);
    list->setSelectedItemIndex(2);
    guis.screens.at("equip")->findControl("BTN_EQUIP")->handleClick(0, 0);
    auto equipped = player->getEquippedItem(InventorySlots::body);
    ASSERT_TRUE(equipped);
    EXPECT_NE(item, equipped);
    EXPECT_EQ(1, equipped->baseItemType());
    EXPECT_EQ(2, item->stackSize());
    EXPECT_EQ(2, second->stackSize());
    EXPECT_EQ(1, equipped->stackSize());
    EXPECT_EQ(player->id(), equipped->owner());
    screen.openItems();
    list->setSelectedItemIndex(0);
    guis.screens.at("equip")->findControl("BTN_EQUIP")->handleClick(0, 0);
    EXPECT_FALSE(player->getEquippedItem(InventorySlots::body));
    ASSERT_EQ(2u, player->items().size());
    EXPECT_EQ(2, item->stackSize());
    EXPECT_EQ(3, second->stackSize());
    EXPECT_FALSE(equipped->isRuntimeLive());
}

} // namespace
