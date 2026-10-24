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
     * @return class saving throws at the class level, from the saving throw
     *         table's row level - 1; none for a level without a row
     */
    SavingThrows getSavingThrows(int level) const;

    /**
     * @return base attack bonus at the class level, from the attack table's
     *         row level - 1; 0 for a level without a row
     */
    int getAttackBonus(int level) const;

    /**
     * @return defense bonus at the class level, from the defense bonus
     *         table's row level; 0 for a level without a row
     */
    int getDefenseBonus(int level) const;

    ClassType type() const { return _type; }

    /**
     * @return whether this is a Jedi class: a Jedi base class or a TSL
     *         prestige class
     */
    bool isJedi() const {
        return (_type >= ClassType::JediGuardian && _type <= ClassType::JediSentinel) ||
               (_type >= ClassType::JediWeaponMaster && _type <= ClassType::SithAssassin);
    }

    const std::string &name() const { return _name; }
    const std::string &description() const { return _description; }
    /** The lower-case name's string; -1 when blank. */
    int lowerNameStrRef() const { return _lowerNameStrRef; }
    int hitdie() const { return _hitdie; }
    int forcedie() const { return _forcedie; }
    const CreatureAttributes &defaultAttributes() const { return _defaultAttributes; }
    int skillPointBase() const { return _skillPointBase; }
    /** @return the feats gained at the class level (featgain.2da row level - 1); 0 without a row */
    int getFeatGain(int level) const;
    /**
     * @return the bonus feats gained at the class level (featgain.2da bonus
     *         column, row level - 1); 0 without a row
     */
    int getBonusFeatGain(int level) const;
    /**
     * @return the powers a Jedi class gains at the class level
     *         (classpowergain.2da row level - 1, when cls_spgn_jedi.2da gives
     *         that row a power level); 0 without a row and for any other class
     */
    int getPowerGain(int level) const;
    int getFeatListValue(FeatType feat) const;

    /** @return whether a character of the class can take the feat as one of its normal feats */
    bool isNormalFeat(FeatType feat) const {
        int listValue = getFeatListValue(feat);
        return listValue == 0 || listValue == 1;
    }

    /** @return whether a character of the class can take the feat as one of its bonus feats */
    bool isBonusFeat(FeatType feat) const {
        int listValue = getFeatListValue(feat);
        return listValue == 1 || listValue == 2;
    }

    /** @return whether a character of the class can choose the feat, as a normal or a bonus feat */
    bool isFeatSelectable(FeatType feat) const {
        return isNormalFeat(feat) || isBonusFeat(feat);
    }

    /**
     * @return whether the class names a skills table: a character of a class
     *         without one can use untrained only the skills every class can use
     */
    bool hasSkills() const { return _hasSkills; }

    /**
     * @return the class level at which the class grants the feat, from 0 to
     *         255, or none when the feat is not on the class's granted list;
     *         level 0 is never reached
     */
    std::optional<int> getFeatGrantedLevel(FeatType feat) const;

    /**
     * @return whether a new TSL player character of this class starts with
     *         the feat
     */
    bool isPCGrantedFeat(FeatType feat) const { return _pcGrantedFeats.count(feat) > 0; }

private:
    ClassType _type;
    std::string _name;
    std::string _description;
    int _lowerNameStrRef {-1};
    int _hitdie {0};
    int _forcedie {0};
    CreatureAttributes _defaultAttributes;
    int _skillPointBase {0};
    std::unordered_set<SkillType> _classSkills;
    bool _hasSkills {false};
    std::unordered_map<FeatType, int> _featListValues;
    std::unordered_map<FeatType, int> _featGrantedLevels;
    std::unordered_set<FeatType> _pcGrantedFeats;
    /** Per-level tables, by row: row n holds class level n + 1. */
    std::vector<SavingThrows> _savingThrows;
    std::vector<int> _featGains;
    std::vector<int> _bonusFeatGains;
    std::vector<int> _powerGains;
    std::vector<int> _attackBonuses;
    std::vector<int> _defenseBonuses;

    // Services

    Classes &_classes;

    resource::IStrings &_strings;
    resource::ITwoDAs &_twoDas;

    // END Services

    void loadClassSkills(const std::string &skillsTable);
    void loadSavingThrows(const std::string &savingThrowTable);
    void loadAttackBonuses(const std::string &attackBonusTable);
    void loadDefenseBonuses(const std::string &defenseBonusColumn);
    void loadFeatListValues(const std::string &featsPrefix);
    void loadFeatGains(const std::string &featGainPrefix);
    void loadPowerGains(const std::string &powerGainPrefix);
};

} // namespace game

} // namespace reone
