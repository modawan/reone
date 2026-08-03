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

#include "reone/game/reputes.h"

#include <algorithm>

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

#include "reone/game/object/creature.h"

using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kDefaultRepute = 50;
static constexpr int kMinRepute = 0;
static constexpr int kMaxRepute = 100;

void Reputes::init() {
    std::shared_ptr<TwoDA> repute(_twoDas.get("repute"));
    if (!repute) {
        return;
    }

    _factionLabels.clear();
    _factionValues.clear();

    for (int row = 0; row < repute->getRowCount(); ++row) {
        _factionLabels.push_back(boost::to_lower_copy(repute->getString(row, "label")));
    }

    for (int row = 0; row < repute->getRowCount(); ++row) {
        std::vector<int> values;
        for (size_t i = 0; i < _factionLabels.size(); ++i) {
            int value;

            const std::string &label = _factionLabels[i];
            if (label == "player" || label == "glb_xor") {
                value = kDefaultRepute;
            } else {
                value = repute->getInt(row, _factionLabels[i], kDefaultRepute);
            }

            values.push_back(value);
        }
        _factionValues.push_back(std::move(values));
    }
}

int Reputes::getReputation(Faction sourceFaction, Faction targetFaction) const {
    int source = static_cast<int>(sourceFaction);
    int target = static_cast<int>(targetFaction);

    if (source < 0 || source >= static_cast<int>(_factionValues.size()) ||
        target < 0 || target >= static_cast<int>(_factionValues[source].size()))
        return kDefaultRepute;

    return _factionValues[source][target];
}

void Reputes::adjustReputation(Faction sourceFaction, Faction targetFaction, int adjustment) {
    int source = static_cast<int>(sourceFaction);
    int target = static_cast<int>(targetFaction);

    // Only the source faction's view of the target moves. A faction's view of
    // itself is fixed, and out-of-range factions have no cell to adjust.
    if (source < 0 || source >= static_cast<int>(_factionValues.size()) ||
        target < 0 || target >= static_cast<int>(_factionValues[source].size()) ||
        source == target) {
        return;
    }

    int64_t adjusted = static_cast<int64_t>(_factionValues[source][target]) + adjustment;
    _factionValues[source][target] = static_cast<int>(
        std::clamp(adjusted, static_cast<int64_t>(kMinRepute), static_cast<int64_t>(kMaxRepute)));
}

bool Reputes::getIsEnemy(const Creature &source, const Creature &target) const {
    return getIsEnemy(source.faction(), target.faction());
}

bool Reputes::getIsEnemy(Faction sourceFaction, Faction targetFaction) const {
    return getReputation(sourceFaction, targetFaction) < 50;
}

bool Reputes::getIsFriend(const Creature &source, const Creature &target) const {
    return getReputation(source.faction(), target.faction()) > 50;
}

bool Reputes::getIsNeutral(const Creature &source, const Creature &target) const {
    return getReputation(source.faction(), target.faction()) == 50;
}

} // namespace game

} // namespace reone
