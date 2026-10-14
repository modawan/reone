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
 * Heals the creature by integer 0 each time more than integer 1 milliseconds
 * of the time of day have passed since the last heal (integers 2 and 3).
 * Integer 4 selects the resource it restores, as for a heal.
 */
class RegenerateEffect : public CopyableEffect<RegenerateEffect> {
public:
    RegenerateEffect(int amount, float intervalSeconds) :
        RegenerateEffect(amount, static_cast<int>(intervalSeconds * 1000.0f), 0) {
    }

    RegenerateEffect(int amount, int intervalMilliseconds, int resourceSelector) :
        CopyableEffect(EffectType::Regenerate) {
        setSaveFacingInteger(0, amount);
        setSaveFacingInteger(1, intervalMilliseconds);
        setSaveFacingInteger(4, resourceSelector);
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
    void onUpdate(Object &object, const EffectInstance &instance, float dt) override;
};

} // namespace game

} // namespace reone
