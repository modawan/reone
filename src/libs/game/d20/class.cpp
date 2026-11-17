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

#include <algorithm>

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

template <class T>
static std::optional<int> findRecommendation(const std::unordered_map<T, int> &recommendations, T key) {
    auto maybeRecommendation = recommendations.find(key);
    return maybeRecommendation != recommendations.end() ? std::optional<int>(maybeRecommendation->second) : std::nullopt;
}

void CreatureClass::load(const TwoDA &twoDa, int row) {
    _name = _strings.getText(twoDa.getInt(row, "name"));
    _description = _strings.getText(twoDa.getInt(row, "description"));
    _lowerNameStrRef = twoDa.getInt(row, "lower", -1);
    _hitdie = twoDa.getInt(row, "hitdie");
    _forcedie = twoDa.getInt(row, "forcedie");
    _skillPointBase = twoDa.getInt(row, "skillpointbase");
    // The recommended level-up point goes to the ability the column names.
    _primaryAbility = readAbilityColumn(twoDa, row, "primaryabil");
    // Only a spell caster is recommended powers or gains them by itself.
    _spellCaster = twoDa.getInt(row, "spellcaster", 0) != 0;

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
        if (skills->getInt(row, skillsTable + "_class") != 0) {
            _classSkills.insert(static_cast<SkillType>(row));
        }
        auto recommendation = skills->getIntOpt(row, skillsTable + "_reco");
        if (recommendation) {
            _skillRecommendations.insert(std::make_pair(static_cast<SkillType>(row), *recommendation));
        }
    }
}

int CreatureClass::skillPointsForLevel(int intModifier, bool firstLevel, bool tsl) const {
    if (tsl) {
        int points = std::max(1, _skillPointBase + intModifier);
        return firstLevel ? 4 * points : points;
    }
    if (firstLevel) {
        return 4 * std::max(1, _skillPointBase / 2 + intModifier);
    }
    return std::max(1, (_skillPointBase + intModifier) / 2);
}

void CreatureClass::loadSavingThrows(const std::string &savingThrowTable) {
    auto twoDa = getRequiredTwoDA(_twoDas, savingThrowTable);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        // A saving throw bonus keeps its sign.
        SavingThrows throws;
        throws.fortitude = twoDa->getInt(row, "fortsave");
        throws.reflex = twoDa->getInt(row, "refsave");
        throws.will = twoDa->getInt(row, "willsave");

        _savingThrows.push_back(throws);
    }
}

void CreatureClass::loadAttackBonuses(const std::string &attackBonusTable) {
    auto twoDa = getRequiredTwoDA(_twoDas, attackBonusTable);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        // An attack bonus below zero reads as 0.
        _attackBonuses.push_back(std::max(0, twoDa->getInt(row, "bab")));
    }
}

void CreatureClass::loadDefenseBonuses(const std::string &defenseBonusColumn) {
    auto twoDa = getRequiredTwoDA(_twoDas, kDefenseBonusTwoDAResRef);
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        // A defense bonus keeps its sign.
        _defenseBonuses.push_back(twoDa->getInt(row, defenseBonusColumn));
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
    auto recommendationColumn = featsPrefix + "_recom";
    for (int row = 0; row < feats->getRowCount(); ++row) {
        auto feat = static_cast<FeatType>(row);
        // A blank cell, or a column the table lacks, reads as 0. A list value
        // below zero is off every list.
        int listValue = feats->getInt(row, listColumn);
        if (listValue < 0) {
            listValue = kOffListFeatListValue;
        }
        _featListValues.insert(std::make_pair(feat, listValue));
        auto recommendation = feats->getIntOpt(row, recommendationColumn);
        if (recommendation) {
            _featRecommendations.insert(std::make_pair(feat, *recommendation));
        }

        // List value 3, or one above 4, marks a feat the class grants at the
        // level in its granted column; a level below 1 is never reached. A new
        // TSL player character also starts with the selectable feats marked 1
        // in the player-character column.
        if (listValue == kGrantedFeatListValue || listValue > kOffListFeatListValue) {
            _featGrantedLevels.insert(std::make_pair(feat, feats->getInt(row, grantedColumn)));
        } else if (listValue < kGrantedFeatListValue && feats->getInt(row, pcGrantedColumn) == 1) {
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

    // A blank cell, a missing bonus column, or a gain below zero reads 0.
    std::string bonusGainColumn(featGainPrefix + "_bon");
    for (int row = 0; row < featGain->getRowCount(); ++row) {
        _featGains.push_back(std::max(0, featGain->getInt(row, regularGainColumn, 0)));
        _bonusFeatGains.push_back(std::max(0, featGain->getInt(row, bonusGainColumn, 0)));
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
        // least one power level. A blank gain, or one below zero, is none.
        const bool hasPowerLevels = powerLevels->getInt(row, "numspelllevels", 0) > 0;
        const int gain = powerGain->getInt(row, powerGainPrefix, 0);
        _powerGains.push_back(hasPowerLevels ? std::max(0, gain) : 0);
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
    return maybeFeatListValue != _featListValues.end() ? maybeFeatListValue->second : kOffListFeatListValue;
}

std::optional<int> CreatureClass::getSkillRecommendation(SkillType skill) const {
    return findRecommendation(_skillRecommendations, skill);
}

std::optional<int> CreatureClass::getFeatRecommendation(FeatType feat) const {
    return findRecommendation(_featRecommendations, feat);
}

std::optional<int> CreatureClass::getFeatGrantedLevel(FeatType feat) const {
    auto maybeLevel = _featGrantedLevels.find(feat);
    return maybeLevel != _featGrantedLevels.end() ? std::optional<int>(maybeLevel->second) : std::nullopt;
}

} // namespace game

} // namespace reone
