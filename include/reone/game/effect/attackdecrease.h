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
 * Lowers attack rolls: the amount is integer 0, the modifier type integer 1
 * and the racial type integer 2.
 */
class AttackDecreaseEffect : public CopyableEffect<AttackDecreaseEffect> {
public:
    AttackDecreaseEffect(int penalty, AttackBonus modifierType) :
        CopyableEffect(EffectType::AttackDecrease) {
        setSaveFacingInteger(0, penalty);
        setSaveFacingInteger(1, static_cast<int>(modifierType));
        setSaveFacingInteger(2, static_cast<int>(RacialType::All));
    }

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;
};

} // namespace game

} // namespace reone
