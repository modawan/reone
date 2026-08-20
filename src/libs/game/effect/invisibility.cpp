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

#include "reone/game/effect/invisibility.h"

#include "reone/game/object/creature.h"
#include "reone/system/cast.h"

namespace reone {
namespace game {

bool InvisibilityEffect::onApply(Object &object) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return false;
    }
    creature->refreshEffectInvisibility();
    return true;
}

void InvisibilityEffect::onRemove(Object &object) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->refreshEffectInvisibility();
    }
}

} // namespace game
} // namespace reone
