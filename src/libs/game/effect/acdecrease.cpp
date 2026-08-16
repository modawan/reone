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

#include "reone/game/effect/acdecrease.h"

#include "reone/game/game.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

bool ACDecreaseEffect::onApply(Object &object) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return false;
    }
    if (_value <= 0 || creature->plotFlag()) {
        return false;
    }

    auto creatorObject = object.game().getObjectById(creatorId());
    const auto *creator = creatorObject
                              ? dyn_cast<Creature>(creatorObject.get())
                              : nullptr;
    return !creature->hasEffectImmunity(
        ImmunityType::AcDecrease,
        creator);
}

} // namespace game

} // namespace reone
