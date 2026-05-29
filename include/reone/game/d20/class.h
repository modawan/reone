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

#pragma once

#include "reone/resource/format/2dareader.h"

#include "../types.h"

#include "attributes.h"
#include "savingthrows.h"

namespace reone {

namespace resource {

class IStrings;
class ITwoDAs;

} // namespace resource

namespace game {

class Classes;

class CreatureClass {
public:
    CreatureClass(
        ClassType type,
        Classes &classes,
        resource::IStrings &strings,
        resource::ITwoDAs &twoDas) :
        _type(type),
        _classes(classes),
        _strings(strings),
        _twoDas(twoDas) {
    }

    void load(const resource::TwoDA &twoDa, int row);

    bool isClassSkill(SkillType skill) const;

    /**
     * @return class saving throws at the specified creature level
     */
    const SavingThrows &getSavingThrows(int level) const;

    /**
     * @return base attack bonus at the specified creature level
     */
    int getAttackBonus(int level) const;

    ClassType type() const { return _type; }
    const std::string &name() const { return _name; }
    const std::string &description() const { return _description; }
    int hitdie() const { return _hitdie; }
    const CreatureAttributes &defaultAttributes() const { return _defaultAttributes; }
    int skillPointBase() const { return _skillPointBase; }
    int getFeatGain(int level) const;
    int getPowerGain(int level) const;
    int getFeatListValue(FeatType feat) const;
    bool isFeatSelectable(FeatType feat) const {
        int listValue = getFeatListValue(feat);
        return listValue == 0 || listValue == 1;
    }

private:
    ClassType _type;
    std::string _name;
    std::string _description;
    int _hitdie {0};
    CreatureAttributes _defaultAttributes;
    int _skillPointBase {0};
    std::unordered_set<SkillType> _classSkills;
    std::unordered_map<FeatType, int> _featListValues;
    std::unordered_map<int, SavingThrows> _savingThrowsByLevel;
    std::unordered_map<int, int> _featGainsByLevel;
    std::unordered_map<int, int> _powerGainsByLevel;
    std::vector<int> _attackBonuses;

    // Services

    Classes &_classes;

    resource::IStrings &_strings;
    resource::ITwoDAs &_twoDas;

    // END Services

    void loadClassSkills(const std::string &skillsTable);
    void loadSavingThrows(const std::string &savingThrowTable);
    void loadAttackBonuses(const std::string &attackBonusTable);
    void loadFeatListValues(const std::string &featsPrefix);
    void loadFeatGains(const std::string &featGainPrefix);
    void loadPowerGains(const std::string &powerGainPrefix);
};

} // namespace game

} // namespace reone
