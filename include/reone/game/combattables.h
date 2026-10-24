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

/** A body bag (bodybag.2da). A blank cell, or a bag outside the table, has no value. */
struct BodyBagRow {
    std::optional<int> appearance;
    std::optional<int> nameStrRef;
    /** The bag is the creature's own corpse. */
    bool corpse {false};
};

/**
 * Vitality and Force regeneration (regeneration.2da). KotOR reads its in and
 * out of combat rows; TSL reads its values by label. A blank cell, or a
 * missing label, is zero.
 */
struct RegenerationRates {
    /** The row KotOR reads in combat. */
    static constexpr int kInCombatRow = 0;
    /** The row KotOR reads out of combat. */
    static constexpr int kOutOfCombatRow = 1;

    /** KotOR: percent of the maximum vitality per second, by row. */
    std::array<float, 2> healthRegen {};
    /** KotOR: percent of the maximum Force points per second, by row. */
    std::array<float, 2> forceRegen {};
    float timePerHitPoint {0.0f};
    float inCombatHitPointBase {0.0f};
    float outOfCombatHitPointBase {0.0f};
    float constitutionBonus {0.0f};
    float skillRankBonus {0.0f};
    float forcePointTime {0.0f};
    float inCombatForcePointBase {0.0f};
    float outOfCombatForcePointBase {0.0f};
    float wisdomBonus {0.0f};
};

/** An effect icon (effecticon.2da). A blank cell, or an icon outside the table, has no value. */
struct EffectIconRow {
    /** The row names a picture. */
    bool hasIcon {false};
    std::optional<int> priority;
    /** Whether the effect helps the creature. */
    std::optional<bool> good;
    std::optional<int> nameStrRef;
};

/**
 * The tables the combat rules read on every attack, hit, effect, death and
 * spawn, parsed once at start-up: item properties and equipment limits,
 * effects and creature states, kill experience and challenge, regeneration
 * and the sounds of weapons. Every table is required; one that only TSL has
 * is read in TSL alone.
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

    /** The effect types that survive the death of a creature (removefxondeath.2da). */
    virtual const std::vector<int> &deathKeptEffectTypes() const = 0;

    /** The experience a level row needs (exptable.2da); fails outside the table. */
    virtual uint32_t experienceThreshold(int levelRow) const = 0;

    /**
     * A kill's experience by the player's level row and the victim's challenge
     * column (xptable.2da); zero outside the table or at a blank cell.
     */
    virtual float killExperience(int row, int column) const = 0;

    /**
     * A weapon sound (weaponsounds.2da) in lower case; empty when the cell is
     * blank, the row is outside the table or there is no such column.
     */
    virtual const std::string &weaponSound(int row, const std::string &column) const = 0;

    /** A body bag; blank outside bodybag.2da. */
    virtual BodyBagRow bodyBag(int row) const = 0;

    /** The least challenge rating of a fractionalcr.2da row; fails outside the table. */
    virtual float fractionalChallengeMinimum(int row) const = 0;

    /** An encounter difficulty (encdifficulty.2da); none outside the table or at a blank cell. */
    virtual std::optional<int> encounterDifficulty(int row) const = 0;

    /** The regeneration rates (regeneration.2da). */
    virtual const RegenerationRates &regeneration() const = 0;

    /** An effect icon; blank outside effecticon.2da. */
    virtual EffectIconRow effectIcon(int row) const = 0;

    /** The most a single item may cost for each level row (itemvalue.2da). */
    virtual const std::vector<int> &itemValueLimits() const = 0;

    /**
     * The tag a TSL player character limit expects (iprp_pc.2da); empty
     * outside the table.
     */
    virtual const std::string &playerCharacterLimitTag(int subtype) const = 0;
};

class CombatTables : public ICombatTables {
public:
    /**
     * Parse every table. autobalance.2da and iprp_pc.2da are read in TSL
     * alone, the only game that has them.
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

    const std::vector<int> &deathKeptEffectTypes() const override;
    uint32_t experienceThreshold(int levelRow) const override;
    float killExperience(int row, int column) const override;
    const std::string &weaponSound(int row, const std::string &column) const override;
    BodyBagRow bodyBag(int row) const override;
    float fractionalChallengeMinimum(int row) const override;
    std::optional<int> encounterDifficulty(int row) const override;
    const RegenerationRates &regeneration() const override;
    EffectIconRow effectIcon(int row) const override;
    const std::vector<int> &itemValueLimits() const override;
    const std::string &playerCharacterLimitTag(int subtype) const override;

private:
    /** A light side and a dark side Force cost multiplier. */
    struct ForceCostMultipliers {
        /** The multiplier of a light side power. */
        float good {1.0f};
        /** The multiplier of a dark side power. */
        float evil {1.0f};
    };

    /** By lower-case name. */
    std::unordered_map<std::string, CostTable> _costTables;
    /** The lower-case table names of iprp_costtable, by row. */
    std::vector<std::string> _costTableNames;
    /** iprp_damagecost.2da, by row. */
    std::vector<DamageCost> _damageCosts;
    /** iprp_monstcost.2da, by row. */
    std::vector<MonsterDamageCost> _monsterDamageCosts;
    /** The rows of iprp_damagetype.2da. */
    int _damageTypeCount {0};
    /** The rows of iprp_protection.2da. */
    int _protectionCount {0};
    /** iprp_onhit.2da's parameter table, by row. */
    std::vector<int> _onHitParameterTables;
    /** iprp_onhitdur.2da, by row. */
    std::vector<OnHitDuration> _onHitDurations;
    /** gameeffects.2da's immunity types, by row. */
    std::vector<std::vector<int>> _gameEffectImmunities;
    /** By lower-case label; the first row of a label is the one found. */
    std::unordered_map<std::string, ForceShieldRow> _forceShields;
    /** forceadjust.2da, by row. */
    std::vector<ForceCostMultipliers> _forceCostMultipliers;
    /** excitedduration.2da, by row. */
    std::vector<uint32_t> _excitedDurations;
    /** poison.2da, by row. */
    std::vector<PoisonData> _poisons;
    /** statescripts.2da, by row. */
    std::vector<std::string> _stateScripts;
    /** Empty in KotOR, which has no autobalance. */
    std::vector<AutoBalanceRow> _autoBalance;
    /** The effect types removefxondeath.2da keeps through death. */
    std::vector<int> _deathKeptEffectTypes;
    /** exptable.2da's experience by level row. */
    std::vector<uint32_t> _experienceThresholds;
    /** By row, then by column; a blank cell is zero. */
    std::vector<std::vector<float>> _killExperience;
    /** By row, then by column; blank cells are left out. */
    std::vector<std::unordered_map<std::string, std::string>> _weaponSounds;
    /** bodybag.2da, by row. */
    std::vector<BodyBagRow> _bodyBags;
    /** fractionalcr.2da's minimum, by row. */
    std::vector<float> _fractionalChallengeMinimums;
    /** encdifficulty.2da's value by row; none where blank. */
    std::vector<std::optional<int>> _encounterDifficulties;
    /** regeneration.2da's rates. */
    RegenerationRates _regeneration;
    /** effecticon.2da, by row. */
    std::vector<EffectIconRow> _effectIcons;
    /** itemvalue.2da's largest single item value, by level row. */
    std::vector<int> _itemValueLimits;
    /** Empty in KotOR, which has no player character limits. */
    std::vector<std::string> _playerCharacterLimitTags;
};

} // namespace game

} // namespace reone
