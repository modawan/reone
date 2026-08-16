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
#include "reone/game/game.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

static constexpr int kDamageTypeCount = 15;

static int applyNativeDamageImmunityDelta(int current, long long delta) {
    // Native processing performs each immunity adjustment with addb/subb, sign-extends the
    // resulting byte, and only then applies the setter's [-100, 100] clamp.
    // Preserve that per-effect truncation instead of accumulating in an int.
    long long raw = static_cast<long long>(current) + delta;
    int byte = static_cast<int>(raw % 0x100);
    if (byte < 0) {
        byte += 0x100;
    }
    int signedByte = byte < 0x80 ? byte : byte - 0x100;
    return std::clamp(signedByte, -100, 100);
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

        std::vector<const Object::AppliedEffect *> modifiers;
        for (const Object::AppliedEffect &applied : object.effects()) {
            if (applied.effect->type() == EffectType::DamageImmunityIncrease ||
                applied.effect->type() == EffectType::DamageImmunityDecrease) {
                modifiers.push_back(&applied);
            }
        }
        std::sort(
            modifiers.begin(),
            modifiers.end(),
            [](const Object::AppliedEffect *left,
               const Object::AppliedEffect *right) {
                return left->applicationOrder < right->applicationOrder;
            });

        for (const Object::AppliedEffect *applied : modifiers) {
            switch (applied->effect->type()) {
            case EffectType::DamageImmunityIncrease: {
                const auto &effect =
                    static_cast<const DamageImmunityIncreaseEffect &>(*applied->effect);
                if (damageTypeMatches(
                        static_cast<int>(effect.damageType()),
                        static_cast<int>(type))) {
                    immunity = applyNativeDamageImmunityDelta(
                        immunity,
                        static_cast<long long>(effect.percentImmunity()));
                }
                break;
            }
            case EffectType::DamageImmunityDecrease: {
                const auto &effect =
                    static_cast<const DamageImmunityDecreaseEffect &>(*applied->effect);
                if (damageTypeMatches(
                        static_cast<int>(effect.damageType()),
                        static_cast<int>(type))) {
                    immunity = applyNativeDamageImmunityDelta(
                        immunity,
                        -static_cast<long long>(effect.percentImmunity()));
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
    int damage,
    DamageResolution &resolution) {

    if (damage <= 0) {
        resolution.damageAfterImmunity = 0;
        return 0;
    }

    int immunity = getDamageImmunity(object, damageType);
    resolution.immunityPercent = immunity;

    int result;
    if (immunity > 0) {
        int prevented = std::max(1, damage * immunity / 100);
        result = std::max(0, damage - prevented);
        resolution.immunityPrevented = damage - result;
        resolution.mitigationFeedback.push_back({
            MitigationFeedbackType::DamageImmunity,
            resolution.immunityPrevented,
            std::nullopt,
            static_cast<int>(damageType),
        });
    } else {
        result = damage - damage * immunity / 100;
        resolution.vulnerabilityAdded = result - damage;
    }

    resolution.damageAfterImmunity = result;
    return result;
}

static int applyDamageResistance(
    Object &object,
    DamageType damageType,
    int damage,
    DamageResolution &resolution) {

    int resistance = 0;
    int featBonus = 0;
    if (const auto *creature = dyn_cast<Creature>(&object)) {
        creature->getDamageResistanceFeatBonuses(
            resolution.improvedToughnessBonus,
            resolution.wookieeEnduranceBonus);
        featBonus = resolution.improvedToughnessBonus +
                    resolution.wookieeEnduranceBonus;
    }

    bool secondaryQualifier = false;
    std::shared_ptr<DamageResistanceEffect> selectedEffect;
    for (const Object::AppliedEffect &applied : object.effects()) {
        if (applied.effect->type() != EffectType::DamageResistance) {
            continue;
        }

        auto effect = std::static_pointer_cast<DamageResistanceEffect>(applied.effect);
        if (damageTypeMatches(
                effect->secondaryDamageFlags(),
                static_cast<int>(damageType))) {
            secondaryQualifier = true;
        }
        if (!damageTypeMatches(
                static_cast<int>(effect->damageType()),
                static_cast<int>(damageType)) ||
            effect->amount() <= resistance) {
            continue;
        }

        resistance = effect->amount();
        selectedEffect = std::move(effect);
    }

    resolution.resistanceAmount = resistance;
    int consumptionDamage = damage * (secondaryQualifier ? 2 : 1);
    std::optional<int> preHitPool = selectedEffect
                                        ? selectedEffect->remainingLimit()
                                        : std::nullopt;
    int effectiveResistance = resistance;
    if (preHitPool && *preHitPool <= consumptionDamage) {
        effectiveResistance = *preHitPool;
    }

    int reportedAmount = selectedEffect
                             ? selectedEffect->absorb(consumptionDamage)
                             : std::min(consumptionDamage, resistance);
    if (selectedEffect) {
        resolution.resistancePoolRemaining = selectedEffect->remainingLimit();
        if (selectedEffect->exhausted()) {
            object.removeEffect(selectedEffect);
        }
    }

    if (resistance > 0) {
        resolution.mitigationFeedback.push_back({
            preHitPool
                ? MitigationFeedbackType::FiniteDamageResistance
                : MitigationFeedbackType::DamageResistance,
            reportedAmount,
            preHitPool
                ? std::optional<int>(*preHitPool - reportedAmount)
                : std::nullopt,
            0,
        });
    }

    int prevented = std::min(std::max(damage, 0), effectiveResistance);
    resolution.resistancePrevented = prevented;
    int remaining = std::max(0, damage - prevented);
    int result = std::max(0, damage - prevented - featBonus);
    resolution.resistanceFeatPrevented = remaining - result;
    resolution.damageAfterResistance = result;
    return result;
}

static int applyDamageReduction(
    Object &object,
    DamageType damageType,
    DamagePower damagePower,
    int damage,
    DamageResolution &resolution) {

    if (!damageTypeMatches(
            kPhysicalDamageTypeFlags,
            static_cast<int>(damageType))) {
        return damage;
    }

    int reduction = 0;
    DamagePower requiredPower = DamagePower::Normal;
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

    resolution.reductionAmount = reduction;
    resolution.reductionPower = requiredPower;
    std::optional<int> preHitPool = selectedEffect
                                        ? selectedEffect->remainingLimit()
                                        : std::nullopt;
    int reportedAmount = selectedEffect
                             ? selectedEffect->absorb(damage)
                             : std::min(damage, reduction);
    if (selectedEffect) {
        resolution.reductionPoolRemaining = selectedEffect->remainingLimit();
        if (selectedEffect->exhausted()) {
            object.removeEffect(selectedEffect);
        }
    }

    resolution.reductionBypassed =
        reduction > 0 &&
        static_cast<int>(damagePower) >= static_cast<int>(requiredPower);
    if (resolution.reductionBypassed) {
        return damage;
    }

    if (reduction > 0) {
        resolution.mitigationFeedback.push_back({
            preHitPool
                ? MitigationFeedbackType::FiniteDamageReduction
                : MitigationFeedbackType::DamageReduction,
            reportedAmount,
            preHitPool
                ? std::optional<int>(*preHitPool - reportedAmount)
                : std::nullopt,
            0,
        });
    }

    int result = std::max(0, damage - reportedAmount);
    resolution.reductionPrevented = damage - result;
    return result;
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

    DamageResolution result;
    result.damageFlags = _damageFlags;
    result.damagePower = _power;
    result.rawDamage = total();
    if (object.plotFlag()) {
        result.plotSuppressed = true;
        _resolution.emplace(std::move(result));
        return;
    }

    auto damageType = static_cast<DamageType>(_damageFlags);
    int amount = applyDamageImmunity(
        object,
        damageType,
        result.rawDamage,
        result);
    amount = applyDamageResistance(
        object,
        damageType,
        amount,
        result);
    amount = applyDamageReduction(
        object,
        damageType,
        _power,
        amount,
        result);
    result.finalDamage = amount;
    _resolution.emplace(std::move(result));
}

const DamageResolution &DamagePacket::resolution() const {
    if (!_resolution) {
        throw std::logic_error("Damage packet has not been resolved");
    }
    return *_resolution;
}

int DamagePacket::resolvedDamage() const {
    return resolution().finalDamage;
}

int DamagePacket::total() const {
    int result = 0;
    for (const Component &component : _components) {
        result += component.amount;
    }
    return result;
}

void DamageEffect::applyTo(Object &object) {
    // Native OnApplyDamage decides whether to enter the effect path from the
    // raw fifteen-slot payload. A raw-positive attack that mitigation reduces
    // to zero must still publish LastDamager/slot state and run OnDamaged.
    int entryAmount = _damage.total();
    if (entryAmount == 0 && !object.plotFlag()) {
        return;
    }

    if (!_damage.isResolved()) {
        _damage.resolve(object);
    }

    int amount = object.game().scaleDamageForDifficulty(
        _damage.resolvedDamage(),
        object);
    std::array<int16_t, 15> damageAmounts = _context.damageAmounts;
    if (_context.preResolved && damageAmounts.back() >= 0) {
        damageAmounts.back() = static_cast<int16_t>(amount);
    }
    if (object.plotFlag() || (!_context.preResolved && amount == 0)) {
        for (int16_t &value : damageAmounts) {
            if (value >= 0) {
                value = 0;
            }
        }
    }

    debug(str(boost::format("Damage taken: %s %d") % object.tag() % amount));
    object.applyDamageEffect(amount, _damager, damageAmounts);
}

} // namespace game

} // namespace reone
