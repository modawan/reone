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

#include <optional>
#include <stdexcept>

#include "reone/system/smallvector.h"

#include "../effect.h"

namespace reone {

namespace game {

inline constexpr int kAllDamageTypeFlags = 8199;
inline constexpr int kPhysicalDamageTypeFlags = 16391;

DamageType getPrimaryDamageType(int damageFlags);
bool damageTypeMatches(int modifierFlags, int damageFlags);

/**
 * Typed damage caused by one hit.
 *
 * A packet may contain multiple damage types, but it is applied to the target
 * as one damage event.
 */
class DamagePacket {
public:
    explicit DamagePacket(DamagePower power = DamagePower::Normal) :
        _power(power) {
    }

    void add(int amount, DamageType type);
    void setDamageFlags(int damageFlags);
    void setPower(DamagePower power);
    void resolve(Object &object);

    int total() const;
    int resolvedDamage() const;
    bool empty() const { return _components.empty(); }
    bool isResolved() const { return _resolvedDamage.has_value(); }

private:
    struct Component {
        int amount;
        DamageType type;
    };

    void requireUnresolved() const;
    void addResolved(int amount, DamageType type);

    DamagePower _power;
    int _damageFlags {0};
    SmallVector<Component, 4> _components;
    std::optional<int> _resolvedDamage;
};

class DamageEffect : public Effect {
public:
    DamageEffect(int amount,
                 DamageType type,
                 DamagePower power) :
        Effect(EffectType::Damage),
        _damage(power) {
        _damage.add(amount, type);
        _damage.setDamageFlags(static_cast<int>(type));
    }

    explicit DamageEffect(DamagePacket damage) :
        Effect(EffectType::Damage),
        _damage(std::move(damage)) {
        if (!_damage.isResolved()) {
            throw std::invalid_argument("Damage packet has not been resolved");
        }
    }

    bool onApply(Object &object, const EffectInstance &instance) override;

private:
    DamagePacket _damage;
};

} // namespace game

} // namespace reone
