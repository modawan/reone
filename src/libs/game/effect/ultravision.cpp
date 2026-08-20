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

#include "reone/game/effect/ultravision.h"

#include "reone/game/object/creature.h"
#include "reone/system/cast.h"

namespace reone {
namespace game {

bool UltravisionEffect::onApply(Object &object) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return false;
    }
    creature->setVisibilityCounter(Creature::kUltravisionCounter, true);
    creature->refreshEffectInvisibility();
    return true;
}

void UltravisionEffect::onRemove(Object &object) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return;
    }
    creature->restoreVisibilityCounter(
        EffectType::Ultravision,
        Creature::kUltravisionCounter,
        false);
    creature->refreshEffectInvisibility();
}

} // namespace game
} // namespace reone
