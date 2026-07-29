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

#include <algorithm>

#include "../effect.h"

namespace reone {

namespace game {

class DamageReductionEffect : public Effect {
public:
    DamageReductionEffect(int amount, DamagePower damagePower, int limit) :
        Effect(EffectType::DamageReduction),
        _amount(amount),
        _damagePower(damagePower),
        _limit(limit),
        _limited(limit > 0) {
    }

    void applyTo(Object &) override {
    }

    int amount() const { return _amount; }
    DamagePower damagePower() const { return _damagePower; }

    int absorb(int damage) {
        if (damage <= 0 || _amount <= 0) {
            return 0;
        }

        if (!_limited) {
            return std::min(damage, _amount);
        }

        int remaining = _limit;
        _limit = std::max(0, _limit - damage);
        if (_limit == 0) {
            return std::min(damage, remaining);
        }
        return std::min(damage, _amount);
    }

    bool exhausted() const { return _limited && _limit == 0; }

private:
    int _amount;
    DamagePower _damagePower;
    int _limit;
    bool _limited;
};

} // namespace game

} // namespace reone
