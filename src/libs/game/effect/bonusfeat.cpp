/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/effect/bonusfeat.h"

#include "reone/game/object/creature.h"

namespace reone {

namespace game {

BonusFeatEffect::BonusFeatEffect(
    FeatType feat,
    EffectProvenance provenance) :
    Effect(EffectType::BonusFeat, std::move(provenance)),
    _feat(feat) {
}

bool BonusFeatEffect::onApply(Object &object) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || _feat == FeatType::Invalid) {
        return false;
    }
    creature->addBonusFeat(_feat);
    return true;
}

void BonusFeatEffect::onRemove(Object &object) {
    auto *creature = dyn_cast<Creature>(&object);
    if (creature) {
        creature->removeBonusFeat(_feat);
    }
}

} // namespace game

} // namespace reone
