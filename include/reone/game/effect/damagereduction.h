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

class DamageReductionEffect : public Effect {
public:
    DamageReductionEffect(
        int amount,
        DamagePower damagePower,
        int limit,
        EffectProvenance provenance = {}) :
        Effect(EffectType::DamageReduction, std::move(provenance)),
        _absorption(amount, limit),
        _damagePower(damagePower) {
    }

    void applyTo(Object &) override {
    }

    int amount() const { return _absorption.amount(); }
    DamagePower damagePower() const { return _damagePower; }
    int absorb(int damage) { return _absorption.absorb(damage); }
    bool exhausted() const { return _absorption.exhausted(); }
    std::optional<int> remainingLimit() const {
        return _absorption.remainingLimit();
    }

private:
    DamageAbsorption _absorption;
    DamagePower _damagePower;
};

} // namespace game

} // namespace reone
