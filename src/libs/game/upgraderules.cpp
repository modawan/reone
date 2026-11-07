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

#include "reone/game/upgraderules.h"

#include "reone/resource/2da.h"

namespace reone {

namespace game {

/** Upgrades is a DWORD: rows past its bits cannot be installed. */
static constexpr int kUpgradeBits = 32;

namespace {

struct ColorCrystal {
    const char *templateResRef;
    int bladeVariation;
};

// The colour crystals' blades, from their names (dialog.tlk 32501 to 32509)
// and the lightsaber blade models w_lghtsbr_001 to 005.
constexpr ColorCrystal kColorCrystals[] = {
    {"g_w_sbrcrstl14", 1}, // blue
    {"g_w_sbrcrstl15", 4}, // yellow
    {"g_w_sbrcrstl16", 3}, // green
    {"g_w_sbrcrstl17", 5}, // violet
    {"g_w_sbrcrstl18", 2}, // red
};

uint32_t bit(int row) {
    return uint32_t {1} << row;
}

} // namespace

UpgradeTable UpgradeTable::fromTwoDA(const resource::TwoDA &upgrade) {
    std::vector<UpgradeRow> rows;
    for (int row = 0; row < upgrade.getRowCount() && row < kUpgradeBits; ++row) {
        auto resRef = boost::to_lower_copy(upgrade.getString(row, "template"));
        auto kind = upgrade.getIntOpt(row, "upgradetype");
        if (resRef.empty() || !kind) {
            continue;
        }
        rows.push_back(UpgradeRow {row, std::move(resRef), *kind});
    }
    return UpgradeTable(std::move(rows));
}

const UpgradeRow *UpgradeTable::findByTemplate(const std::string &resRef) const {
    for (const auto &row : _rows) {
        if (boost::iequals(row.templateResRef, resRef)) {
            return &row;
        }
    }
    return nullptr;
}

const UpgradeRow *UpgradeTable::findByRow(int row) const {
    for (const auto &entry : _rows) {
        if (entry.row == row) {
            return &entry;
        }
    }
    return nullptr;
}

std::set<int> acceptedUpgradeRows(const std::vector<Item::PropertyEntry> &properties) {
    std::set<int> rows;
    for (const auto &property : properties) {
        if (property.upgradeType != Item::PropertyEntry::kNoUpgrade && property.upgradeType < kUpgradeBits) {
            rows.insert(property.upgradeType);
        }
    }
    return rows;
}

UpgradeCategory upgradeCategoryOf(bool lightsaber, bool ranged, bool bodyArmor) {
    if (lightsaber) {
        return UpgradeCategory::Lightsaber;
    }
    if (ranged) {
        return UpgradeCategory::Ranged;
    }
    if (bodyArmor) {
        return UpgradeCategory::Armor;
    }
    return UpgradeCategory::Melee;
}

std::vector<UpgradeSlot> upgradeSlots(bool lightsaber, const std::set<int> &accepted, const UpgradeTable &table) {
    std::set<int> kinds;
    for (const auto &row : table.rows()) {
        if (accepted.count(row.row) > 0) {
            kinds.insert(row.slotKind);
        }
    }
    std::vector<UpgradeSlot> slots;
    if (lightsaber) {
        slots.push_back(UpgradeSlot {UpgradeSlot::Type::ColorCrystal, -1});
        if (kinds.count(kLightsaberUpgradeKind) > 0) {
            for (int i = 0; i < kPowerCrystalSlots; ++i) {
                slots.push_back(UpgradeSlot {UpgradeSlot::Type::Upgrade, kLightsaberUpgradeKind});
            }
        }
        return slots;
    }
    for (int kind : kinds) {
        slots.push_back(UpgradeSlot {UpgradeSlot::Type::Upgrade, kind});
    }
    return slots;
}

std::vector<int> rowsFittingSlot(const UpgradeSlot &slot, const std::set<int> &accepted, const UpgradeTable &table) {
    std::vector<int> rows;
    if (slot.type != UpgradeSlot::Type::Upgrade) {
        return rows;
    }
    for (const auto &row : table.rows()) {
        if (row.slotKind == slot.slotKind && accepted.count(row.row) > 0) {
            rows.push_back(row.row);
        }
    }
    return rows;
}

std::vector<int> installedRows(
    const std::vector<UpgradeSlot> &slots,
    const std::set<int> &accepted,
    uint32_t upgrades,
    const UpgradeTable &table) {

    std::vector<int> result(slots.size(), -1);
    std::set<int> placed;
    for (size_t i = 0; i < slots.size(); ++i) {
        for (int row : rowsFittingSlot(slots[i], accepted, table)) {
            if ((upgrades & bit(row)) != 0 && placed.count(row) == 0) {
                result[i] = row;
                placed.insert(row);
                break;
            }
        }
    }
    return result;
}

std::optional<int> colorCrystalBladeVariation(const std::string &templateResRef) {
    for (const auto &crystal : kColorCrystals) {
        if (boost::iequals(crystal.templateResRef, templateResRef)) {
            return crystal.bladeVariation;
        }
    }
    return std::nullopt;
}

std::optional<std::string> colorCrystalForBladeVariation(int variation) {
    for (const auto &crystal : kColorCrystals) {
        if (crystal.bladeVariation == variation) {
            return std::string(crystal.templateResRef);
        }
    }
    return std::nullopt;
}

std::optional<UpgradeChange> planSlotChange(
    const std::vector<UpgradeSlot> &slots,
    int slotIndex,
    const std::set<int> &accepted,
    uint32_t upgrades,
    int modelVariation,
    const UpgradeTable &table,
    const std::string &chosen) {

    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots.size())) {
        return std::nullopt;
    }
    const auto &slot = slots[slotIndex];
    UpgradeChange change;
    change.upgrades = upgrades;
    change.modelVariation = modelVariation;

    if (slot.type == UpgradeSlot::Type::ColorCrystal) {
        auto variation = colorCrystalBladeVariation(chosen);
        if (!variation || *variation == modelVariation) {
            return std::nullopt;
        }
        change.modelVariation = *variation;
        change.consumed = boost::to_lower_copy(chosen);
        change.returned = colorCrystalForBladeVariation(modelVariation).value_or("");
        return change;
    }

    const int installed = installedRows(slots, accepted, upgrades, table)[slotIndex];
    const auto *installedRow = installed >= 0 ? table.findByRow(installed) : nullptr;
    const auto *chosenRow = table.findByTemplate(chosen);
    if (!chosenRow) {
        return std::nullopt;
    }
    if (installedRow && chosenRow->row == installedRow->row) {
        change.upgrades &= ~bit(installedRow->row);
        change.returned = installedRow->templateResRef;
        return change;
    }
    auto fitting = rowsFittingSlot(slot, accepted, table);
    if (std::find(fitting.begin(), fitting.end(), chosenRow->row) == fitting.end()) {
        return std::nullopt;
    }
    // A bit is one upgrade: another slot already holding it keeps it.
    if ((upgrades & bit(chosenRow->row)) != 0) {
        return std::nullopt;
    }
    if (installedRow) {
        change.upgrades &= ~bit(installedRow->row);
        change.returned = installedRow->templateResRef;
    }
    change.upgrades |= bit(chosenRow->row);
    change.consumed = chosenRow->templateResRef;
    return change;
}

} // namespace game

} // namespace reone
