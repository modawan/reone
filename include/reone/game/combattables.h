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

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "reone/resource/types.h"

namespace reone {

namespace resource {

class ITwoDAs;

} // namespace resource

namespace game {

/** A row of an item property cost table. A blank cell has no value. */
struct CostTableRow {
    std::optional<int> value;
    std::optional<int> amount;
};

/** An item property cost table. A row outside it reads as blank. */
struct CostTable {
    std::vector<CostTableRow> rows;

    /** The row at \p index, or a blank row outside the table. */
    CostTableRow row(int index) const {
        return index >= 0 && index < static_cast<int>(rows.size()) ? rows[index] : CostTableRow();
    }
};

/**
 * A damage bonus code (iprp_damagecost): its rank and dice. A blank cell, or
 * a code outside the table, is zero rather than a literal damage amount.
 */
struct DamageCost {
    int rank {0};
    int numDice {0};
    int die {0};
};

/** A monster damage code (iprp_monstcost): its dice. A code outside the table has none. */
struct MonsterDamageCost {
    int numDice {0};
    int die {0};
};

/** An on-hit property's chance and duration (iprp_onhitdur). A row outside the table is zero. */
struct OnHitDuration {
    int chance {0};
    int rounds {0};
};

/** A Force shield (forceshields.2da). A blank cell, or a shield outside the table, is zero. */
struct ForceShieldRow {
    /** The number of appearances with a visual of their own. */
    static constexpr int kAppearanceCount = 4;
    /** The number of radius columns, radius_00 to radius_05. */
    static constexpr int kRadiusCount = 6;

    int visual {0};
    /** The appearances with a visual of their own; a blank one matches none. */
    std::array<std::optional<int>, kAppearanceCount> appearances;
    /** The visual of each of those appearances. */
    std::array<int, kAppearanceCount> appearanceVisuals {};
    std::array<float, kRadiusCount> radii {};
    int damageFlags {0};
    int vulnerabilities {0};
    int resistance {0};
    int amount {0};
};

/** A poison (poison.2da). A blank cell, or a poison outside the table, is zero. */
struct PoisonData {
    int nameStrRef {0};
    int difficultyClass {0};
    int duration {0};
    int period {0};
    int hitPointDamage {0};
    int forcePointDamage {0};
    /** Damage to each ability, in Ability order. */
    std::array<int, 6> abilityDamage {};
};

/**
 * A TSL autobalance multiplier set (autobalance.2da): how a creature that is
 * not in the party scales with the player's level when it spawns. A blank
 * cell, or a set outside the table, is zero.
 */
struct AutoBalanceRow {
    float vitalityMultiplier {0.0f};
    float toHitMultiplier {0.0f};
    float armorClassMultiplier {0.0f};
    float damageMultiplier {0.0f};
    float savingThrowMultiplier {0.0f};
    int challengeRatingModifier {0};
    float levelMultiplier {0.0f};
};

/**
 * The tables the combat rules read on every attack, hit and effect, parsed
 * once at start-up. Every table is required.
 */
class ICombatTables {
public:
    virtual ~ICombatTables() = default;

    /** An item property cost table by its lower-case name. */
    virtual const CostTable &costTable(const std::string &resRef) const = 0;

    /** The item property cost table that iprp_costtable names at \p index. */
    virtual const CostTable &costTableAt(int index) const = 0;

    /** A damage bonus code; blank outside iprp_damagecost. */
    virtual DamageCost damageCost(int row) const = 0;

    /** A monster damage code; blank outside iprp_monstcost. */
    virtual MonsterDamageCost monsterDamageCost(int row) const = 0;

    /** The number of rows of iprp_damagetype. */
    virtual int damageTypeCount() const = 0;

    /** The number of rows of iprp_protection. */
    virtual int protectionCount() const = 0;

    /** The parameter table (param1resref) of an on-hit subtype. */
    virtual int onHitParameterTable(int subtype) const = 0;

    /** An on-hit chance and duration; zero outside iprp_onhitdur. */
    virtual OnHitDuration onHitDuration(int row) const = 0;

    /** The immunities whose gameeffects.2da column is set for the row. */
    virtual const std::vector<int> &gameEffectImmunities(int row) const = 0;

    /** The shield's row, or none when forceshields.2da has no such shield. */
    virtual std::optional<std::reference_wrapper<const ForceShieldRow>> findForceShield(int shield) const = 0;

    /** The shield's row; a shield outside the table reads as blank. */
    virtual ForceShieldRow forceShield(int shield) const = 0;

    /**
     * The multiplier of a Force power's cost at an alignment row
     * (forceadjust.2da), for a light side power when \p good, else a dark
     * side one. A blank cell leaves the cost unchanged.
     */
    virtual float forceCostMultiplier(int row, bool good) const = 0;

    /** How long, in milliseconds, a creature stays excited (excitedduration.2da). */
    virtual uint32_t excitedDuration(int row) const = 0;

    /** A poison by type; blank outside poison.2da. */
    virtual PoisonData poison(int row) const = 0;

    /** The script of a creature state (statescripts.2da). */
    virtual const std::string &stateScript(int row) const = 0;

    /** A TSL autobalance multiplier set; zero outside autobalance.2da. */
    virtual AutoBalanceRow autoBalance(int multiplierSet) const = 0;
};

class CombatTables : public ICombatTables {
public:
    /**
     * Parse every table. autobalance.2da is read in TSL alone, the only game
     * that has it.
     */
    void init(resource::ITwoDAs &twoDas, resource::GameID gameId);

    const CostTable &costTable(const std::string &resRef) const override;
    const CostTable &costTableAt(int index) const override;
    DamageCost damageCost(int row) const override;
    MonsterDamageCost monsterDamageCost(int row) const override;
    int damageTypeCount() const override;
    int protectionCount() const override;

    int onHitParameterTable(int subtype) const override;
    OnHitDuration onHitDuration(int row) const override;

    const std::vector<int> &gameEffectImmunities(int row) const override;
    std::optional<std::reference_wrapper<const ForceShieldRow>> findForceShield(int shield) const override;
    ForceShieldRow forceShield(int shield) const override;
    float forceCostMultiplier(int row, bool good) const override;
    uint32_t excitedDuration(int row) const override;
    PoisonData poison(int row) const override;
    const std::string &stateScript(int row) const override;
    AutoBalanceRow autoBalance(int multiplierSet) const override;

private:
    /** A light side and a dark side Force cost multiplier. */
    struct ForceCostMultipliers {
        float good {1.0f};
        float evil {1.0f};
    };

    /** By lower-case name. */
    std::unordered_map<std::string, CostTable> _costTables;
    /** The lower-case table names of iprp_costtable, by row. */
    std::vector<std::string> _costTableNames;
    std::vector<DamageCost> _damageCosts;
    std::vector<MonsterDamageCost> _monsterDamageCosts;
    int _damageTypeCount {0};
    int _protectionCount {0};
    std::vector<int> _onHitParameterTables;
    std::vector<OnHitDuration> _onHitDurations;
    std::vector<std::vector<int>> _gameEffectImmunities;
    /** By lower-case label; the first row of a label is the one found. */
    std::unordered_map<std::string, ForceShieldRow> _forceShields;
    std::vector<ForceCostMultipliers> _forceCostMultipliers;
    std::vector<uint32_t> _excitedDurations;
    std::vector<PoisonData> _poisons;
    std::vector<std::string> _stateScripts;
    /** Empty in KotOR, which has no autobalance. */
    std::vector<AutoBalanceRow> _autoBalance;
};

} // namespace game

} // namespace reone
