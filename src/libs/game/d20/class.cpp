/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/strings.h"

#include "reone/game/d20/classes.h"
#include "reone/game/twodautil.h"

using namespace reone::resource;

namespace reone {

namespace game {

// A feat list value below this one marks a feat a character of the class can
// choose; this one marks a feat the class grants at the level in its granted
// column.
static constexpr int kGrantedFeatListValue = 3;
// A feat list value that marks a feat off the class's list. A list value
// above it marks a granted feat, as kGrantedFeatListValue does.
static constexpr int kOffListFeatListValue = 4;

static const char kSkillsTwoDAResRef[] = "skills";
static const char kDefenseBonusTwoDAResRef[] = "acbonus";
static const char kFeatTwoDAResRef[] = "feat";
static const char kFeatGainTwoDAResRef[] = "featgain";
static const char kPowerGainTwoDAResRef[] = "classpowergain";
static const char kPowerLevelsTwoDAResRef[] = "cls_spgn_jedi";

static bool hasColumn(const TwoDA &twoDa, const std::string &column) {
    return twoDa.getColumnIndex(column) != -1;
}

void CreatureClass::load(const TwoDA &twoDa, int row) {
    _name = _strings.getText(twoDa.getInt(row, "name"));
    _description = _strings.getText(twoDa.getInt(row, "description"));
    _lowerNameStrRef = twoDa.getInt(row, "lower", -1);
    _hitdie = twoDa.getInt(row, "hitdie");
    _forcedie = twoDa.getInt(row, "forcedie");
    _skillPointBase = twoDa.getInt(row, "skillpointbase");

    _defaultAttributes.addClassLevels(this, 1);
    _defaultAttributes.setAbilityScore(Ability::Strength, twoDa.getInt(row, "str"));
    _defaultAttributes.setAbilityScore(Ability::Dexterity, twoDa.getInt(row, "dex"));
    _defaultAttributes.setAbilityScore(Ability::Constitution, twoDa.getInt(row, "con"));
    _defaultAttributes.setAbilityScore(Ability::Intelligence, twoDa.getInt(row, "int"));
    _defaultAttributes.setAbilityScore(Ability::Wisdom, twoDa.getInt(row, "wis"));
    _defaultAttributes.setAbilityScore(Ability::Charisma, twoDa.getInt(row, "cha"));

    std::string skillsTable(boost::to_lower_copy(twoDa.getString(row, "skillstable")));
    _hasSkills = !skillsTable.empty();
    loadClassSkills(skillsTable);

    std::string savingThrowTable(boost::to_lower_copy(
        twoDa.getString(row, "savingthrowtable")));
    loadSavingThrows(savingThrowTable);

    std::string attackBonusTable(boost::to_lower_copy(
        twoDa.getString(row, "attackbonustable")));
    loadAttackBonuses(attackBonusTable);

    std::string defenseBonusColumn(boost::to_lower_copy(
        twoDa.getString(row, "armorclasscolumn")));
    if (!defenseBonusColumn.empty()) {
        loadDefenseBonuses(defenseBonusColumn);
    }

    std::string featsPrefix(boost::to_lower_copy(twoDa.getString(row, "featstable")));
    loadFeatListValues(featsPrefix);

    std::string featGainPrefix(boost::to_lower_copy(twoDa.getString(row, "featgain")));
    loadFeatGains(featGainPrefix);

    std::string powerGainPrefix(boost::to_lower_copy(twoDa.getString(row, "spellgaintable")));
    loadPowerGains(powerGainPrefix);
}

void CreatureClass::loadClassSkills(const std::string &skillsTable) {
    std::shared_ptr<TwoDA> skills(_twoDas.get(kSkillsTwoDAResRef));
    for (int row = 0; row < skills->getRowCount(); ++row) {
        if (skills->getInt(row, skillsTable + "_class") == 1) {
            _classSkills.insert(static_cast<SkillType>(row));
        }
    }
}

void CreatureClass::loadSavingThrows(const std::string &savingThrowTable) {
    auto twoDa = getRequiredTwoDA(_twoDas, savingThrowTable);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        // Saving throw bonuses are signed bytes.
        SavingThrows throws;
        throws.fortitude = static_cast<int8_t>(twoDa->getInt(row, "fortsave"));
        throws.reflex = static_cast<int8_t>(twoDa->getInt(row, "refsave"));
        throws.will = static_cast<int8_t>(twoDa->getInt(row, "willsave"));

        _savingThrows.push_back(throws);
    }
}

void CreatureClass::loadAttackBonuses(const std::string &attackBonusTable) {
    auto twoDa = getRequiredTwoDA(_twoDas, attackBonusTable);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        // Attack bonuses are unsigned bytes.
        _attackBonuses.push_back(twoDa->getInt(row, "bab") & 0xff);
    }
}

void CreatureClass::loadDefenseBonuses(const std::string &defenseBonusColumn) {
    auto twoDa = getRequiredTwoDA(_twoDas, kDefenseBonusTwoDAResRef);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        // Defense bonuses are signed bytes.
        _defenseBonuses.push_back(static_cast<int8_t>(twoDa->getInt(row, defenseBonusColumn)));
    }
}

void CreatureClass::loadFeatListValues(const std::string &featsPrefix) {
    if (featsPrefix.empty()) {
        return;
    }

    std::shared_ptr<TwoDA> feats(_twoDas.get(kFeatTwoDAResRef));
    if (!feats) {
        return;
    }

    std::string listColumn(featsPrefix + "_list");
    auto grantedColumn = featsPrefix + "_granted";
    auto pcGrantedColumn = featsPrefix + "_pc_granted";
    for (int row = 0; row < feats->getRowCount(); ++row) {
        auto feat = static_cast<FeatType>(row);
        // The list, granted and player-character cells are bytes; a blank
        // cell, or a column the table lacks, reads as 0.
        int listValue = feats->getInt(row, listColumn) & 0xff;
        _featListValues.insert(std::make_pair(feat, listValue));

        // List value 3, or one above 4, marks a feat the class grants at the
        // level in its granted column. A new TSL player character also starts
        // with the selectable feats marked in the player-character column.
        if (listValue == kGrantedFeatListValue || listValue > kOffListFeatListValue) {
            _featGrantedLevels.insert(std::make_pair(feat, feats->getInt(row, grantedColumn) & 0xff));
        } else if (listValue < kGrantedFeatListValue && (feats->getInt(row, pcGrantedColumn) & 0xff) == 1) {
            _pcGrantedFeats.insert(feat);
        }
    }
}

void CreatureClass::loadFeatGains(const std::string &featGainPrefix) {
    if (featGainPrefix.empty()) {
        return;
    }

    std::shared_ptr<TwoDA> featGain(_twoDas.get(kFeatGainTwoDAResRef));
    std::string regularGainColumn(featGainPrefix + "_reg");
    if (!featGain || !hasColumn(*featGain, regularGainColumn)) {
        return;
    }

    // Both gains are bytes; a blank cell, or a missing bonus column, reads 0.
    std::string bonusGainColumn(featGainPrefix + "_bon");
    for (int row = 0; row < featGain->getRowCount(); ++row) {
        _featGains.push_back(featGain->getInt(row, regularGainColumn, 0) & 0xff);
        _bonusFeatGains.push_back(featGain->getInt(row, bonusGainColumn, 0) & 0xff);
    }
}

void CreatureClass::loadPowerGains(const std::string &powerGainPrefix) {
    if (powerGainPrefix.empty()) {
        return;
    }

    std::shared_ptr<TwoDA> powerLevels(_twoDas.get(kPowerLevelsTwoDAResRef));
    std::shared_ptr<TwoDA> powerGain(_twoDas.get(kPowerGainTwoDAResRef));
    if (!powerLevels || !powerGain || !hasColumn(*powerGain, powerGainPrefix)) {
        return;
    }

    for (int row = 0; row < powerGain->getRowCount(); ++row) {
        // A level gains powers only when the power level table gives it at
        // least one power level (an unsigned byte). A power gain is an
        // unsigned byte; 255 means none.
        const bool hasPowerLevels = (powerLevels->getInt(row, "numspelllevels", 0) & 0xff) > 0;
        const int gain = powerGain->getInt(row, powerGainPrefix, 0) & 0xff;
        _powerGains.push_back(hasPowerLevels && gain != 0xff ? gain : 0);
    }
}

bool CreatureClass::isClassSkill(SkillType skill) const {
    return _classSkills.count(skill) > 0;
}

// The per-level tables give level n's value in row n - 1, and nothing for a
// level without a row.
template <class T>
static T levelRow(const std::vector<T> &rows, int level) {
    if (level < 1 || level > static_cast<int>(rows.size())) {
        return T {};
    }
    return rows[level - 1];
}

SavingThrows CreatureClass::getSavingThrows(int level) const {
    return levelRow(_savingThrows, level);
}

int CreatureClass::getAttackBonus(int level) const {
    return levelRow(_attackBonuses, level);
}

int CreatureClass::getDefenseBonus(int level) const {
    assert(level >= 0);
    assert(!_defenseBonuses.empty());

    if (level >= static_cast<int>(_defenseBonuses.size())) {
        return 0;
    }
    return _defenseBonuses[level];
}

int CreatureClass::getFeatGain(int level) const {
    return levelRow(_featGains, level);
}

int CreatureClass::getBonusFeatGain(int level) const {
    return levelRow(_bonusFeatGains, level);
}

int CreatureClass::getPowerGain(int level) const {
    if (!isJedi()) {
        return 0;
    }
    return levelRow(_powerGains, level);
}

int CreatureClass::getFeatListValue(FeatType feat) const {
    auto maybeFeatListValue = _featListValues.find(feat);
    return maybeFeatListValue != _featListValues.end() ? maybeFeatListValue->second : 4;
}

std::optional<int> CreatureClass::getFeatGrantedLevel(FeatType feat) const {
    auto maybeLevel = _featGrantedLevels.find(feat);
    return maybeLevel != _featGrantedLevels.end() ? std::optional<int>(maybeLevel->second) : std::nullopt;
}

} // namespace game

} // namespace reone
