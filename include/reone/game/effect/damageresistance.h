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
#include "damageabsorption.h"

namespace reone {

namespace game {

class DamageResistanceEffect : public Effect {
public:
    DamageResistanceEffect(
        DamageType damageType,
        int amount,
        int limit,
        int secondaryDamageFlags,
        EffectProvenance provenance = {}) :
        Effect(EffectType::DamageResistance, std::move(provenance)),
        _damageType(damageType),
        _absorption(amount, limit),
        _secondaryDamageFlags(secondaryDamageFlags) {
    }

    void applyTo(Object &object) override;

    DamageType damageType() const { return _damageType; }
    int amount() const { return _absorption.amount(); }
    int secondaryDamageFlags() const { return _secondaryDamageFlags; }
    int absorb(int damage) { return _absorption.absorb(damage); }
    bool exhausted() const { return _absorption.exhausted(); }
    std::optional<int> remainingLimit() const {
        return _absorption.remainingLimit();
    }

private:
    DamageType _damageType;
    DamageAbsorption _absorption;
    int _secondaryDamageFlags;
};

} // namespace game

} // namespace reone
