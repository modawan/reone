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

#include "../fixtures/itempresentation.h"

namespace {

// TSL's shipped equip_p.gui carries the second weapon set's slots but none of
// the attack and damage labels beside them.
const std::set<std::string> kSecondWeaponSetLabels {
    "LBL_ATKR2", "LBL_ATKL2", "LBL_TOHITL2", "LBL_TOHITR2"};

std::shared_ptr<SuppliedItems> weaponSets() {
    auto backing = std::make_shared<SuppliedItems>();
    backing->equipment.slotAvailable = true;
    backing->equipment.mainDamage = "1-6";
    backing->equipment.offDamage = "1-4";
    backing->equipment.mainAttack = "+5";
    backing->equipment.offAttack = "+3";
    backing->equipment.mainDamage2 = "2-8";
    backing->equipment.offDamage2 = "2-5";
    backing->equipment.mainAttack2 = "+7";
    backing->equipment.offAttack2 = "+4";
    return backing;
}

// SharedPresentation lives in an anonymous namespace, so each translation
// unit gets its own class; gtest needs one fixture class per test suite.
class EquipmentWeaponSetLabels : public SharedPresentation {};

std::string labelText(const std::shared_ptr<RecordingGUI> &screen, const std::string &tag) {
    auto control = screen->findControl(tag);
    return control ? control->text().text : std::string("<absent>");
}

TEST_F(EquipmentWeaponSetLabels, tsl_equipment_updates_when_the_second_weapon_set_labels_are_absent) {
    ScreenResources guis(options, scene, graphics, resources);
    guis.absent = kSecondWeaponSetLabels;
    auto backing = weaponSets();
    auto equipment = std::make_shared<Equipment>(
        GameID::TSL, options, services(guis), resources.services().strings, backing, []() {});
    equipment->init();

    // Without the guard this dereferences LBL_ATKR2 and faults.
    equipment->update();

    auto screen = guis.screens.at("equip_p");
    for (const auto &tag : kSecondWeaponSetLabels) {
        EXPECT_EQ(nullptr, screen->findControl(tag)) << tag << " should stay absent";
    }
    // The first weapon set still receives its values.
    EXPECT_EQ("1-6", labelText(screen, "LBL_ATKR"));
    EXPECT_EQ("1-4", labelText(screen, "LBL_ATKL"));
    EXPECT_EQ("+3", labelText(screen, "LBL_TOHITL"));
    EXPECT_EQ("+5", labelText(screen, "LBL_TOHITR"));
}

TEST_F(EquipmentWeaponSetLabels, tsl_equipment_fills_the_second_weapon_set_labels_when_present) {
    ScreenResources guis(options, scene, graphics, resources);
    auto backing = weaponSets();
    auto equipment = std::make_shared<Equipment>(
        GameID::TSL, options, services(guis), resources.services().strings, backing, []() {});
    equipment->init();

    equipment->update();

    auto screen = guis.screens.at("equip_p");
    EXPECT_EQ("1-6", labelText(screen, "LBL_ATKR"));
    EXPECT_EQ("1-4", labelText(screen, "LBL_ATKL"));
    EXPECT_EQ("+3", labelText(screen, "LBL_TOHITL"));
    EXPECT_EQ("+5", labelText(screen, "LBL_TOHITR"));
    // A layout that does define them is still filled in.
    EXPECT_EQ("2-8", labelText(screen, "LBL_ATKR2"));
    EXPECT_EQ("2-5", labelText(screen, "LBL_ATKL2"));
    EXPECT_EQ("+4", labelText(screen, "LBL_TOHITL2"));
    EXPECT_EQ("+7", labelText(screen, "LBL_TOHITR2"));
}

TEST_F(EquipmentWeaponSetLabels, kotor_equipment_keeps_its_single_weapon_set) {
    ScreenResources guis(options, scene, graphics, resources);
    // KotOR has no second weapon set at all, labels included.
    guis.absent = kSecondWeaponSetLabels;
    auto backing = weaponSets();
    auto equipment = std::make_shared<Equipment>(
        GameID::KotOR, options, services(guis), resources.services().strings, backing, []() {});
    equipment->init();

    equipment->update();

    auto screen = guis.screens.at("equip");
    EXPECT_EQ("1-6", labelText(screen, "LBL_ATKR"));
    EXPECT_EQ("1-4", labelText(screen, "LBL_ATKL"));
    EXPECT_EQ("+3", labelText(screen, "LBL_TOHITL"));
    EXPECT_EQ("+5", labelText(screen, "LBL_TOHITR"));
}

} // namespace
