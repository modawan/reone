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

#include "reone/game/effect/stunned.h"

#include "reone/game/object/creature.h"

namespace reone {

namespace game {

void StunnedEffect::applyTo(Object &object) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setMovementType(Creature::MovementType::None);
    }
}

void StunnedEffect::onRemove(Object &object) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        for (const Object::AppliedEffect &applied : creature->effects()) {
            if (applied.effect->type() == EffectType::Stunned) {
                return;
            }
        }
        creature->resumeStateDrivenAnimation();
    }
}

} // namespace game

} // namespace reone
