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

using namespace reone::resource;

namespace reone {

namespace game {

static const char kSkillsTwoDAResRef[] = "skills";
static const char kFeatTwoDAResRef[] = "feat";
static const char kFeatGainTwoDAResRef[] = "featgain";
static const char kPowerGainTwoDAResRef[] = "classpowergain";

static bool hasColumn(const TwoDA &twoDa, const std::string &column) {
    return std::find(twoDa.columns().begin(), twoDa.columns().end(), column) != twoDa.columns().end();
}

static std::optional<int> getLevel(const TwoDA &twoDa, int row) {
    if (auto level = twoDa.getIntOpt(row, "label")) {
        return level;
    }
    return twoDa.getIntOpt(row, "level");
}

void CreatureClass::load(const TwoDA &twoDa, int row) {
    _name = _strings.getText(twoDa.getInt(row, "name"));
    _description = _strings.getText(twoDa.getInt(row, "description"));
    _hitdie = twoDa.getInt(row, "hitdie");
    _skillPointBase = twoDa.getInt(row, "skillpointbase");

    _defaultAttributes.addClassLevels(this, 1);
    _defaultAttributes.setAbilityScore(Ability::Strength, twoDa.getInt(row, "str"));
    _defaultAttributes.setAbilityScore(Ability::Dexterity, twoDa.getInt(row, "dex"));
    _defaultAttributes.setAbilityScore(Ability::Constitution, twoDa.getInt(row, "con"));
    _defaultAttributes.setAbilityScore(Ability::Intelligence, twoDa.getInt(row, "int"));
    _defaultAttributes.setAbilityScore(Ability::Wisdom, twoDa.getInt(row, "wis"));
    _defaultAttributes.setAbilityScore(Ability::Charisma, twoDa.getInt(row, "cha"));

    std::string skillsTable(boost::to_lower_copy(twoDa.getString(row, "skillstable")));
    loadClassSkills(skillsTable);

    std::string savingThrowTable(boost::to_lower_copy(twoDa.getString(row, "savingthrowtable")));
    loadSavingThrows(savingThrowTable);

    std::string attackBonusTable(boost::to_lower_copy(twoDa.getString(row, "attackbonustable")));
    loadAttackBonuses(attackBonusTable);

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
    std::shared_ptr<TwoDA> twoDa(_twoDas.get(savingThrowTable));
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        int level = twoDa->getInt(row, "level");

        SavingThrows throws;
        throws.fortitude = twoDa->getInt(row, "fortsave");
        throws.reflex = twoDa->getInt(row, "refsave");
        throws.will = twoDa->getInt(row, "willsave");

        _savingThrowsByLevel.insert(std::make_pair(level, std::move(throws)));
    }
}

void CreatureClass::loadAttackBonuses(const std::string &attackBonusTable) {
    std::shared_ptr<TwoDA> twoDa(_twoDas.get(attackBonusTable));
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        _attackBonuses.push_back(twoDa->getInt(row, "bab"));
    }
}

void CreatureClass::loadFeatListValues(const std::string &featsPrefix) {
    if (featsPrefix.empty()) {
        return;
    }

    std::shared_ptr<TwoDA> feats(_twoDas.get(kFeatTwoDAResRef));
    std::string listColumn(featsPrefix + "_list");
    if (!feats || !hasColumn(*feats, listColumn)) {
        return;
    }

    for (int row = 0; row < feats->getRowCount(); ++row) {
        int listValue = feats->getInt(row, listColumn, 4);
        _featListValues.insert(std::make_pair(static_cast<FeatType>(row), listValue));
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

    for (int row = 0; row < featGain->getRowCount(); ++row) {
        auto level = getLevel(*featGain, row);
        if (level) {
            _featGainsByLevel.insert(std::make_pair(*level, featGain->getInt(row, regularGainColumn, 0)));
        }
    }
}

void CreatureClass::loadPowerGains(const std::string &powerGainPrefix) {
    if (powerGainPrefix.empty()) {
        return;
    }

    std::shared_ptr<TwoDA> powerGain(_twoDas.get(kPowerGainTwoDAResRef));
    if (!powerGain || !hasColumn(*powerGain, powerGainPrefix)) {
        return;
    }

    for (int row = 0; row < powerGain->getRowCount(); ++row) {
        auto level = getLevel(*powerGain, row);
        if (level) {
            _powerGainsByLevel.insert(std::make_pair(*level, powerGain->getInt(row, powerGainPrefix, 0)));
        }
    }
}

bool CreatureClass::isClassSkill(SkillType skill) const {
    return _classSkills.count(skill) > 0;
}

const SavingThrows &CreatureClass::getSavingThrows(int level) const {
    auto maybeThrows = _savingThrowsByLevel.find(level);
    if (maybeThrows == _savingThrowsByLevel.end()) {
        throw std::logic_error("Saving throws not found for level " + std::to_string(level));
    }
    return maybeThrows->second;
}

int CreatureClass::getAttackBonus(int level) const {
    if (level < 1 || level > static_cast<int>(_attackBonuses.size())) {
        throw std::out_of_range(str(boost::format("Level out of range: %d/%d") % level % static_cast<int>(_attackBonuses.size())));
    }
    return _attackBonuses[level - 1];
}

int CreatureClass::getFeatGain(int level) const {
    auto maybeFeatGain = _featGainsByLevel.find(level);
    return maybeFeatGain != _featGainsByLevel.end() ? maybeFeatGain->second : 0;
}

int CreatureClass::getPowerGain(int level) const {
    auto maybePowerGain = _powerGainsByLevel.find(level);
    return maybePowerGain != _powerGainsByLevel.end() ? maybePowerGain->second : 0;
}

int CreatureClass::getFeatListValue(FeatType feat) const {
    auto maybeFeatListValue = _featListValues.find(feat);
    return maybeFeatListValue != _featListValues.end() ? maybeFeatListValue->second : 4;
}

} // namespace game

} // namespace reone
