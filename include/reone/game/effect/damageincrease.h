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

class DamageIncreaseEffect : public Effect {
public:
    DamageIncreaseEffect(
        int bonus,
        DamageType damageType,
        EffectProvenance provenance = {}) :
        Effect(EffectType::DamageIncrease, std::move(provenance)),
        _bonus(bonus),
        _damageType(damageType) {
    }

    void applyTo(Object &object) override;

    int bonus() const { return _bonus; }
    DamageType damageType() const { return _damageType; }

private:
    int _bonus;
    DamageType _damageType;
};

} // namespace game

} // namespace reone
