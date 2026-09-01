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

class ACIncreaseEffect : public Effect {
public:
    ACIncreaseEffect(int value, ACBonus modifyType, int damageType) :
        Effect(EffectType::ACIncrease),
        _value(value),
        _modifyType(modifyType),
        _damageType(damageType) {
        setSaveFacingInteger(0, static_cast<int>(modifyType));
        setSaveFacingInteger(1, value);
        setSaveFacingInteger(2, static_cast<int>(RacialType::All));
        setSaveFacingInteger(5, damageType);
    }

    void applyTo(Object &object) override;

    int bonus() const { return _value; }
    ACBonus modifierType() const { return _modifyType; }
    int damageType() const { return _damageType; }

private:
    int _value;
    ACBonus _modifyType;
    int _damageType;
};

} // namespace game

} // namespace reone
