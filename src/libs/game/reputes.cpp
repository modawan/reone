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

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

#include "reone/game/object/creature.h"

using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kDefaultRepute = 50;

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

bool Reputes::getIsEnemy(const Creature &left, const Creature &right) const {
    return getIsEnemy(left.faction(), right.faction());
}

bool Reputes::getIsEnemy(Faction left, Faction right) const {
    return getRepute(left, right) < 50;
}

bool Reputes::getIsFriend(const Creature &left, const Creature &right) const {
    return getRepute(left.faction(), right.faction()) > 50;
}

bool Reputes::getIsNeutral(const Creature &left, const Creature &right) const {
    return getRepute(left.faction(), right.faction()) == 50;
}

int Reputes::getRepute(Faction left, Faction right) const {
    int leftFaction = static_cast<int>(left);
    int rightFaction = static_cast<int>(right);

    if (leftFaction < 0 || leftFaction >= _factionValues.size() ||
        rightFaction < 0 || rightFaction >= _factionValues[leftFaction].size())
        return kDefaultRepute;

    return _factionValues[leftFaction][rightFaction];
}

} // namespace game

} // namespace reone
