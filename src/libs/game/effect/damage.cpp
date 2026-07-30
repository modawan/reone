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

#include "reone/game/effect/damage.h"

#include "reone/system/logutil.h"

#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damagereduction.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

static constexpr int kAllDamageTypes = 8199;
static constexpr int kPhysicalDamageTypes = 16391;
static constexpr int kDamageTypeCount = 15;

static bool damageTypeMatches(DamageType modifierType, DamageType damageType) {
    int modifierFlags = static_cast<int>(modifierType);
    int damageFlags = static_cast<int>(damageType);

    if (modifierFlags == kAllDamageTypes) {
        return true;
    }
    if (modifierFlags <= 0 || damageFlags == 0) {
        return false;
    }
    if (modifierFlags == kPhysicalDamageTypes) {
        return (damageFlags & static_cast<int>(DamageType::Physical)) != 0;
    }
    return (modifierFlags & damageFlags) != 0;
}

static int getDamageImmunity(const Object &object, DamageType damageType) {
    int damageFlags = static_cast<int>(damageType);
    int result = 0;

    for (int bit = 0; bit < kDamageTypeCount; ++bit) {
        int typeFlag = 1 << bit;
        if ((damageFlags & typeFlag) == 0) {
            continue;
        }

        auto type = static_cast<DamageType>(typeFlag);
        int immunity = 0;
        if (const auto *creature = dyn_cast<Creature>(&object)) {
            immunity = std::clamp(
                creature->getItemDamageImmunity(type),
                -100,
                100);
        }

        for (const Object::AppliedEffect &applied : object.effects()) {
            switch (applied.effect->type()) {
            case EffectType::DamageImmunityIncrease: {
                const auto &effect =
                    static_cast<const DamageImmunityIncreaseEffect &>(*applied.effect);
                if (effect.active() &&
                    damageTypeMatches(effect.damageType(), type)) {
                    immunity = std::clamp(
                        immunity + effect.percentImmunity(),
                        -100,
                        100);
                }
                break;
            }
            case EffectType::DamageImmunityDecrease: {
                const auto &effect =
                    static_cast<const DamageImmunityDecreaseEffect &>(*applied.effect);
                if (effect.active() &&
                    damageTypeMatches(effect.damageType(), type)) {
                    immunity = std::clamp(
                        immunity - effect.percentImmunity(),
                        -100,
                        100);
                }
                break;
            }
            default:
                break;
            }
        }

        if (result == 0 || immunity < result) {
            result = immunity;
        }
    }

    return std::clamp(result, -100, 100);
}

static int applyDamageImmunity(
    const Object &object,
    DamageType damageType,
    int damage) {

    if (damage <= 0) {
        return 0;
    }

    int immunity = getDamageImmunity(object, damageType);
    if (immunity > 0) {
        int prevented = std::max(1, damage * immunity / 100);
        return std::max(0, damage - prevented);
    }
    return damage - damage * immunity / 100;
}

static int applyDamageResistance(
    Object &object,
    DamageType damageType,
    int damage) {

    if (damage <= 0) {
        return 0;
    }

    int resistance = 0;
    int featBonus = 0;
    if (const auto *creature = dyn_cast<Creature>(&object)) {
        resistance = creature->getItemDamageResistance(damageType);
        featBonus = creature->getDamageResistanceFeatBonus();
    }

    std::shared_ptr<DamageResistanceEffect> selectedEffect;
    for (const Object::AppliedEffect &applied : object.effects()) {
        if (applied.effect->type() != EffectType::DamageResistance) {
            continue;
        }

        auto effect = std::static_pointer_cast<DamageResistanceEffect>(applied.effect);
        if (!damageTypeMatches(effect->damageType(), damageType) ||
            effect->amount() <= resistance) {
            continue;
        }

        resistance = effect->amount();
        selectedEffect = std::move(effect);
    }

    int prevented = selectedEffect
                        ? selectedEffect->absorb(damage)
                        : std::min(damage, resistance);
    if (selectedEffect && selectedEffect->exhausted()) {
        object.removeEffect(selectedEffect);
    }

    return std::max(0, damage - prevented - featBonus);
}

static int applyDamageReduction(
    Object &object,
    DamageType damageType,
    DamagePower damagePower,
    int damage) {

    if (damage <= 0) {
        return 0;
    }
    if (!damageTypeMatches(
            static_cast<DamageType>(kPhysicalDamageTypes),
            damageType)) {
        return damage;
    }

    int reduction = 0;
    DamagePower requiredPower = DamagePower::Normal;
    if (const auto *creature = dyn_cast<Creature>(&object)) {
        creature->getItemDamageReduction(reduction, requiredPower);
    }

    std::shared_ptr<DamageReductionEffect> selectedEffect;
    for (const Object::AppliedEffect &applied : object.effects()) {
        if (applied.effect->type() != EffectType::DamageReduction) {
            continue;
        }

        auto effect = std::static_pointer_cast<DamageReductionEffect>(applied.effect);
        if (effect->amount() <= reduction) {
            continue;
        }

        reduction = effect->amount();
        requiredPower = effect->damagePower();
        selectedEffect = std::move(effect);
    }

    int prevented = selectedEffect
                        ? selectedEffect->absorb(damage)
                        : std::min(damage, reduction);
    if (selectedEffect && selectedEffect->exhausted()) {
        object.removeEffect(selectedEffect);
    }

    if (static_cast<int>(damagePower) >= static_cast<int>(requiredPower)) {
        return damage;
    }
    return std::max(0, damage - prevented);
}

void DamagePacket::addResolved(int amount, DamageType type) {
    for (DamageComponent &component : _components) {
        if (component.type == type) {
            component.amount = std::max(component.amount + amount, 1);
            return;
        }
    }
    if (amount > 0) {
        _components.push_back({amount, type});
    }
}

void DamagePacket::add(int amount, DamageType type) {
    if (_mitigated || amount == 0) {
        return;
    }

    addResolved(amount, type);
}

void DamagePacket::addBaseDamage(int amount, DamageType type) {
    if (_mitigated || amount <= 0) {
        return;
    }

    if (_baseDamage == 0) {
        _baseDamageType = type;
    }
    _baseDamage += amount;
    addResolved(amount, type);
}

void DamagePacket::setPower(DamagePower power) {
    if (static_cast<int>(power) > static_cast<int>(_power)) {
        _power = power;
    }
}

void DamagePacket::mitigate(Object &object) {
    if (_mitigated) {
        return;
    }
    _mitigated = true;

    SmallVector<DamageComponent, 4> components(std::move(_components));
    _components.resize(0);
    if (object.plotFlag()) {
        _baseDamage = 0;
        return;
    }

    std::sort(
        components.begin(),
        components.end(),
        [](const DamageComponent &lhs, const DamageComponent &rhs) {
            return static_cast<int>(lhs.type) < static_cast<int>(rhs.type);
        });

    int finalBaseDamage = 0;
    for (const DamageComponent &component : components) {
        int amount = applyDamageImmunity(
            object,
            component.type,
            component.amount);
        amount = applyDamageResistance(
            object,
            component.type,
            amount);

        int basePortion = component.type == _baseDamageType
                              ? std::min(_baseDamage, component.amount)
                              : 0;
        int survivingBase = basePortion > 0
                                ? std::min(
                                      amount,
                                      static_cast<int>(
                                          static_cast<int64_t>(basePortion) * amount /
                                          component.amount))
                                : 0;
        if (survivingBase > 0) {
            int reducedBase = applyDamageReduction(
                object,
                component.type,
                _power,
                survivingBase);
            amount -= survivingBase - reducedBase;
            survivingBase = reducedBase;
        }

        finalBaseDamage += survivingBase;
        addResolved(amount, component.type);
    }

    _baseDamage = finalBaseDamage;
}

int DamagePacket::total() const {
    int result = 0;
    for (const DamageComponent &component : _components) {
        result += component.amount;
    }
    return result;
}

void DamageEffect::applyTo(Object &object) {
    _damage.mitigate(object);

    int amount = _damage.total();
    debug(str(boost::format("Damage taken: %s %d") % object.tag() % amount));
    object.damage(amount, _damager);
}

} // namespace game

} // namespace reone
