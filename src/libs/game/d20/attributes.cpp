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

#include "reone/game/d20/attributes.h"

#include <algorithm>

#include "reone/game/d20/class.h"

namespace reone {

namespace game {

static constexpr int kDefaultAbilityScore = 8;
static constexpr int kDefaultSkillRank = 0;

void CreatureAttributes::addFeat(FeatType type) {
    if (_feats.insert(type).second) {
        _featOrder.push_back(type);
    }
}

void CreatureAttributes::removeFeat(FeatType type) {
    _feats.erase(type);
    _featOrder.erase(std::remove(_featOrder.begin(), _featOrder.end(), type), _featOrder.end());
    _spentFeatUses.erase(type);
}

int CreatureAttributes::spentFeatUses(FeatType type) const {
    const auto found = _spentFeatUses.find(type);
    return found == _spentFeatUses.end() ? 0 : found->second;
}

void CreatureAttributes::spendFeatUse(FeatType type) {
    ++_spentFeatUses[type];
}

void CreatureAttributes::addSpell(SpellType type) {
    addSpell(type, getEffectiveClass());
}

void CreatureAttributes::addSpell(SpellType type, ClassType owner) {
    auto &known = _classSpells[owner];
    if (std::find(known.begin(), known.end(), type) == known.end()) {
        known.push_back(type);
    }
    _spells.insert(type);
}

void CreatureAttributes::removeSpell(SpellType type) {
    for (auto &[owner, known] : _classSpells) {
        known.erase(std::remove(known.begin(), known.end(), type), known.end());
    }
    _spells.erase(type);
}

const std::vector<SpellType> &CreatureAttributes::spellsForClass(ClassType owner) const {
    static const std::vector<SpellType> empty;
    const auto found = _classSpells.find(owner);
    return found == _classSpells.end() ? empty : found->second;
}

int CreatureAttributes::getDefense() const {
    return 10 +
           getAggregateDefenseBonus() +
           getAbilityModifier(Ability::Dexterity);
}

int CreatureAttributes::getAbilityScore(Ability ability) const {
    auto it = _abilityScores.find(ability);
    return it != _abilityScores.end() ? it->second : kDefaultAbilityScore;
}

int CreatureAttributes::getAbilityModifier(Ability ability) const {
    return getAbilityModifierFromScore(getAbilityScore(ability));
}

int CreatureAttributes::strength() const {
    return getAbilityScore(Ability::Strength);
}

int CreatureAttributes::dexterity() const {
    return getAbilityScore(Ability::Dexterity);
}

int CreatureAttributes::constitution() const {
    return getAbilityScore(Ability::Constitution);
}

int CreatureAttributes::intelligence() const {
    return getAbilityScore(Ability::Intelligence);
}

int CreatureAttributes::wisdom() const {
    return getAbilityScore(Ability::Wisdom);
}

int CreatureAttributes::charisma() const {
    return getAbilityScore(Ability::Charisma);
}

void CreatureAttributes::setAbilityScore(Ability ability, int score) {
    _abilityScores[ability] = score;
}

void CreatureAttributes::addClassLevels(CreatureClass *clazz, int levels) {
    for (int i = 0; i < static_cast<int>(_classLevels.size()); ++i) {
        if (_classLevels[i].first == clazz) {
            _classLevels[i].second += levels;
            return;
        }
    }
    _classLevels.push_back(std::make_pair(clazz, levels));
}

ClassType CreatureAttributes::getClassByPosition(int position) const {
    return position > 0 && (position - 1) < static_cast<int>(_classLevels.size()) ? _classLevels[static_cast<size_t>(position) - 1].first->type() : ClassType::Invalid;
}

ClassType CreatureAttributes::getEffectiveClass() const {
    return _classLevels.empty() ? ClassType::Invalid : _classLevels.back().first->type();
}

int CreatureAttributes::getLevelByPosition(int position) const {
    return position > 0 && (position - 1) < static_cast<int>(_classLevels.size()) ? _classLevels[static_cast<size_t>(position) - 1].second : 0;
}

int CreatureAttributes::getClassLevel(ClassType clazz) const {
    auto maybeClassLevel = std::find_if(
        _classLevels.begin(), _classLevels.end(),
        [&clazz](auto &classLevel) { return classLevel.first->type() == clazz; });

    return maybeClassLevel != _classLevels.end() ? maybeClassLevel->second : 0;
}

int CreatureAttributes::getAggregateHitDie() const {
    int result = 0;
    for (auto &pair : _classLevels) {
        result += pair.second * pair.first->hitdie();
    }
    return result;
}

// War Veteran's vitality, which counts once for the level history and for
// every level otherwise.
static constexpr int kWarVeteranHitPoints = 25;

static int toughnessHitPointsPerLevel(const std::function<bool(FeatType)> &hasVitalityFeat) {
    if (hasVitalityFeat(FeatType::MasterToughness)) {
        return 2;
    }
    return hasVitalityFeat(FeatType::Toughness) ? 1 : 0;
}

int CreatureAttributes::getLevelHistoryMaxHitPoints(const std::function<int(int)> &levelHitDie,
                                                    int constitutionModifier,
                                                    const std::function<bool(FeatType)> &hasVitalityFeat) const {
    const int level = getAggregateLevel();
    const int toughness = toughnessHitPointsPerLevel(hasVitalityFeat);
    int result = 0;
    for (int i = 0; i < level; ++i) {
        result += std::max(1, levelHitDie(i) + constitutionModifier) + toughness;
    }
    return result + (hasVitalityFeat(FeatType::WarVeteran) ? kWarVeteranHitPoints : 0);
}

int CreatureAttributes::getMaxHitPoints(int baseHitPoints, int constitutionModifier,
                                        const std::function<bool(FeatType)> &hasVitalityFeat) const {
    const int level = getAggregateLevel();
    if (level <= 0) {
        return std::max(0, baseHitPoints);
    }

    int featHitPointsPerLevel = toughnessHitPointsPerLevel(hasVitalityFeat);
    const int maximum = std::max(level, baseHitPoints + constitutionModifier * level);

    // War Veteran counts for every level.
    if (hasVitalityFeat(FeatType::WarVeteran)) {
        featHitPointsPerLevel += kWarVeteranHitPoints;
    }
    if (hasVitalityFeat(FeatType::MasterWookieEndurance)) {
        featHitPointsPerLevel += 4;
    } else if (hasVitalityFeat(FeatType::ImprovedWookieEndurance)) {
        featHitPointsPerLevel += 3;
    } else if (hasVitalityFeat(FeatType::WookieEndurance)) {
        featHitPointsPerLevel += 2;
    }
    return maximum + featHitPointsPerLevel * level;
}

int CreatureAttributes::getAggregateLevel() const {
    int result = 0;
    for (auto &pair : _classLevels) {
        result += pair.second;
    }
    return result;
}

int CreatureAttributes::getAggregateAttackBonus() const {
    int result = 0;
    for (auto &pair : _classLevels) {
        result += pair.first->getAttackBonus(pair.second);
    }
    return result;
}

int CreatureAttributes::getAggregateDefenseBonus() const {
    int result = 0;
    for (auto &pair : _classLevels) {
        result += pair.first->getDefenseBonus(pair.second);
    }
    return result;
}

SavingThrows CreatureAttributes::getAggregateSavingThrows() const {
    SavingThrows result;
    result.fortitude = 0;
    result.reflex = 0;
    result.will = 0;
    for (auto &pair : _classLevels) {
        auto classThrows = pair.first->getSavingThrows(pair.second);
        result.fortitude += classThrows.fortitude;
        result.reflex += classThrows.reflex;
        result.will += classThrows.will;
    }
    return result;
}

bool CreatureAttributes::hasSkill(SkillType skill) const {
    return getSkillRank(skill) > 0;
}

int CreatureAttributes::getSkillRank(SkillType skill) const {
    auto it = _skillRanks.find(skill);
    return it != _skillRanks.end() ? it->second : kDefaultSkillRank;
}

int CreatureAttributes::computerUse() const {
    return getSkillRank(SkillType::ComputerUse);
}

int CreatureAttributes::demolitions() const {
    return getSkillRank(SkillType::Demolitions);
}

int CreatureAttributes::stealth() const {
    return getSkillRank(SkillType::Stealth);
}

int CreatureAttributes::awareness() const {
    return getSkillRank(SkillType::Awareness);
}

int CreatureAttributes::persuade() const {
    return getSkillRank(SkillType::Persuade);
}

int CreatureAttributes::repair() const {
    return getSkillRank(SkillType::Repair);
}

int CreatureAttributes::security() const {
    return getSkillRank(SkillType::Security);
}

int CreatureAttributes::treatInjury() const {
    return getSkillRank(SkillType::TreatInjury);
}

void CreatureAttributes::setSkillRank(SkillType skill, int rank) {
    _skillRanks[skill] = rank;
}

} // namespace game

} // namespace reone
