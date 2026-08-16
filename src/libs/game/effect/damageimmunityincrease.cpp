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

#include "reone/game/effect/damageimmunityincrease.h"

#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/object.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/system/cast.h"

namespace reone {

namespace game {

bool DamageImmunityIncreaseEffect::onApply(Object &) {
    return _percentImmunity >= 0;
}

bool DamageImmunityDecreaseEffect::onApply(Object &object) {
    if (_percentImmunity < 0) {
        return false;
    }
    if (object.plotFlag()) {
        return false;
    }
    auto *target = dyn_cast<Creature>(&object);
    if (!target) {
        return true;
    }
    auto creatorObject = object.game().getObjectById(creatorId());
    const auto *creator = creatorObject
                              ? dyn_cast<Creature>(creatorObject.get())
                              : nullptr;
    return !target->hasEffectImmunity(
        ImmunityType::DamageImmunityDecrease,
        creator);
}

} // namespace game

} // namespace reone
