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

void Feats::init() {
    std::shared_ptr<TwoDA> feats(_twoDas.get("feat"));
    if (!feats) {
        return;
    }

    for (int row = 0; row < feats->getRowCount(); ++row) {
        std::string name(_strings.getText(feats->getInt(row, "name", -1)));
        std::string description(_strings.getText(feats->getInt(row, "description", -1)));
        std::shared_ptr<Texture> icon(_textures.get(feats->getString(row, "icon"), TextureUsage::GUI));
        uint32_t minCharLevel = feats->getHexInt(row, "mincharlevel");
        auto preReqFeat1 = static_cast<FeatType>(feats->getHexInt(row, "prereqfeat1"));
        auto preReqFeat2 = static_cast<FeatType>(feats->getHexInt(row, "prereqfeat2"));
        auto successor = static_cast<FeatType>(feats->getHexInt(row, "successor"));
        uint32_t pips = feats->getHexInt(row, "pips");

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

} // namespace game

} // namespace reone
