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

class ConcealmentEffect : public CopyableEffect<ConcealmentEffect> {
public:
    /** Concealment applies to all races; a miss chance is a concealment with none. */
    ConcealmentEffect(int percentage, int racialType = static_cast<int>(RacialType::All)) :
        CopyableEffect(EffectType::Concealment),
        _percentage(percentage) {
        setSaveFacingInteger(0, percentage);
        setSaveFacingInteger(1, racialType);
    }

    EffectApplicationResult onApply(Object &, EffectInstance &instance) override {
        const int percentage = instance.integerParameter(0);
        return percentage >= 1 && percentage <= 100
                   ? EffectApplicationResult::Retained
                   : EffectApplicationResult::Rejected;
    }

private:
    int _percentage;
};

} // namespace game

} // namespace reone
