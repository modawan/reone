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

#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "object/item.h"

namespace reone {

namespace resource {
class TwoDA;
}

namespace game {

/**
 * The rules of KotOR's upgrade bench, kept apart from its screens.
 *
 * What the game data says: a row of upgrade.2da is a bit of an item's
 * Upgrades, names the upgrade item that sets it and the kind of slot it goes
 * in (a row of upgradetypes.2da). A property whose UpgradeType is a row is in
 * force only while that bit is set, so an item takes the upgrades its
 * properties name. The colour crystals are not in upgrade.2da: a lightsaber's
 * ModelVariation picks its blade.
 *
 * The rest is chosen, not read from the data, and is marked so below.
 */

/** The four kinds of item the bench lists. */
enum class UpgradeCategory {
    Ranged,
    Melee,
    Lightsaber,
    Armor
};

/** A row of upgrade.2da. */
struct UpgradeRow {
    /** The row, which is also the Upgrades bit. */
    int row {0};
    /** The upgrade item's template, lower case. */
    std::string templateResRef;
    /** The slot kind, a row of upgradetypes.2da. */
    int slotKind {0};
};

/** upgrade.2da as the bench reads it. */
class UpgradeTable {
public:
    UpgradeTable() = default;
    explicit UpgradeTable(std::vector<UpgradeRow> rows) :
        _rows(std::move(rows)) {}

    /** Rows without a template or a slot kind are skipped. */
    static UpgradeTable fromTwoDA(const resource::TwoDA &upgrade);

    const std::vector<UpgradeRow> &rows() const { return _rows; }
    /** The row whose upgrade item has this template, whatever its case. */
    const UpgradeRow *findByTemplate(const std::string &resRef) const;
    const UpgradeRow *findByRow(int row) const;

private:
    std::vector<UpgradeRow> _rows;
};

/** A slot on the upgrade screen. */
struct UpgradeSlot {
    enum class Type {
        /** A lightsaber's colour crystal: it sets the blade, not an Upgrades bit. */
        ColorCrystal,
        /** An upgrade of slotKind: it sets an Upgrades bit. */
        Upgrade
    };

    Type type {Type::Upgrade};
    /** The slot kind of an upgrade slot; -1 for a colour crystal slot. */
    int slotKind {-1};

    bool operator==(const UpgradeSlot &other) const {
        return type == other.type && slotKind == other.slotKind;
    }
};

/** The slot kind of lightsaber power crystals (upgradetypes.2da Lightsaber_Upgrade). */
constexpr int kLightsaberUpgradeKind = 0;

/**
 * Chosen: a lightsaber has two power crystal slots. The data allows any number
 * of crystals, one per bit; the screen authors three slots on the lightsaber
 * row, one of which the colour crystal takes.
 */
constexpr int kPowerCrystalSlots = 2;

/** The rows of upgrade.2da an item takes: the UpgradeTypes its properties carry. */
std::set<int> acceptedUpgradeRows(const std::vector<Item::PropertyEntry> &properties);

/**
 * The category the bench lists an item under. Chosen: by its base item, a
 * lightsaber first, then a ranged weapon, then body armour, and every other
 * item as melee. An item is listed only when it has a slot.
 */
UpgradeCategory upgradeCategoryOf(bool lightsaber, bool ranged, bool bodyArmor);

/**
 * The slots the screen shows for an item, in screen order.
 *
 * Chosen: one slot for each slot kind the item takes an upgrade of, by kind;
 * a lightsaber shows its colour crystal slot first and then two power crystal
 * slots, the latter only when it takes a power crystal. A kind the item takes
 * no upgrade of has no slot.
 */
std::vector<UpgradeSlot> upgradeSlots(bool lightsaber, const std::set<int> &accepted, const UpgradeTable &table);

/** The rows an item could put in a slot: the rows of the slot's kind that it takes. */
std::vector<int> rowsFittingSlot(const UpgradeSlot &slot, const std::set<int> &accepted, const UpgradeTable &table);

/**
 * The row in each slot, -1 for an empty slot or a colour crystal slot.
 * Chosen: the bits set for a kind fill its slots in row order; bits beyond
 * the slots of their kind, or for a row the item does not take, are not shown.
 */
std::vector<int> installedRows(
    const std::vector<UpgradeSlot> &slots,
    const std::set<int> &accepted,
    uint32_t upgrades,
    const UpgradeTable &table);

/**
 * The blade ModelVariation a colour crystal gives a lightsaber, by the
 * crystal's template: g_w_sbrcrstl14 blue 1, 15 yellow 4, 16 green 3,
 * 17 violet 5, 18 red 2.
 */
std::optional<int> colorCrystalBladeVariation(const std::string &templateResRef);
/** The colour crystal that gives this blade variation, when one does. */
std::optional<std::string> colorCrystalForBladeVariation(int variation);

/** What putting an upgrade in a slot, or taking one out, comes to. */
struct UpgradeChange {
    uint32_t upgrades {0};
    int modelVariation {0};
    /** The template of the upgrade item taken from the inventory; empty for none. */
    std::string consumed;
    /** The template of the upgrade item given back to the inventory; empty for none. */
    std::string returned;
};

/**
 * Plans fitting the upgrade item of template \p chosen into slot \p slotIndex.
 *
 * Chosen rules: an upgrade slot holds one upgrade. Fitting one swaps out
 * whatever the slot held, which goes back to the inventory. Choosing the
 * upgrade the slot already holds takes it out. A colour crystal sets the
 * blade, and the crystal of the blade it replaces, when the blade is one a
 * crystal gives, goes back; a lightsaber always keeps a crystal, so choosing
 * the current colour changes nothing.
 *
 * @return nothing when the choice does not fit the slot or changes nothing
 */
std::optional<UpgradeChange> planSlotChange(
    const std::vector<UpgradeSlot> &slots,
    int slotIndex,
    const std::set<int> &accepted,
    uint32_t upgrades,
    int modelVariation,
    const UpgradeTable &table,
    const std::string &chosen);

} // namespace game

} // namespace reone
