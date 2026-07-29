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
#include "reone/game/effect/immunity.h"
#include "reone/game/object.h"

namespace reone {

namespace game {

void DamageImmunityIncreaseEffect::applyTo(Object &) {
    if (_percentImmunity < 0) {
        _active = false;
    }
}

void DamageImmunityDecreaseEffect::applyTo(Object &object) {
    if (_percentImmunity < 0 || object.plotFlag()) {
        _active = false;
        return;
    }

    for (const Object::AppliedEffect &applied : object.effects()) {
        if (applied.effect->type() != EffectType::Immunity) {
            continue;
        }

        const auto &effect = static_cast<const ImmunityEffect &>(*applied.effect);
        if (effect.immunityType() == ImmunityType::DamageImmunityDecrease) {
            _active = false;
            return;
        }
    }
}

} // namespace game

} // namespace reone
