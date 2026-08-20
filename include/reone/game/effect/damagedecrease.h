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

class DamageDecreaseEffect : public Effect {
public:
    DamageDecreaseEffect(
        int penalty,
        DamageType damageType,
        EffectProvenance provenance = {}) :
        Effect(EffectType::DamageDecrease, std::move(provenance)),
        _penalty(penalty),
        _damageType(damageType) {
    }

    void applyTo(Object &object) override;

    int penalty() const { return _penalty; }
    DamageType damageType() const { return _damageType; }

private:
    int _penalty;
    DamageType _damageType;
};

} // namespace game

} // namespace reone
