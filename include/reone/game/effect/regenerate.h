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
 * have passed since the last heal (integers 2 and 3).
 * Integer 4 selects the resource it restores, as for a heal.
 */
class RegenerateEffect : public CopyableEffect<RegenerateEffect> {
public:
    /**
     * An interval in seconds, taken in whole milliseconds: one below zero is
     * none, and one past what an int holds, or not a number, is the longest.
     */
    RegenerateEffect(int amount, float intervalSeconds) :
        RegenerateEffect(amount, wholeMilliseconds(intervalSeconds), 0) {
    }

    RegenerateEffect(int amount, int intervalMilliseconds, int resourceSelector) :
        CopyableEffect(EffectType::Regenerate) {
        setSaveFacingInteger(0, amount);
        setSaveFacingInteger(1, intervalMilliseconds);
        setSaveFacingInteger(4, resourceSelector);
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
    void onUpdate(Object &object, const EffectInstance &instance, float dt) override;

private:
    static int wholeMilliseconds(float seconds) {
        const uint64_t milliseconds = getWholeMilliseconds(seconds);
        return static_cast<int>(std::min<uint64_t>(milliseconds, std::numeric_limits<int>::max()));
    }
};

} // namespace game

} // namespace reone
