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

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static bool isValidFeat(FeatType type) {
    return type != FeatType::Invalid;
}

static bool isManualFeatListValue(int value) {
    return value == 0 || value == 1;
}

static bool hasPrerequisite(const Feat &feat, FeatType prerequisite) {
    return feat.preReqFeat1 == prerequisite || feat.preReqFeat2 == prerequisite;
}

static std::vector<FeatType> getValidPrerequisites(
    const Feat &feat,
    const std::unordered_map<FeatType, std::shared_ptr<Feat>> &feats) {

    std::vector<FeatType> result;
    if (isValidFeat(feat.preReqFeat1) && feats.count(feat.preReqFeat1) > 0) {
        result.push_back(feat.preReqFeat1);
    }
    if (isValidFeat(feat.preReqFeat2) && feats.count(feat.preReqFeat2) > 0) {
        result.push_back(feat.preReqFeat2);
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

    for (int row = 0; row < feats->getRowCount(); ++row) {
        std::string name(_strings.getText(feats->getInt(row, "name", -1)));
        std::string description(_strings.getText(feats->getInt(row, "description", -1)));
        std::shared_ptr<Texture> icon(_textures.get(feats->getString(row, "icon"), TextureUsage::GUI));
        uint32_t minCharLevel = feats->getInt(row, "mincharlevel");
        auto preReqFeat1 = static_cast<FeatType>(feats->getInt(row, "prereqfeat1"));
        auto preReqFeat2 = static_cast<FeatType>(feats->getInt(row, "prereqfeat2"));
        auto successor = static_cast<FeatType>(feats->getInt(row, "successor"));
        uint32_t pips = feats->getInt(row, "pips");

        auto feat = std::make_shared<Feat>();
        feat->name = std::move(name);
        feat->description = std::move(description);
        feat->icon = std::move(icon);
        feat->minCharLevel = minCharLevel;
        feat->preReqFeat1 = preReqFeat1;
        feat->preReqFeat2 = preReqFeat2;
        feat->successor = successor;
        feat->pips = pips;
        _feats.insert(std::make_pair(static_cast<FeatType>(row), std::move(feat)));
    }
}

std::shared_ptr<Feat> Feats::get(FeatType type) const {
    auto it = _feats.find(type);
    return it != _feats.end() ? it->second : nullptr;
}

int Feats::getLevelUpChoiceCount(const CreatureAttributes &attributes, const CreatureClass &clazz) const {
    int nextClassLevel = attributes.getClassLevel(clazz.type()) + 1;
    return clazz.getFeatGain(nextClassLevel);
}

bool Feats::isLevelUpCandidate(FeatType type, const CreatureAttributes &attributes, const CreatureClass &clazz) const {
    if (!isValidFeat(type) || attributes.hasFeat(type) || !clazz.isFeatSelectable(type)) {
        return false;
    }

    std::shared_ptr<Feat> feat(get(type));
    if (!feat) {
        return false;
    }

    int nextCharacterLevel = attributes.getAggregateLevel() + 1;
    if (nextCharacterLevel < static_cast<int>(feat->minCharLevel)) {
        return false;
    }

    if (isValidFeat(feat->preReqFeat1) && !attributes.hasFeat(feat->preReqFeat1)) {
        return false;
    }
    if (isValidFeat(feat->preReqFeat2) && !attributes.hasFeat(feat->preReqFeat2)) {
        return false;
    }

    return true;
}

std::vector<FeatType> Feats::getLevelUpCandidates(const CreatureAttributes &attributes, const CreatureClass &clazz) const {
    std::vector<FeatType> result;
    for (auto &feat : _feats) {
        if (isLevelUpCandidate(feat.first, attributes, clazz)) {
            result.push_back(feat.first);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<FeatDisplayEntry> Feats::getLevelUpDisplayEntries(const CreatureAttributes &attributes, const CreatureClass &clazz) const {
    auto parents = getDisplayParents(_feats);

    std::vector<FeatDisplayEntry> result;
    for (auto &feat : _feats) {
        FeatType chainRoot = getChainRoot(feat.first, parents);

        bool owned = attributes.hasFeat(feat.first);
        int listValue = clazz.getFeatListValue(feat.first);
        bool chainVisible = clazz.isFeatSelectable(chainRoot);
        bool manualFeatVisible = isManualFeatListValue(listValue) &&
                                 (chainVisible || hasOwnedChainMember(feat.first, attributes, parents));
        bool visible = owned || manualFeatVisible;
        if (!visible) {
            continue;
        }

        FeatAvailability availability = FeatAvailability::Owned;
        if (owned) {
            availability = FeatAvailability::Owned;
        } else if (isManualFeatListValue(listValue)) {
            int nextCharacterLevel = attributes.getAggregateLevel() + 1;
            if (nextCharacterLevel < static_cast<int>(feat.second->minCharLevel)) {
                availability = FeatAvailability::LockedMinLevel;
            } else if ((isValidFeat(feat.second->preReqFeat1) && !attributes.hasFeat(feat.second->preReqFeat1)) ||
                       (isValidFeat(feat.second->preReqFeat2) && !attributes.hasFeat(feat.second->preReqFeat2))) {
                availability = FeatAvailability::LockedMissingPrerequisite;
            } else {
                availability = FeatAvailability::Selectable;
            }
        }

        FeatDisplayEntry entry;
        entry.type = feat.first;
        entry.availability = availability;
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

} // namespace game

} // namespace reone
