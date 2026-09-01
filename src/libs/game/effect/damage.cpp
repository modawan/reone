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
#include "reone/game/object.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

static constexpr int kDamageTypeCount = 15;

struct AbsorptionResult {
    int prevented {0};
    bool exhausted {false};
};

static AbsorptionResult absorbDamage(
    EffectInstance &effect,
    int damage,
    size_t amountParameter,
    size_t limitParameter) {
    int amount = effect.integerParameter(amountParameter);
    int limit = effect.integerParameter(limitParameter);
    if (damage <= 0 || amount <= 0) {
        return {};
    }
    if (limit <= 0) {
        return {std::min(damage, amount), false};
    }

    int prevented = std::min({damage, amount, limit});
    effect.integerParameters[limitParameter] = std::max(0, limit - damage);
    return {prevented, effect.integerParameters[limitParameter] == 0};
}

DamageType getPrimaryDamageType(int damageFlags) {
    assert(damageFlags > 0);

    int type = 1;
    while (damageFlags > 1) {
        damageFlags >>= 1;
        type <<= 1;
    }
    return static_cast<DamageType>(type);
}

bool damageTypeMatches(int modifierFlags, int damageFlags) {
    if (modifierFlags == kAllDamageTypeFlags) {
        return true;
    }
    if (modifierFlags <= 0 || damageFlags == 0) {
        return false;
    }
    if (modifierFlags == kPhysicalDamageTypeFlags) {
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

        for (const EffectInstance &applied : object.effects()) {
            switch (applied.type()) {
            case EffectType::DamageImmunityIncrease: {
                if (damageTypeMatches(
                        applied.integerParameter(0),
                        static_cast<int>(type))) {
                    immunity = std::clamp(
                        immunity + applied.integerParameter(1),
                        -100,
                        100);
                }
                break;
            }
            case EffectType::DamageImmunityDecrease: {
                if (damageTypeMatches(
                        applied.integerParameter(0),
                        static_cast<int>(type))) {
                    immunity = std::clamp(
                        immunity - applied.integerParameter(1),
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

    std::shared_ptr<Effect> selectedEffect;
    for (const EffectInstance &applied : object.effects()) {
        if (applied.type() != EffectType::DamageResistance) {
            continue;
        }
        int amount = applied.integerParameter(1);
        if (!damageTypeMatches(
                applied.integerParameter(0),
                static_cast<int>(damageType)) ||
            amount <= resistance) {
            continue;
        }

        resistance = amount;
        selectedEffect = applied.effect;
    }

    int prevented = std::min(damage, resistance);
    if (selectedEffect) {
        EffectInstance *instance = object.findEffectInstance(*selectedEffect);
        if (instance) {
            AbsorptionResult absorption = absorbDamage(*instance, damage, 1, 2);
            prevented = absorption.prevented;
            if (absorption.exhausted) {
                object.removeEffect(selectedEffect);
            }
        }
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
            kPhysicalDamageTypeFlags,
            static_cast<int>(damageType))) {
        return damage;
    }

    int reduction = 0;
    DamagePower requiredPower = DamagePower::Normal;
    if (const auto *creature = dyn_cast<Creature>(&object)) {
        creature->getItemDamageReduction(reduction, requiredPower);
    }

    std::shared_ptr<Effect> selectedEffect;
    for (const EffectInstance &applied : object.effects()) {
        if (applied.type() != EffectType::DamageReduction) {
            continue;
        }
        int amount = applied.integerParameter(0);
        if (amount <= reduction) {
            continue;
        }

        reduction = amount;
        requiredPower = static_cast<DamagePower>(
            applied.integerParameter(1));
        selectedEffect = applied.effect;
    }

    int prevented = std::min(damage, reduction);
    if (selectedEffect) {
        EffectInstance *instance = object.findEffectInstance(*selectedEffect);
        if (instance) {
            AbsorptionResult absorption = absorbDamage(*instance, damage, 0, 2);
            prevented = absorption.prevented;
            if (absorption.exhausted) {
                object.removeEffect(selectedEffect);
            }
        }
    }

    if (static_cast<int>(damagePower) >= static_cast<int>(requiredPower)) {
        return damage;
    }
    return std::max(0, damage - prevented);
}

void DamagePacket::requireUnresolved() const {
    if (isResolved()) {
        throw std::logic_error("Damage packet has already been resolved");
    }
}

void DamagePacket::addResolved(int amount, DamageType type) {
    for (Component &component : _components) {
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
    requireUnresolved();
    if (amount == 0) {
        return;
    }

    addResolved(amount, type);
}

void DamagePacket::setDamageFlags(int damageFlags) {
    requireUnresolved();
    if (damageFlags == 0) {
        throw std::invalid_argument("Damage flags must not be zero");
    }
    _damageFlags = damageFlags;
}

void DamagePacket::setPower(DamagePower power) {
    requireUnresolved();
    if (static_cast<int>(power) > static_cast<int>(_power)) {
        _power = power;
    }
}

void DamagePacket::resolve(Object &object) {
    requireUnresolved();
    if (_damageFlags == 0) {
        throw std::logic_error("Damage packet has no damage flags");
    }

    int amount = total();
    if (object.plotFlag()) {
        _resolvedDamage = 0;
        return;
    }

    auto damageType = static_cast<DamageType>(_damageFlags);
    amount = applyDamageImmunity(object, damageType, amount);
    amount = applyDamageResistance(object, damageType, amount);
    amount = applyDamageReduction(object, damageType, _power, amount);
    _resolvedDamage = amount;
}

int DamagePacket::resolvedDamage() const {
    if (!_resolvedDamage) {
        throw std::logic_error("Damage packet has not been resolved");
    }
    return *_resolvedDamage;
}

int DamagePacket::total() const {
    int result = 0;
    for (const Component &component : _components) {
        result += component.amount;
    }
    return result;
}

void DamageEffect::applyTo(Object &object) {
    if (!_damage.isResolved()) {
        _damage.resolve(object);
    }

    int amount = _damage.resolvedDamage();
    debug(str(boost::format("Damage taken: %s %d") % object.tag() % amount));
    object.damage(amount, _damager);
}

} // namespace game

} // namespace reone
