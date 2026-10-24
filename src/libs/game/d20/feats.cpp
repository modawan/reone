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

#include "reone/game/d20/feats.h"

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/strings.h"

#include "reone/game/d20/attributes.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/races.h"
#include "reone/game/d20/skills.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static bool isValidFeat(FeatType type) {
    return type != FeatType::Invalid;
}

// Bao-Dur and Kreia: the companions whose columns mark class feats they are
// never granted; Kreia gains no feats of her own at character levels.
static constexpr char kBaoDurColumn[] = "baodur";
static constexpr char kKreiaColumn[] = "kreia";

// The Unarmed Specialist feats are eight ranks in a row.
static constexpr int kUnarmedSpecialistRanks = 8;

// The TSL companions with a level column of their own in the feat table, in
// column order.
static const std::array<const char *, 7> kCompanionColumns {
    "handmaiden", "baodur", "hanharr", "hk47", "g0t0", "atton", "kreia"};
// A companion column marks a class feat the companion is never granted with
// this value.
static constexpr int kNeverGrantedCompanionLevel = 255;

// The alternative prerequisite columns are this prefix and an index.
static constexpr char kAlternativePrerequisitePrefix[] = "orreqfeat";
static constexpr int kAlternativePrerequisiteCount = 5;

// The minimum ability score columns.
static const std::array<std::pair<const char *, Ability>, 4> kMinAbilityColumns {{{"minstr", Ability::Strength},
                                                                                  {"mindex", Ability::Dexterity},
                                                                                  {"minint", Ability::Intelligence},
                                                                                  {"minwis", Ability::Wisdom}}};

// The required skill word that names no skill.
static constexpr int kNoRequiredSkill = 0xffff;

// TSL's feats that no character chooses.
static bool isUnchoosableFeat(FeatType type) {
    return type == FeatType::ComplexUnarmedAnims || type == FeatType::MineImmunity;
}

// The Unarmed Specialist feats follow the character's levels in all its Jedi
// classes rather than its level in the class that grants them.
static bool isUnarmedSpecialistFeat(FeatType type) {
    int value = static_cast<int>(type);
    int first = static_cast<int>(FeatType::UnarmedDamageEightRanks1);
    return value >= first && value < first + kUnarmedSpecialistRanks;
}

static bool hasPrerequisite(const Feat &feat, FeatType prerequisite) {
    int word = static_cast<int>(prerequisite);
    return feat.preReqFeat1 == word || feat.preReqFeat2 == word;
}

// A feat is acquired when it is held, given by an effect or chosen.
static bool isAcquired(
    int word,
    const CreatureAttributes &attributes,
    const std::set<FeatType> &effectFeats,
    const std::set<FeatType> &chosen) {

    auto type = static_cast<FeatType>(word);
    return attributes.hasFeat(type) || effectFeats.count(type) > 0 || chosen.count(type) > 0;
}

// The prerequisites must be acquired: each prerequisite word other than
// kNoPrerequisite, and one of the alternative prerequisites when there are any.
static bool meetsPrerequisites(
    const Feat &feat,
    const CreatureAttributes &attributes,
    const std::set<FeatType> &effectFeats,
    const std::set<FeatType> &chosen) {

    auto acquired = [&](int word) { return isAcquired(word, attributes, effectFeats, chosen); };
    for (int prerequisite : {feat.preReqFeat1, feat.preReqFeat2}) {
        if (prerequisite != Feat::kNoPrerequisite && !acquired(prerequisite)) {
            return false;
        }
    }
    return feat.alternativePrerequisites.empty() ||
           std::any_of(feat.alternativePrerequisites.begin(), feat.alternativePrerequisites.end(), acquired);
}

static std::vector<FeatType> getValidPrerequisites(
    const Feat &feat,
    const std::unordered_map<FeatType, std::shared_ptr<Feat>> &feats) {

    std::vector<FeatType> result;
    for (int prerequisite : {feat.preReqFeat1, feat.preReqFeat2}) {
        auto type = static_cast<FeatType>(prerequisite);
        if (prerequisite != Feat::kNoPrerequisite && feats.count(type) > 0) {
            result.push_back(type);
        }
    }
    return result;
}

static bool dependsOn(
    FeatType type,
    FeatType prerequisite,
    const std::unordered_map<FeatType, std::shared_ptr<Feat>> &feats,
    std::set<FeatType> &visited) {

    if (!visited.insert(type).second) {
        return false;
    }

    auto maybeFeat = feats.find(type);
    if (maybeFeat == feats.end()) {
        return false;
    }

    for (auto directPrerequisite : getValidPrerequisites(*maybeFeat->second, feats)) {
        if (directPrerequisite == prerequisite ||
            dependsOn(directPrerequisite, prerequisite, feats, visited)) {
            return true;
        }
    }
    return false;
}

static bool dependsOn(
    FeatType type,
    FeatType prerequisite,
    const std::unordered_map<FeatType, std::shared_ptr<Feat>> &feats) {

    std::set<FeatType> visited;
    return dependsOn(type, prerequisite, feats, visited);
}

static bool isCoherentSuccessor(
    const Feat &parent,
    FeatType parentType,
    const Feat &child,
    const std::unordered_map<FeatType, std::shared_ptr<Feat>> &feats) {

    if (hasPrerequisite(child, parentType)) {
        return true;
    }

    auto childPrerequisites = getValidPrerequisites(child, feats);
    return childPrerequisites.empty() && child.pips == parent.pips + 1;
}

static std::optional<FeatType> getPrerequisiteParent(
    const Feat &feat,
    const std::unordered_map<FeatType, std::shared_ptr<Feat>> &feats) {

    auto prerequisites = getValidPrerequisites(feat, feats);
    if (prerequisites.size() == 1) {
        return prerequisites.front();
    }
    if (prerequisites.size() != 2) {
        return std::nullopt;
    }

    if (dependsOn(prerequisites[0], prerequisites[1], feats)) {
        return prerequisites[0];
    }
    if (dependsOn(prerequisites[1], prerequisites[0], feats)) {
        return prerequisites[1];
    }
    return std::nullopt;
}

static std::unordered_map<FeatType, FeatType> getDisplayParents(
    const std::unordered_map<FeatType, std::shared_ptr<Feat>> &feats) {

    std::unordered_map<FeatType, std::vector<FeatType>> successorParents;
    for (auto &feat : feats) {
        if (!isValidFeat(feat.second->successor) || feats.count(feat.second->successor) == 0) {
            continue;
        }

        const Feat &child = *feats.at(feat.second->successor);
        if (isCoherentSuccessor(*feat.second, feat.first, child, feats)) {
            successorParents[feat.second->successor].push_back(feat.first);
        }
    }

    std::unordered_map<FeatType, FeatType> result;
    for (auto &feat : feats) {
        auto maybeSuccessorParents = successorParents.find(feat.first);
        if (maybeSuccessorParents != successorParents.end() && maybeSuccessorParents->second.size() == 1) {
            result.insert({feat.first, maybeSuccessorParents->second.front()});
            continue;
        }

        auto prerequisiteParent = getPrerequisiteParent(*feat.second, feats);
        if (prerequisiteParent) {
            result.insert({feat.first, *prerequisiteParent});
        }
    }
    return result;
}

static FeatType getChainRoot(
    FeatType type,
    const std::unordered_map<FeatType, FeatType> &parents) {

    FeatType result = type;
    std::set<FeatType> visited;
    while (visited.insert(result).second) {
        auto maybeParent = parents.find(result);
        if (maybeParent == parents.end()) {
            break;
        }
        result = maybeParent->second;
    }
    return result;
}

static int getVisualIndex(
    FeatType type,
    const std::unordered_map<FeatType, FeatType> &parents) {

    int result = 0;
    FeatType current = type;
    std::set<FeatType> visited;
    while (visited.insert(current).second) {
        auto maybeParent = parents.find(current);
        if (maybeParent == parents.end()) {
            break;
        }
        ++result;
        current = maybeParent->second;
    }
    return result;
}

static bool hasOwnedChainMember(
    FeatType type,
    const CreatureAttributes &attributes,
    const std::unordered_map<FeatType, FeatType> &parents) {

    FeatType current = type;
    std::set<FeatType> visited;
    while (visited.insert(current).second) {
        if (attributes.hasFeat(current)) {
            return true;
        }

        auto maybeParent = parents.find(current);
        if (maybeParent == parents.end()) {
            break;
        }
        current = maybeParent->second;
    }
    return false;
}

void Feats::init() {
    std::shared_ptr<TwoDA> feats(_twoDas.get("feat"));
    if (!feats) {
        return;
    }

    // A table without a name column names its feats in its feat column.
    const std::string nameColumn = feats->getColumnIndex("name") != -1 ? "name" : "feat";
    for (int row = 0; row < feats->getRowCount(); ++row) {
        // A row without a name is not a feat.
        const auto nameStrRef = feats->getIntOpt(row, nameColumn);
        if (!nameStrRef) {
            continue;
        }
        std::string name(_strings.getText(*nameStrRef));
        std::string description(_strings.getText(feats->getInt(row, "description", -1)));
        std::shared_ptr<Texture> icon(_textures.get(feats->getString(row, "icon"), TextureUsage::GUI));
        int category = feats->getInt(row, "category");
        // The uses a day and the minimum character level are bytes.
        int usesPerDay = feats->getInt(row, "usesperday") & 0xff;
        int maxCR = feats->getInt(row, "maxcr");
        auto exclusion = static_cast<uint32_t>(feats->getInt(row, "exclusion"));
        int minCharLevel = feats->getInt(row, "mincharlevel") & 0xff;
        // A prerequisite is a feat word; a blank cell names none.
        int preReqFeat1 = feats->getIntOpt(row, "prereqfeat1").value_or(Feat::kNoPrerequisite) & 0xffff;
        int preReqFeat2 = feats->getIntOpt(row, "prereqfeat2").value_or(Feat::kNoPrerequisite) & 0xffff;
        // The alternative prerequisites and the required skill are words,
        // set only by a cell that is not blank.
        std::vector<int> alternativePrerequisites;
        for (int i = 0; i < kAlternativePrerequisiteCount; ++i) {
            auto word = feats->getIntOpt(row, kAlternativePrerequisitePrefix + std::to_string(i));
            if (word && (*word & 0xffff) != Feat::kNoPrerequisite) {
                alternativePrerequisites.push_back(*word & 0xffff);
            }
        }
        std::optional<SkillType> requiredSkill;
        auto skillWord = feats->getIntOpt(row, "reqskill");
        if (skillWord && (*skillWord & 0xffff) != kNoRequiredSkill) {
            requiredSkill = static_cast<SkillType>(*skillWord & 0xffff);
        }
        // The minimum attack bonus and ability scores are bytes. The minimum
        // spell level column sets no requirement: every feat's minimum spell
        // level is 0, so no class's known powers are ever asked for.
        int minAttackBonus = feats->getInt(row, "minattackbonus") & 0xff;
        std::map<Ability, int> minAbilityScores;
        for (auto &[column, ability] : kMinAbilityColumns) {
            minAbilityScores.insert(std::make_pair(ability, feats->getInt(row, column) & 0xff));
        }
        auto successor = static_cast<FeatType>(feats->getInt(row, "successor"));
        uint32_t pips = feats->getInt(row, "pips");
        std::optional<int> spellId = feats->getIntOpt(row, "spellid");

        auto feat = std::make_shared<Feat>();
        feat->name = std::move(name);
        feat->nameStrRef = *nameStrRef;
        feat->description = std::move(description);
        feat->icon = std::move(icon);
        feat->category = category;
        feat->usesPerDay = usesPerDay;
        feat->maxCR = maxCR;
        feat->exclusion = exclusion;
        feat->minCharLevel = minCharLevel;
        feat->preReqFeat1 = preReqFeat1;
        feat->preReqFeat2 = preReqFeat2;
        feat->alternativePrerequisites = std::move(alternativePrerequisites);
        feat->minAttackBonus = minAttackBonus;
        feat->minAbilityScores = std::move(minAbilityScores);
        feat->requiredSkill = requiredSkill;
        feat->successor = successor;
        feat->pips = pips;
        feat->spellId = spellId;
        for (auto column : kCompanionColumns) {
            // A companion level is a byte.
            int level = feats->getInt(row, column) & 0xff;
            if (level != 0) {
                feat->companionLevels.insert(std::make_pair(
                    column,
                    level == kNeverGrantedCompanionLevel ? std::nullopt : std::optional<int>(level)));
            }
        }
        _feats.insert(std::make_pair(static_cast<FeatType>(row), std::move(feat)));
        _featOrder.push_back(static_cast<FeatType>(row));
    }
}

std::shared_ptr<Feat> Feats::get(FeatType type) const {
    auto it = _feats.find(type);
    return it != _feats.end() ? it->second : nullptr;
}

int Feats::getLevelUpChoiceCount(const CreatureAttributes &attributes, const CreatureClass &clazz) const {
    int nextClassLevel = attributes.getClassLevel(clazz.type()) + 1;
    return clazz.getFeatGain(nextClassLevel) + clazz.getBonusFeatGain(nextClassLevel);
}

std::vector<FeatType> Feats::getChoosableFeats(const FeatChoice &choice) const {
    std::vector<FeatType> result;
    for (auto type : _featOrder) {
        if (canChooseFeat(type, choice)) {
            result.push_back(type);
        }
    }
    return result;
}

bool Feats::canChooseFeat(FeatType type, const FeatChoice &choice) const {
    if (choice.tsl && isUnchoosableFeat(type)) {
        return false;
    }
    std::shared_ptr<Feat> feat(get(type));
    if (!feat || (choice.normalPicks == 0 && choice.bonusPicks == 0)) {
        return false;
    }
    if (choice.tsl) {
        // Bao-Dur and Kreia cannot choose the feats their columns never grant them.
        std::string companion(boost::to_lower_copy(choice.tag));
        if (companion == kBaoDurColumn || companion == kKreiaColumn) {
            auto companionLevel = feat->companionLevels.find(companion);
            if (companionLevel != feat->companionLevels.end() && !companionLevel->second) {
                return false;
            }
        }
    }
    if (choice.attributes.hasFeat(type) || choice.chosen.count(type) > 0) {
        return false;
    }
    if (!meetsRequirements(*feat, choice)) {
        return false;
    }

    auto isNormal = [&choice](FeatType type) { return choice.clazz.isNormalFeat(type); };
    auto isBonus = [&choice](FeatType type) { return choice.clazz.isBonusFeat(type); };
    if (!isNormal(type) && !isBonus(type)) {
        return false;
    }

    // The chosen feats take their picks first: a normal-only feat takes a
    // normal pick, and fails the choice when none is left; a bonus-only feat
    // takes a bonus pick while one is left.
    int normalPicks = choice.normalPicks;
    int bonusPicks = choice.bonusPicks;
    int unassigned = 0;
    for (auto chosen : choice.chosen) {
        bool normal = isNormal(chosen);
        bool bonus = isBonus(chosen);
        if (normal && !bonus) {
            if (normalPicks == 0) {
                return false;
            }
            --normalPicks;
        } else if (!normal && bonus && bonusPicks > 0) {
            --bonusPicks;
        } else {
            ++unassigned;
        }
    }

    // Then the feat takes a pick of its only kind.
    bool assigned = false;
    if (isNormal(type) && !isBonus(type)) {
        if (normalPicks == 0) {
            return false;
        }
        --normalPicks;
        assigned = true;
    } else if (!isNormal(type) && isBonus(type)) {
        if (bonusPicks == 0) {
            return false;
        }
        --bonusPicks;
        assigned = true;
    }

    // The other chosen feats take a normal pick, or a bonus pick when no
    // normal pick is left.
    for (int i = 0; i < unassigned; ++i) {
        if (normalPicks > 0) {
            --normalPicks;
        } else if (bonusPicks > 0) {
            --bonusPicks;
        } else {
            return false;
        }
    }

    // A feat of either kind needs a pick of either kind left.
    return assigned || normalPicks > 0 || bonusPicks > 0;
}

bool Feats::meetsRequirements(const Feat &feat, const FeatChoice &choice) const {
    const CreatureAttributes &attributes = choice.attributes;
    if (attributes.getAggregateLevel() < feat.minCharLevel) {
        return false;
    }
    // The base attack bonus is the sum of the classes' bonuses, in a byte.
    if (feat.minAttackBonus > (attributes.getAggregateAttackBonus() & 0xff)) {
        return false;
    }
    for (auto &[ability, minScore] : feat.minAbilityScores) {
        if (minScore > attributes.getAbilityScore(ability)) {
            return false;
        }
    }
    if (!meetsPrerequisites(feat, attributes, choice.effectFeats, choice.chosen)) {
        return false;
    }
    return !feat.requiredSkill || canUseSkill(*feat.requiredSkill, attributes, choice.tsl);
}

bool Feats::hasPrerequisites(FeatType type, const CreatureAttributes &attributes, const std::set<FeatType> &effectFeats, const std::set<FeatType> &chosen) const {
    return meetsPrerequisites(*_feats.at(type), attributes, effectFeats, chosen);
}

bool Feats::canUseSkill(SkillType skill, const CreatureAttributes &attributes, bool tsl) const {
    // A skill past the skills table cannot be used.
    std::shared_ptr<Skill> rules(_skills.get(skill));
    if (!rules) {
        return false;
    }
    // In TSL, the camouflage powers and the personal cloaking shield let a
    // character use Stealth.
    if (tsl && skill == SkillType::Stealth &&
        (attributes.hasSpell(SpellType::ForceCamouflage) ||
         attributes.hasSpell(SpellType::ImprovedForceCamouflage) ||
         attributes.hasSpell(SpellType::MasterForceCamouflage) ||
         attributes.hasFeat(FeatType::PersonalCloakingShield))) {
        return true;
    }
    if (rules->untrained) {
        const auto &classLevels = attributes.classLevels();
        return std::any_of(classLevels.begin(), classLevels.end(), [&rules](const auto &classLevel) {
            return rules->allClassesCanUse || classLevel.first->hasSkills();
        });
    }
    return attributes.getSkillRank(skill) > 0;
}

std::vector<FeatDisplayEntry> Feats::getLevelUpDisplayEntries(const CreatureAttributes &attributes, const CreatureClass &clazz) const {
    auto parents = getDisplayParents(_feats);

    std::vector<FeatDisplayEntry> result;
    for (auto &feat : _feats) {
        FeatType chainRoot = getChainRoot(feat.first, parents);

        bool owned = attributes.hasFeat(feat.first);
        bool chainVisible = clazz.isFeatSelectable(chainRoot);
        bool manualFeatVisible = clazz.isFeatSelectable(feat.first) &&
                                 (chainVisible || hasOwnedChainMember(feat.first, attributes, parents));
        bool visible = owned || manualFeatVisible;
        if (!visible) {
            continue;
        }

        FeatDisplayEntry entry;
        entry.type = feat.first;
        entry.chainRoot = chainRoot;
        entry.tier = static_cast<int>(feat.second->pips);
        entry.visualIndex = getVisualIndex(feat.first, parents);
        result.push_back(std::move(entry));
    }

    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        if (left.chainRoot != right.chainRoot) {
            return static_cast<int>(left.chainRoot) < static_cast<int>(right.chainRoot);
        }
        if (left.visualIndex != right.visualIndex) {
            return left.visualIndex < right.visualIndex;
        }
        return static_cast<int>(left.type) < static_cast<int>(right.type);
    });
    return result;
}

std::vector<FeatType> Feats::addGrantedFeats(CreatureAttributes &attributes, RacialType race, const std::string &tag, bool tsl, bool newCharacter) const {
    const auto &classLevels = attributes.classLevels();
    assert(!classLevels.empty());
    const CreatureClass &clazz = *classLevels.back().first;
    const int classLevel = classLevels.back().second;
    const int characterLevel = attributes.getAggregateLevel();
    const int jediLevel = attributes.getJediLevel();
    std::string companion(boost::to_lower_copy(tag));
    const std::unordered_set<FeatType> noRaceFeats;
    const auto &raceFeats = characterLevel == 1 ? _races.get(race).firstLevelFeats : noRaceFeats;

    // The granted feats are listed whether or not they were held, except
    // that TSL does not list the class's grants of the feats no character
    // chooses.
    std::vector<FeatType> result;
    auto grant = [&attributes, &result](FeatType type, bool listed) {
        attributes.addFeat(type);
        if (listed) {
            result.push_back(type);
        }
    };

    for (auto type : _featOrder) {
        // At the first character level the race's feats are granted, and the
        // class does not decide them.
        if (raceFeats.count(type) > 0) {
            grant(type, true);
            continue;
        }

        const Feat &feat = *_feats.at(type);
        auto companionLevel = feat.companionLevels.find(companion);
        bool hasCompanionLevel = tsl && companionLevel != feat.companionLevels.end();

        if (auto grantedLevel = clazz.getFeatGrantedLevel(type)) {
            if (tsl && isUnarmedSpecialistFeat(type)) {
                if (clazz.isJedi() && *grantedLevel == jediLevel) {
                    grant(type, true);
                }
                continue;
            }
            if (*grantedLevel != classLevel) {
                continue;
            }
            // Bao-Dur and Kreia are never granted the class feats their
            // columns mark as never granted.
            bool excluded = (companion == kBaoDurColumn || companion == kKreiaColumn) &&
                            hasCompanionLevel && !companionLevel->second;
            if (!excluded) {
                grant(type, !(tsl && isUnchoosableFeat(type)));
            }
        } else if (tsl && newCharacter && clazz.isPCGrantedFeat(type)) {
            grant(type, true);
        } else if (companion != kKreiaColumn && hasCompanionLevel && companionLevel->second == characterLevel) {
            // The other companions gain their own feats at a character level.
            grant(type, true);
        }
    }

    // War Veteran is listed only when it was not held.
    if (tsl && newCharacter && !attributes.hasFeat(FeatType::WarVeteran)) {
        grant(FeatType::WarVeteran, true);
    }
    return result;
}

} // namespace game

} // namespace reone
