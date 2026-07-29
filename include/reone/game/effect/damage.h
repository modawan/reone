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

#include "reone/system/smallvector.h"

#include "../effect.h"

namespace reone {

namespace game {

struct DamageComponent {
    int amount;
    DamageType type;
};

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
    void addBaseDamage(int amount, DamageType type);
    void setPower(DamagePower power);
    void mitigate(Object &object);

    int total() const;
    int baseDamage() const { return _baseDamage; }
    DamagePower power() const { return _power; }
    bool empty() const { return _components.empty(); }

    const ISmallVector<DamageComponent> &components() const { return _components; }

private:
    void addResolved(int amount, DamageType type);

    DamagePower _power;
    int _baseDamage {0};
    DamageType _baseDamageType {DamageType::Universal};
    SmallVector<DamageComponent, 4> _components;
    SmallVector<DamageComponent, 12> _contributions;
    bool _mitigated {false};
};

class DamageEffect : public Effect {
public:
    DamageEffect(int amount,
                 DamageType type,
                 DamagePower power,
                 uint32_t damager) :
        Effect(EffectType::Damage),
        _damage(power),
        _damager(damager) {
        _damage.addBaseDamage(amount, type);
    }

    DamageEffect(DamagePacket damage, uint32_t damager) :
        Effect(EffectType::Damage),
        _damage(std::move(damage)),
        _damager(damager) {
    }

    void applyTo(Object &object) override;

    int amount() const { return _damage.total(); }
    const DamagePacket &packet() const { return _damage; }
    uint32_t damager() const { return _damager; }

private:
    DamagePacket _damage;
    uint32_t _damager;
};

} // namespace game

} // namespace reone
