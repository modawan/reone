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

/** Raises an ability score; the ability and the amount are its first two integers. */
class AbilityIncreaseEffect : public CopyableEffect<AbilityIncreaseEffect> {
public:
    AbilityIncreaseEffect(Ability abilityToIncrease, int modifyBy) :
        CopyableEffect(EffectType::AbilityIncrease) {
        setSaveFacingInteger(0, static_cast<int>(abilityToIncrease));
        setSaveFacingInteger(1, modifyBy);
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &) override;
};

} // namespace game

} // namespace reone
