/*
 * Copyright (c) 2020-2026 The reone project contributors
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
#include "reone/game/gui/upgradescreens.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/game/upgradebench.h"
#include "reone/game/upgraderules.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/types.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace reone::game {

/** Reaches the upgrade screen's state, which a running GUI only draws. */
class UpgradeScreensTestAccess {
public:
    static const std::vector<UpgradeSlot> &slots(const UpgradeGUI &gui) { return gui._slots; }

    static std::vector<std::string> choices(const UpgradeGUI &gui) {
        std::vector<std::string> result;
        for (const auto &choice : gui._choices) {
            result.push_back(choice.templateResRef + (choice.installed ? " (installed)" : ""));
        }
        return result;
    }

    static void openSlot(UpgradeGUI &gui, int slot) { gui.openSlot(slot); }
    static void fitChoice(UpgradeGUI &gui, int index) { gui.fitChoice(index); }
    static void back(UpgradeGUI &gui) { gui.back(); }
    static int picking(const UpgradeGUI &gui) { return gui._picking; }

    static size_t listed(const UpgradeItemsGUI &gui) { return gui._listed.size(); }

    static UpgradeGUI &upgrade(Game &game) { return *game._upgrade; }
    static UpgradeItemsGUI &items(Game &game) { return *game._upgradeItems; }
};

} // namespace reone::game

namespace {

/** upgrade.2da as the game ships it. */
std::shared_ptr<TwoDA> makeUpgradeTable() {
    TwoDA::Builder builder;
    builder.columns({"label", "template", "upgradetype"});
    for (int i = 1; i <= 13; ++i) {
        builder.row({"crystal", str(boost::format("G_W_SBRCRSTL%02d") % i), "0"});
    }
    builder.row({"Vibration_Cell", "G_I_UPGRADE007", "1"});
    builder.row({"Durasteel_Alloy", "G_I_UPGRADE008", "2"});
    builder.row({"Energy_Projector", "G_I_UPGRADE009", "3"});
    builder.row({"Scope", "G_I_UPGRADE001", "4"});
    builder.row({"Imp_Energy_Cell", "G_I_UPGRADE002", "5"});
    builder.row({"Beam_Splitter", "G_I_UPGRADE003", "6"});
    builder.row({"Hair_Trigger", "G_I_UPGRADE004", "7"});
    builder.row({"Armor_Reinforcement", "G_I_UPGRADE005", "8"});
    builder.row({"Mesh_Underlay", "G_I_UPGRADE006", "9"});
    builder.row({"Krayt_Dragon_Pearl", "tat18_dragonprl", "0"});
    builder.row({"Wookie_Amulet", "kas25_wookcrysta", "0"});
    builder.row({"Solari_crystal", "g_w_sbrcrstl19", "0"});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::vector<Item::PropertyEntry> propertiesWithUpgrades(std::vector<int> rows) {
    std::vector<Item::PropertyEntry> properties;
    Item::PropertyEntry always;
    properties.push_back(always);
    for (int row : rows) {
        Item::PropertyEntry entry;
        entry.upgradeType = static_cast<uint8_t>(row);
        properties.push_back(entry);
    }
    return properties;
}

std::vector<int> range(int first, int last) {
    std::vector<int> rows;
    for (int i = first; i <= last; ++i) {
        rows.push_back(i);
    }
    return rows;
}

std::vector<int> lightsaberRows() {
    auto rows = range(0, 12);
    rows.insert(rows.end(), {22, 23, 24});
    return rows;
}

std::set<int> accepted(std::vector<int> rows) {
    return acceptedUpgradeRows(propertiesWithUpgrades(std::move(rows)));
}

constexpr uint32_t bit(int row) {
    return uint32_t {1} << row;
}

const UpgradeSlot kColor {UpgradeSlot::Type::ColorCrystal, -1};
UpgradeSlot upgradeSlot(int kind) {
    return UpgradeSlot {UpgradeSlot::Type::Upgrade, kind};
}

class UpgradeRulesTest : public ::testing::Test {
protected:
    UpgradeTable _table {UpgradeTable::fromTwoDA(*makeUpgradeTable())};
};

} // namespace

TEST_F(UpgradeRulesTest, the_table_reads_upgrade_2da_with_its_rows_as_bits) {
    ASSERT_EQ(25u, _table.rows().size());
    EXPECT_EQ("g_w_sbrcrstl01", _table.rows()[0].templateResRef);
    auto *scope = _table.findByTemplate("g_i_upgrade001");
    ASSERT_NE(nullptr, scope);
    EXPECT_EQ(16, scope->row);
    EXPECT_EQ(4, scope->slotKind);
    EXPECT_EQ(24, _table.findByTemplate("G_W_SBRCRSTL19")->row);
    EXPECT_EQ(nullptr, _table.findByTemplate("g_w_sbrcrstl14"));
    EXPECT_EQ(nullptr, _table.findByRow(25));
}

TEST_F(UpgradeRulesTest, a_row_without_a_template_or_kind_is_no_upgrade) {
    auto table = UpgradeTable::fromTwoDA(*TwoDA::Builder()
                                              .columns({"label", "template", "upgradetype"})
                                              .row({"blank", "", "0"})
                                              .row({"kindless", "g_i_upgrade001", ""})
                                              .row({"scope", "g_i_upgrade001", "4"})
                                              .build());
    ASSERT_EQ(1u, table.rows().size());
    EXPECT_EQ(2, table.rows()[0].row);
}

TEST_F(UpgradeRulesTest, an_item_takes_the_upgrades_its_properties_name) {
    EXPECT_EQ((std::set<int> {16, 17}), accepted({16, 17, 16}));
    EXPECT_TRUE(accepted({}).empty());
}

TEST_F(UpgradeRulesTest, slots_follow_the_kinds_an_item_takes) {
    // A blaster: scope, energy cell, beam splitter, hair trigger.
    EXPECT_EQ((std::vector<UpgradeSlot> {upgradeSlot(4), upgradeSlot(5), upgradeSlot(6), upgradeSlot(7)}),
              upgradeSlots(false, accepted(range(16, 19)), _table));
    // A vibroblade: vibration cell, alloy, projector.
    EXPECT_EQ((std::vector<UpgradeSlot> {upgradeSlot(1), upgradeSlot(2), upgradeSlot(3)}),
              upgradeSlots(false, accepted(range(13, 15)), _table));
    // Armour: reinforcement and underlay.
    EXPECT_EQ((std::vector<UpgradeSlot> {upgradeSlot(8), upgradeSlot(9)}),
              upgradeSlots(false, accepted({20, 21}), _table));
    // A blaster with a scope only shows that slot.
    EXPECT_EQ((std::vector<UpgradeSlot> {upgradeSlot(4)}), upgradeSlots(false, accepted({16}), _table));
    EXPECT_TRUE(upgradeSlots(false, accepted({}), _table).empty());
}

TEST_F(UpgradeRulesTest, a_lightsaber_has_a_colour_crystal_and_two_power_crystal_slots) {
    EXPECT_EQ((std::vector<UpgradeSlot> {kColor, upgradeSlot(0), upgradeSlot(0)}),
              upgradeSlots(true, accepted(lightsaberRows()), _table));
    // A lightsaber taking no power crystal still has its colour.
    EXPECT_EQ((std::vector<UpgradeSlot> {kColor}), upgradeSlots(true, accepted({}), _table));
}

TEST_F(UpgradeRulesTest, a_slot_fits_the_rows_of_its_kind_the_item_takes) {
    EXPECT_EQ((std::vector<int> {16}), rowsFittingSlot(upgradeSlot(4), accepted(range(16, 19)), _table));
    EXPECT_EQ((std::vector<int> {0, 1, 22}), rowsFittingSlot(upgradeSlot(0), accepted({0, 1, 22, 16}), _table));
    EXPECT_TRUE(rowsFittingSlot(kColor, accepted(lightsaberRows()), _table).empty());
}

TEST_F(UpgradeRulesTest, set_bits_fill_the_slots_of_their_kind_in_row_order) {
    auto rows = accepted(lightsaberRows());
    auto slots = upgradeSlots(true, rows, _table);
    EXPECT_EQ((std::vector<int> {-1, 3, 22}), installedRows(slots, rows, bit(22) | bit(3), _table));
    // A third crystal has no slot to show in.
    EXPECT_EQ((std::vector<int> {-1, 0, 1}), installedRows(slots, rows, bit(0) | bit(1) | bit(2), _table));
    // A bit for an upgrade the item does not take is not shown.
    auto blaster = accepted({16, 17});
    EXPECT_EQ((std::vector<int> {-1, 17}),
              installedRows(upgradeSlots(false, blaster, _table), blaster, bit(13) | bit(17), _table));
}

TEST_F(UpgradeRulesTest, fitting_an_upgrade_sets_its_bit_and_takes_it_from_the_inventory) {
    auto blaster = accepted(range(16, 19));
    auto slots = upgradeSlots(false, blaster, _table);
    auto change = planSlotChange(slots, 0, blaster, 0, 0, _table, "G_I_UPGRADE001");
    ASSERT_TRUE(change);
    EXPECT_EQ(bit(16), change->upgrades);
    EXPECT_EQ("g_i_upgrade001", change->consumed);
    EXPECT_EQ("", change->returned);
}

TEST_F(UpgradeRulesTest, choosing_the_upgrade_a_slot_holds_takes_it_out) {
    auto blaster = accepted(range(16, 19));
    auto slots = upgradeSlots(false, blaster, _table);
    auto change = planSlotChange(slots, 0, blaster, bit(16) | bit(19), 0, _table, "g_i_upgrade001");
    ASSERT_TRUE(change);
    EXPECT_EQ(bit(19), change->upgrades);
    EXPECT_EQ("", change->consumed);
    EXPECT_EQ("g_i_upgrade001", change->returned);
}

TEST_F(UpgradeRulesTest, fitting_into_a_full_slot_swaps_the_upgrade_it_holds) {
    auto rows = accepted(lightsaberRows());
    auto slots = upgradeSlots(true, rows, _table);
    auto change = planSlotChange(slots, 1, rows, bit(0) | bit(5), 1, _table, "tat18_dragonprl");
    ASSERT_TRUE(change);
    EXPECT_EQ(bit(5) | bit(22), change->upgrades);
    EXPECT_EQ("tat18_dragonprl", change->consumed);
    EXPECT_EQ("g_w_sbrcrstl01", change->returned);
    EXPECT_EQ(1, change->modelVariation);
}

TEST_F(UpgradeRulesTest, an_upgrade_that_does_not_fit_the_slot_changes_nothing) {
    auto blaster = accepted(range(16, 19));
    auto slots = upgradeSlots(false, blaster, _table);
    // A beam splitter in the scope slot.
    EXPECT_FALSE(planSlotChange(slots, 0, blaster, 0, 0, _table, "g_i_upgrade003"));
    // A vibration cell on a blaster.
    EXPECT_FALSE(planSlotChange(slots, 0, blaster, 0, 0, _table, "g_i_upgrade007"));
    // Not an upgrade at all, and a slot the item does not have.
    EXPECT_FALSE(planSlotChange(slots, 0, blaster, 0, 0, _table, "g_w_blstrpstl01"));
    EXPECT_FALSE(planSlotChange(slots, 4, blaster, 0, 0, _table, "g_i_upgrade001"));
    EXPECT_FALSE(planSlotChange(slots, -1, blaster, 0, 0, _table, "g_i_upgrade001"));
}

TEST_F(UpgradeRulesTest, a_crystal_the_other_slot_holds_cannot_be_fitted_twice) {
    auto rows = accepted(lightsaberRows());
    auto slots = upgradeSlots(true, rows, _table);
    EXPECT_FALSE(planSlotChange(slots, 2, rows, bit(4), 1, _table, "g_w_sbrcrstl05"));
}

TEST_F(UpgradeRulesTest, a_colour_crystal_sets_the_blade_and_gives_back_the_one_it_replaces) {
    auto rows = accepted(lightsaberRows());
    auto slots = upgradeSlots(true, rows, _table);
    auto change = planSlotChange(slots, 0, rows, bit(3), 1, _table, "g_w_sbrcrstl18");
    ASSERT_TRUE(change);
    EXPECT_EQ(2, change->modelVariation);
    EXPECT_EQ(bit(3), change->upgrades);
    EXPECT_EQ("g_w_sbrcrstl18", change->consumed);
    EXPECT_EQ("g_w_sbrcrstl14", change->returned);

    // The blade it has already, and a power crystal, change nothing here.
    EXPECT_FALSE(planSlotChange(slots, 0, rows, 0, 2, _table, "g_w_sbrcrstl18"));
    EXPECT_FALSE(planSlotChange(slots, 0, rows, 0, 2, _table, "g_w_sbrcrstl01"));

    // A blade no crystal gives returns nothing.
    change = planSlotChange(slots, 0, rows, 0, 0, _table, "g_w_sbrcrstl16");
    ASSERT_TRUE(change);
    EXPECT_EQ(3, change->modelVariation);
    EXPECT_EQ("", change->returned);
}

TEST_F(UpgradeRulesTest, colour_crystals_map_to_blades_both_ways) {
    const std::vector<std::pair<std::string, int>> crystals {
        {"g_w_sbrcrstl14", 1}, {"g_w_sbrcrstl15", 4}, {"g_w_sbrcrstl16", 3}, {"g_w_sbrcrstl17", 5}, {"g_w_sbrcrstl18", 2}};
    for (const auto &[resRef, variation] : crystals) {
        EXPECT_EQ(variation, colorCrystalBladeVariation(resRef)) << resRef;
        EXPECT_EQ(resRef, colorCrystalForBladeVariation(variation)) << variation;
    }
    EXPECT_EQ(1, colorCrystalBladeVariation("G_W_SBRCRSTL14"));
    EXPECT_FALSE(colorCrystalBladeVariation("g_w_sbrcrstl01"));
    EXPECT_FALSE(colorCrystalForBladeVariation(0));
    EXPECT_FALSE(colorCrystalForBladeVariation(6));
}

TEST_F(UpgradeRulesTest, an_installed_upgrade_puts_its_properties_in_force) {
    auto properties = propertiesWithUpgrades({16, 17});
    auto blaster = acceptedUpgradeRows(properties);
    auto slots = upgradeSlots(false, blaster, _table);
    auto change = planSlotChange(slots, 0, blaster, 0, 0, _table, "g_i_upgrade001");
    ASSERT_TRUE(change);
    // The property in force whatever the upgrades stays in force.
    EXPECT_TRUE(Item::isPropertyActive(change->upgrades, properties[0].upgradeType));
    EXPECT_TRUE(Item::isPropertyActive(change->upgrades, properties[1].upgradeType));
    EXPECT_FALSE(Item::isPropertyActive(change->upgrades, properties[2].upgradeType));
    EXPECT_FALSE(Item::isPropertyActive(0, properties[1].upgradeType));
}

TEST_F(UpgradeRulesTest, the_bench_lists_an_item_by_its_base_item) {
    EXPECT_EQ(UpgradeCategory::Lightsaber, upgradeCategoryOf(true, false, false));
    EXPECT_EQ(UpgradeCategory::Ranged, upgradeCategoryOf(false, true, false));
    EXPECT_EQ(UpgradeCategory::Armor, upgradeCategoryOf(false, false, true));
    EXPECT_EQ(UpgradeCategory::Melee, upgradeCategoryOf(false, false, false));
}

// The bench on a party

namespace {

constexpr int kBlasterBaseItem = 0;
constexpr int kArmorBaseItem = 1;
constexpr int kMeleeBaseItem = 2;
constexpr int kUpgradeBaseItem = 3;
constexpr int kLightsaberBaseItem = 8;
constexpr int kCrystalBaseItem = 11;

std::shared_ptr<TwoDA> makeBaseItems() {
    TwoDA::Builder builder;
    builder.columns({"equipableslots", "itemclass", "rangedweapon"});
    builder.row({"16", "w_blstrpstl", "1"}); // 0 blaster
    builder.row({"2", "a_class4", ""});      // 1 body armour
    builder.row({"16", "w_vbroswrd", ""});   // 2 vibroblade
    builder.row({"", "i_upgrade", ""});      // 3 upgrade item
    for (int i = 4; i < 8; ++i) {
        builder.row({"", "", ""});
    }
    builder.row({"16", "w_lghtsbr", ""}); // 8 lightsaber
    builder.row({"", "", ""});
    builder.row({"", "", ""});
    builder.row({"", "i_crystal", ""}); // 11 crystal
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Gff> makeItemRecord(const std::string &resRef, int baseItem, std::vector<int> upgradeRows, int stackSize = 1, int modelVariation = 1) {
    std::vector<std::shared_ptr<Gff>> properties;
    for (int row : upgradeRows) {
        properties.push_back(Gff::Builder()
                                 .field(Gff::Field::newWord("PropertyName", 0))
                                 .field(Gff::Field::newByte("UpgradeType", row))
                                 .build());
    }
    return Gff::Builder()
        .field(Gff::Field::newResRef("TemplateResRef", resRef))
        .field(Gff::Field::newCExoString("Tag", boost::to_upper_copy(resRef)))
        .field(Gff::Field::newInt("BaseItem", baseItem))
        .field(Gff::Field::newWord("StackSize", stackSize))
        .field(Gff::Field::newByte("ModelVariation", modelVariation))
        .field(Gff::Field::newList("PropertiesList", std::move(properties)))
        .build();
}

struct LoadedUpgradeGUIs {
    std::vector<std::shared_ptr<gui::MockGUI>> guis;
    std::map<std::string, std::map<std::string, std::shared_ptr<gui::Control>>> controls;
};

class UpgradeBenchTest : public ::testing::Test {
protected:
    void SetUp() override {
        _engine.init();
        // Item descriptions read property tables; empty ones describe nothing.
        ON_CALL(_engine.resourceModule().twoDas(), get(_))
            .WillByDefault(Return(std::shared_ptr<TwoDA>(TwoDA::Builder().columns({"label"}).build())));
        ON_CALL(_engine.resourceModule().twoDas(), get("upgrade")).WillByDefault(Return(makeUpgradeTable()));
        ON_CALL(_engine.resourceModule().twoDas(), get("baseitems")).WillByDefault(Return(makeBaseItems()));
        EXPECT_CALL(_engine.resourceModule().twoDas(), get(_)).Times(AnyNumber());
        EXPECT_CALL(_engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
        EXPECT_CALL(_engine.resourceModule().models(), get(_)).Times(AnyNumber());
        EXPECT_CALL(_engine.resourceModule().gffs(), get(_, _)).Times(AnyNumber());
        for (int i = 1; i <= 9; ++i) {
            auto resRef = str(boost::format("g_i_upgrade%03d") % i);
            ON_CALL(_engine.resourceModule().gffs(), get(resRef, ResType::Uti))
                .WillByDefault(Return(makeItemRecord(resRef, kUpgradeBaseItem, {})));
        }
        for (int i = 14; i <= 18; ++i) {
            auto resRef = str(boost::format("g_w_sbrcrstl%02d") % i);
            ON_CALL(_engine.resourceModule().gffs(), get(resRef, ResType::Uti))
                .WillByDefault(Return(makeItemRecord(resRef, kCrystalBaseItem, {})));
        }

        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _player = _game->newCreature();
        _game->party().addMember(kNpcPlayer, _player);
        _game->party().setPlayer(_player);
    }

    std::shared_ptr<Item> give(const std::string &resRef, int baseItem, std::vector<int> upgradeRows, int stackSize = 1, int modelVariation = 1) {
        auto item = _game->newItem();
        item->deserialize(*makeItemRecord(resRef, baseItem, std::move(upgradeRows), stackSize, modelVariation),
                          SerializedIdentityContext::templateResource(resRef));
        return _player->addItem(item);
    }

    int carried(const std::string &resRef) const {
        int count = 0;
        for (const auto &item : _player->items()) {
            if (UpgradeBench::templateOf(*item) == resRef) {
                count += item->stackSize();
            }
        }
        return count;
    }

    void installUpgradeGUIs() {
        EXPECT_CALL(_engine.guiModule().guis(), get(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](const std::string &resRef, std::function<void(gui::IGUI &)> preload)
                                       -> std::shared_ptr<gui::IGUI> {
                auto mock = std::make_shared<NiceMock<gui::MockGUI>>();
                gui::MockGUI *raw = mock.get();
                auto *controls = &_loaded.controls[resRef];
                ON_CALL(*mock, findControl(_)).WillByDefault(Invoke([this, raw, controls](const std::string &tag) {
                    auto found = controls->find(tag);
                    if (found != controls->end()) {
                        return found->second;
                    }
                    std::shared_ptr<gui::Control> control;
                    auto &services = _engine.services();
                    if (tag.rfind("BTN_", 0) == 0) {
                        control = std::make_shared<gui::Button>(*raw, services.scene.graphs, services.graphics, services.resource);
                    } else if (tag.rfind("LB_", 0) == 0) {
                        control = std::make_shared<gui::ListBox>(*raw, services.scene.graphs, services.graphics, services.resource);
                    } else {
                        control = std::make_shared<gui::Label>(*raw, services.scene.graphs, services.graphics, services.resource);
                    }
                    control->setTag(tag);
                    controls->emplace(tag, control);
                    return control;
                }));
                preload(*mock);
                _loaded.guis.push_back(mock);
                return mock;
            }));
    }

    std::shared_ptr<gui::Control> control(const std::string &gui, const std::string &tag) {
        return _loaded.controls[gui][tag];
    }

    script::Variable showUpgradeScreen(uint32_t item) {
        Routines routines(GameID::KotOR, _game.get(), &_engine.services());
        routines.init();
        script::Routine &routine = routines.get(routines.getIndexByName("ShowUpgradeScreen"));
        script::ExecutionContext execution;
        execution.routines = &routines;
        return routine.invoke({script::Variable::ofObject(item)}, execution);
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Creature> _player;
    LoadedUpgradeGUIs _loaded;
};

} // namespace

TEST_F(UpgradeBenchTest, the_bench_lists_the_party_items_of_a_kind_that_have_a_slot) {
    auto blaster = give("g_w_blstrpstl01", kBlasterBaseItem, range(16, 19));
    auto armor = give("g_a_class4001", kArmorBaseItem, {20, 21});
    auto vibroblade = give("g_w_vbroswrd01", kMeleeBaseItem, range(13, 15));
    auto saber = give("g_w_lghtsbr01", kLightsaberBaseItem, lightsaberRows());
    give("g_w_blstrpstl02", kBlasterBaseItem, {});
    auto scope = give("g_i_upgrade001", kUpgradeBaseItem, {});
    auto crystal = give("g_w_sbrcrstl18", kCrystalBaseItem, {});

    UpgradeBench bench(*_game, _engine.services());
    EXPECT_EQ((std::vector<std::shared_ptr<Item>> {blaster}), bench.upgradeableItems(UpgradeCategory::Ranged));
    EXPECT_EQ((std::vector<std::shared_ptr<Item>> {armor}), bench.upgradeableItems(UpgradeCategory::Armor));
    EXPECT_EQ((std::vector<std::shared_ptr<Item>> {vibroblade}), bench.upgradeableItems(UpgradeCategory::Melee));
    EXPECT_EQ((std::vector<std::shared_ptr<Item>> {saber}), bench.upgradeableItems(UpgradeCategory::Lightsaber));
    EXPECT_EQ((std::vector<std::shared_ptr<Item>> {scope, crystal}), bench.carriedUpgrades());
}

TEST_F(UpgradeBenchTest, fitting_and_taking_out_an_upgrade_moves_it_between_inventory_and_item) {
    auto blaster = give("g_w_blstrpstl01", kBlasterBaseItem, range(16, 19));
    give("g_i_upgrade001", kUpgradeBaseItem, {}, 2);
    give("g_i_upgrade007", kUpgradeBaseItem, {});

    UpgradeBench bench(*_game, _engine.services());
    // The vibration cell fits no blaster slot.
    auto fitting = bench.carriedFittingSlot(*blaster, 0);
    ASSERT_EQ(1u, fitting.size());
    EXPECT_EQ("g_i_upgrade001", UpgradeBench::templateOf(*fitting[0]));
    EXPECT_FALSE(bench.apply(*blaster, 0, "g_i_upgrade007"));

    ASSERT_TRUE(bench.apply(*blaster, 0, "g_i_upgrade001"));
    EXPECT_EQ(bit(16), blaster->upgrades());
    EXPECT_EQ(1, carried("g_i_upgrade001"));
    EXPECT_EQ((std::vector<int> {16, -1, -1, -1}), bench.installedRowsOf(*blaster));
    // With the slot full, the spare scope would only take it out again.
    EXPECT_TRUE(bench.carriedFittingSlot(*blaster, 0).empty());

    ASSERT_TRUE(bench.apply(*blaster, 0, "g_i_upgrade001"));
    EXPECT_EQ(0u, blaster->upgrades());
    EXPECT_EQ(2, carried("g_i_upgrade001"));
}

TEST_F(UpgradeBenchTest, the_last_upgrade_carried_leaves_the_inventory_when_fitted) {
    auto blaster = give("g_w_blstrpstl01", kBlasterBaseItem, range(16, 19));
    give("g_i_upgrade002", kUpgradeBaseItem, {});

    UpgradeBench bench(*_game, _engine.services());
    ASSERT_TRUE(bench.apply(*blaster, 1, "g_i_upgrade002"));
    EXPECT_EQ(bit(17), blaster->upgrades());
    EXPECT_EQ(0, carried("g_i_upgrade002"));
    // Nothing carried, nothing fitted.
    EXPECT_FALSE(bench.apply(*blaster, 0, "g_i_upgrade001"));
    EXPECT_EQ(bit(17), blaster->upgrades());
}

TEST_F(UpgradeBenchTest, a_colour_crystal_changes_the_blade_and_returns_the_old_crystal) {
    auto saber = give("g_w_lghtsbr01", kLightsaberBaseItem, lightsaberRows(), 1, 1);
    give("g_w_sbrcrstl17", kCrystalBaseItem, {});

    UpgradeBench bench(*_game, _engine.services());
    ASSERT_EQ(1u, bench.carriedFittingSlot(*saber, 0).size());
    ASSERT_TRUE(bench.apply(*saber, 0, "g_w_sbrcrstl17"));
    EXPECT_EQ(5, saber->modelVariation());
    EXPECT_EQ(0, carried("g_w_sbrcrstl17"));
    EXPECT_EQ(1, carried("g_w_sbrcrstl14"));
    EXPECT_EQ(0u, saber->upgrades());
}

TEST_F(UpgradeBenchTest, show_upgrade_screen_leaves_the_screen_alone_when_the_bench_will_not_load) {
    _game->openInGame();
    ASSERT_EQ(Game::Screen::InGame, _game->currentScreen());
    EXPECT_CALL(_engine.guiModule().guis(), get(_, _)).Times(AnyNumber());

    // The routine used to throw as unimplemented.
    EXPECT_NO_THROW(showUpgradeScreen(script::kObjectInvalid));
    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen());
}

TEST_F(UpgradeBenchTest, show_upgrade_screen_without_an_item_opens_the_selection) {
    installUpgradeGUIs();
    give("g_w_blstrpstl01", kBlasterBaseItem, range(16, 19));
    _game->openInGame();

    EXPECT_NO_THROW(showUpgradeScreen(script::kObjectInvalid));
    EXPECT_EQ(Game::Screen::UpgradeSelection, _game->currentScreen());
    EXPECT_FALSE(control("upgradesel", "BTN_RANGED")->isDisabled());
    // Kinds the party has nothing of cannot be picked.
    EXPECT_TRUE(control("upgradesel", "BTN_MELEE")->isDisabled());
    EXPECT_TRUE(control("upgradesel", "BTN_LIGHTSABER")->isDisabled());
    EXPECT_TRUE(control("upgradesel", "BTN_ARMOR")->isDisabled());

    _game->openUpgradeItems(UpgradeCategory::Ranged);
    EXPECT_EQ(Game::Screen::UpgradeItems, _game->currentScreen());
    EXPECT_EQ(1u, UpgradeScreensTestAccess::listed(UpgradeScreensTestAccess::items(*_game)));
    EXPECT_TRUE(control("upgradeitems", "BTN_UPGRADEITEM")->isVisible());

    // The carried upgrade items are listed, not upgraded.
    _game->openUpgradeItems(std::nullopt);
    EXPECT_EQ(0u, UpgradeScreensTestAccess::listed(UpgradeScreensTestAccess::items(*_game)));
    EXPECT_FALSE(control("upgradeitems", "BTN_UPGRADEITEM")->isVisible());
}

TEST_F(UpgradeBenchTest, an_item_without_a_slot_opens_the_selection) {
    installUpgradeGUIs();
    auto plain = give("g_w_blstrpstl02", kBlasterBaseItem, {});
    _game->openInGame();

    showUpgradeScreen(plain->id());

    EXPECT_EQ(Game::Screen::UpgradeSelection, _game->currentScreen());
}

TEST_F(UpgradeBenchTest, show_upgrade_screen_with_an_item_goes_straight_to_its_slots) {
    installUpgradeGUIs();
    auto vibroblade = give("g_w_vbroswrd01", kMeleeBaseItem, range(13, 15));
    give("g_i_upgrade008", kUpgradeBaseItem, {});
    _game->openInGame();

    showUpgradeScreen(vibroblade->id());

    ASSERT_EQ(Game::Screen::Upgrade, _game->currentScreen());
    auto *gui = &UpgradeScreensTestAccess::upgrade(*_game);
    EXPECT_EQ(3u, UpgradeScreensTestAccess::slots(*gui).size());
    // A melee weapon's three slots take the three-slot row.
    EXPECT_TRUE(control("upgrade", "BTN_UPGRADE31")->isVisible());
    EXPECT_TRUE(control("upgrade", "BTN_UPGRADE33")->isVisible());
    EXPECT_FALSE(control("upgrade", "BTN_UPGRADE41")->isVisible());
    EXPECT_FALSE(control("upgrade", "LB_ITEMS")->isVisible());
    EXPECT_EQ("0/3", control("upgrade", "LBL_UPGRADE_COUNT")->text().text);

    // The alloy slot lists the carried alloy; picking it fits it.
    UpgradeScreensTestAccess::openSlot(*gui, 1);
    EXPECT_TRUE(control("upgrade", "LB_ITEMS")->isVisible());
    EXPECT_FALSE(control("upgrade", "BTN_UPGRADE31")->isVisible());
    ASSERT_EQ((std::vector<std::string> {"g_i_upgrade008"}), UpgradeScreensTestAccess::choices(*gui));
    UpgradeScreensTestAccess::fitChoice(*gui, 0);
    EXPECT_EQ(bit(14), vibroblade->upgrades());
    EXPECT_EQ(0, carried("g_i_upgrade008"));
    EXPECT_EQ(-1, UpgradeScreensTestAccess::picking(*gui));
    EXPECT_EQ("1/3", control("upgrade", "LBL_UPGRADE_COUNT")->text().text);

    // The slot now offers the alloy it holds, which takes it out.
    UpgradeScreensTestAccess::openSlot(*gui, 1);
    ASSERT_EQ((std::vector<std::string> {"g_i_upgrade008 (installed)"}), UpgradeScreensTestAccess::choices(*gui));
    UpgradeScreensTestAccess::fitChoice(*gui, 0);
    EXPECT_EQ(0u, vibroblade->upgrades());
    EXPECT_EQ(1, carried("g_i_upgrade008"));

    // Back from the slots returns to the game, the bench having been opened on the item.
    UpgradeScreensTestAccess::back(*gui);
    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen());
}

TEST_F(UpgradeBenchTest, a_blaster_takes_the_four_slot_row) {
    installUpgradeGUIs();
    auto blaster = give("g_w_blstrpstl01", kBlasterBaseItem, range(16, 19));
    _game->openInGame();

    _game->openUpgradeScreen(blaster);

    ASSERT_EQ(Game::Screen::Upgrade, _game->currentScreen());
    EXPECT_FALSE(control("upgrade", "BTN_UPGRADE31")->isVisible());
    for (const auto *tag : {"BTN_UPGRADE41", "BTN_UPGRADE42", "BTN_UPGRADE43", "BTN_UPGRADE44"}) {
        EXPECT_TRUE(control("upgrade", tag)->isVisible()) << tag;
    }
}

TEST_F(UpgradeBenchTest, armour_shows_two_slots_and_hides_the_third) {
    installUpgradeGUIs();
    auto armor = give("g_a_class4001", kArmorBaseItem, {20, 21});
    _game->openInGame();

    _game->openItemUpgrade(armor, true);

    ASSERT_EQ(Game::Screen::Upgrade, _game->currentScreen());
    EXPECT_TRUE(control("upgrade", "BTN_UPGRADE31")->isVisible());
    EXPECT_TRUE(control("upgrade", "BTN_UPGRADE32")->isVisible());
    EXPECT_FALSE(control("upgrade", "BTN_UPGRADE33")->isVisible());

    // Opened from the list, leaving returns to it.
    auto *gui = &UpgradeScreensTestAccess::upgrade(*_game);
    UpgradeScreensTestAccess::back(*gui);
    EXPECT_EQ(Game::Screen::UpgradeItems, _game->currentScreen());
}

TEST_F(UpgradeBenchTest, a_lightsaber_shows_its_crystal_slots_on_the_lightsaber_layout) {
    installUpgradeGUIs();
    auto saber = give("g_w_lghtsbr01", kLightsaberBaseItem, lightsaberRows());
    _game->openInGame();

    _game->openUpgradeScreen(saber);

    auto *gui = &UpgradeScreensTestAccess::upgrade(*_game);
    EXPECT_EQ((std::vector<UpgradeSlot> {kColor, upgradeSlot(0), upgradeSlot(0)}), UpgradeScreensTestAccess::slots(*gui));
    EXPECT_TRUE(control("upgrade", "BTN_UPGRADE33")->isVisible());
    EXPECT_TRUE(control("upgrade", "LB_DESC_LS")->isVisible());
    EXPECT_FALSE(control("upgrade", "LB_DESC")->isVisible());
    EXPECT_FALSE(control("upgrade", "LBL_UPGRADE_COUNT")->isVisible());
    // Nothing carried fits; the colour slot never offers a way to take the crystal out.
    UpgradeScreensTestAccess::openSlot(*gui, 0);
    EXPECT_TRUE(UpgradeScreensTestAccess::choices(*gui).empty());
}
