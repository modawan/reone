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

#pragma once

#include "../effect.h"

namespace reone {

namespace game {

/**
 * Builds an area of effect around the creature it is applied to, from the
 * vfx_persistent row in integer 0, with the scripts in strings 0 to 2 (enter,
 * heartbeat, exit) replacing the row's. The area of effect goes with the
 * effect.
 */
class AreaOfEffectEffect : public CopyableEffect<AreaOfEffectEffect> {
public:
    AreaOfEffectEffect() :
        CopyableEffect(EffectType::AreaOfEffect) {
    }

    /** Only a creature carries one; the new area of effect is object parameter 0. */
    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
    EffectRemovalResult onRemove(Object &object, const EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
