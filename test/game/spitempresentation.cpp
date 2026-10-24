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

#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/game.h"
#include "reone/game/gui/ingame/itembacking.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"

#include "../fixtures/engine.h"
#include "../fixtures/itempresentation.h"

namespace {

class SPItemPresentation : public SharedPresentation {};

TEST_F(SPItemPresentation, sp_backing_applies_the_real_equipment_presenters_selection) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    // The equipment menus run in the active module.
    TestGameModule::setActiveModuleArea(game, game.newArea());
    auto baseItems = std::shared_ptr<TwoDA>(TwoDA::Builder()
                                                .columns({"equipableslots", "itemclass"})
                                                .row({"2", "armor"})
                                                .row({"2", "armor"})
                                                .build());
    // The game reads its rule tables when it starts; the others are missing.
    EXPECT_CALL(engine.resourceModule().twoDas(),
                get(AnyOf(StartsWith("iprp_"), "gameeffects", "forceshields", "forceadjust",
                          "excitedduration", "poison", "statescripts", "removefxondeath", "exptable",
                          "xptable", "npc", "appearancesndset", "weaponsounds", "placeableobjsnds",
                          "bodybag", "traps", "fractionalcr", "regeneration", "racialtypes", "ranges",
                          "effecticon", "itemvalue", "videoeffects", "stringtokens", "tutorial", "feedbacktext",
                          "aiscripts", "dialoganimations", "animations", "encdifficulty")))
        .Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(baseItems));
    // The equipment view reads the player's Strength through the racial
    // ability adjustments.
    EXPECT_CALL(engine.resourceModule().twoDas(), get("racialtypes"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::shared_ptr<TwoDA>(
            TwoDA::Builder()
                .columns({"stradjust", "dexadjust", "conadjust", "intadjust", "wisadjust", "chaadjust"})
                .row({"0", "0", "0", "0", "0", "0"})
                .build())));
    // Equipping checks the item's minimum level against itemvalue.
    EXPECT_CALL(engine.resourceModule().twoDas(), get("itemvalue"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::shared_ptr<resource::TwoDA>(resource::TwoDA::Builder()
                                                                    .columns({"maxsingleitemvalue"})
                                                                    .row({"13500000"})
                                                                    .build())));
    engine.initCombatTables();
    auto player = game.newCreature();
    game.party().setPlayer(player);
    ASSERT_TRUE(game.party().addMember(kNpcPlayer, player));
    // The player character is a level 1 soldier with a hit die of 10: it
    // reaches every item's minimum level, and the equipment view reads the
    // class's attack and defense bonuses.
    NiceMock<MockStrings> classStrings;
    NiceMock<MockTwoDAs> classTwoDas;
    ON_CALL(classTwoDas, get("skills"))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(TwoDA::Builder().build())));
    ON_CALL(classTwoDas, get("save"))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(
            TwoDA::Builder()
                .columns({"level", "fortsave", "refsave", "willsave"})
                .row({"1", "0", "0", "0"})
                .build())));
    ON_CALL(classTwoDas, get("attack"))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(
            TwoDA::Builder().columns({"bab"}).row({"0"}).build())));
    ON_CALL(classTwoDas, get("acbonus"))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(
            TwoDA::Builder().columns({"soldier"}).row({"0"}).build())));
    Classes classes(classStrings, classTwoDas);
    CreatureClass soldier(ClassType::Soldier, classes, classStrings, classTwoDas);
    soldier.load(*TwoDA::Builder()
                      .columns({"name", "description", "hitdie", "skillpointbase",
                                "str", "dex", "con", "int", "wis", "cha",
                                "skillstable", "savingthrowtable", "attackbonustable",
                                "armorclasscolumn", "featstable", "featgain", "spellgaintable"})
                      .row({"0", "0", "10", "0",
                            "10", "10", "10", "10", "10", "10",
                            "unused", "save", "attack",
                            "soldier", "", "", ""})
                      .build(),
                 0);
    player->applyLevelUp(player->attributes(), soldier);
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
