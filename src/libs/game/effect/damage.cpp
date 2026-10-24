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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "reone/game/combatfeedback.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/damageforcepoints.h"
#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damagereduction.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/death.h"
#include "reone/game/effect/forcedrain.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/healforcepoints.h"
#include "reone/game/effect/hitpointchangewhendying.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

// The damage event slot that holds a hit's total.
static constexpr size_t kTotalSlot = DamageEffect::kTotal;

// A continuation saves this many values of a resolved packet.
static constexpr size_t kResolutionValueCount = 20;

// A destroyed door or placeable's death event, and how long after it the
// object is destroyed.
static constexpr uint16_t kDeathScriptEvent = 10;
static constexpr uint64_t kDestroyDelayMilliseconds = 2000;

// The doors a destroyed door is linked to: another door or a waypoint.
static constexpr int kLinkedToDoor = 1;
static constexpr int kLinkedToWaypoint = 2;

// A dying player character loses or gains a hit point every round.
static constexpr float kRoundSeconds = 6.0f;
static constexpr float kMinimumDyingRate = 0.1f;

// The blood a creature bleeds as it loses vitality while dying (visualeffects.2da rows).
static constexpr int kRedBloodVisual = 158;
static constexpr int kGreenBloodVisual = 159;
static constexpr int kYellowBloodVisual = 160;

namespace {

class DamageAbsorption {
public:
    DamageAbsorption(int amount, int limit) :
        _amount(amount),
        _limit(limit),
        _limited(limit > 0) {
    }

    int amount() const { return _amount; }

    int absorb(int damage) {
        if (damage <= 0 || _amount <= 0) {
            return 0;
        }
        if (!_limited) {
            return std::min(damage, _amount);
        }

        int remaining = _limit;
        _limit = std::max(0, _limit - damage);
        return std::min(damage, _limit == 0 ? remaining : _amount);
    }

    bool exhausted() const { return _limited && _limit == 0; }
    std::optional<int> remainingLimit() const {
        if (!_limited) {
            return std::nullopt;
        }
        return _limit;
    }

private:
    int _amount;
    int _limit;
    bool _limited;
};

} // namespace

std::shared_ptr<resource::Gff> DamagePacket::saveContinuation() const {
    using G = resource::Gff;
    using F = G::Field;
    std::vector<std::shared_ptr<G>> components;
    std::vector<std::shared_ptr<G>> resolution;
    std::vector<std::shared_ptr<G>> feedback;
    std::vector<std::shared_ptr<G>> baseAmounts;
    for (int amount : _baseDamageAmounts) {
        baseAmounts.push_back(G::Builder().type(0).field(F::newInt("Amount", amount)).build());
    }
    for (const auto &component : _components) {
        components.push_back(G::Builder()
                                 .type(0)
                                 .field(F::newInt("Amount", component.amount))
                                 .field(F::newInt("Type", static_cast<int>(component.type)))
                                 .field(F::newByte("Bonus", component.bonus))
                                 .build());
    }
    auto result = G::Builder()
                      .type(0)
                      .field(F::newInt("Power", static_cast<int>(_power)))
                      .field(F::newInt("Flags", _damageFlags))
                      .field(F::newList("Components", std::move(components)))
                      .field(F::newList("BaseAmounts", std::move(baseAmounts)))
                      .field(F::newByte("CutsDoors", _cutsDoors))
                      .build();
    if (_resolution) {
        const auto &r = *_resolution;
        std::vector<std::shared_ptr<G>> amounts;
        for (int amount : r.damageAmounts) {
            amounts.push_back(G::Builder().type(0).field(F::newInt("Amount", amount)).build());
        }
        result->fields().push_back(F::newList("ResolvedAmounts", std::move(amounts)));
        const std::array<int, kResolutionValueCount> values {
            r.damageFlags,
            static_cast<int>(r.damagePower),
            r.rawDamage,
            static_cast<int>(r.plotSuppressed),
            r.immunityPercent,
            r.immunityPrevented,
            r.vulnerabilityAdded,
            r.damageAfterImmunity,
            r.resistanceAmount,
            r.resistancePrevented,
            r.resistanceFeatPrevented,
            r.percentageResistanceBonus,
            r.improvedToughnessBonus,
            r.wookieeEnduranceBonus,
            r.damageAfterResistance,
            r.reductionAmount,
            static_cast<int>(r.reductionPower),
            static_cast<int>(r.reductionBypassed),
            r.reductionPrevented,
            r.finalDamage};
        for (int value : values) {
            resolution.push_back(G::Builder().type(0).field(F::newInt("Value", value)).build());
        }
        for (const auto &line : r.mitigationFeedback) {
            auto entry = G::Builder()
                             .type(0)
                             .field(F::newInt("Type", static_cast<int>(line.type)))
                             .field(F::newInt("Amount", line.amount))
                             .field(F::newInt("Flags", line.damageFlags))
                             .build();
            if (line.remaining) {
                entry->fields().push_back(F::newInt("Remaining", *line.remaining));
            }
            feedback.push_back(std::move(entry));
        }
        result->fields().push_back(F::newList("Resolution", std::move(resolution)));
        result->fields().push_back(F::newList("Feedback", std::move(feedback)));
        if (r.resistancePoolRemaining) {
            result->fields().push_back(F::newInt("ResistPool", *r.resistancePoolRemaining));
        }
        if (r.reductionPoolRemaining) {
            result->fields().push_back(F::newInt("ReducePool", *r.reductionPoolRemaining));
        }
    }
    return result;
}

DamagePacket DamagePacket::restoreContinuation(const resource::Gff &gff) {
    DamagePacket result {static_cast<DamagePower>(gff.getInt("Power"))};
    result._damageFlags = gff.getInt("Flags");
    result._cutsDoors = gff.getBool("CutsDoors");
    const auto &baseAmounts = gff.getList("BaseAmounts");
    for (size_t i = 0; i < result._baseDamageAmounts.size(); ++i) {
        result._baseDamageAmounts[i] = baseAmounts.at(i)->getInt("Amount");
    }
    for (const auto &component : gff.getList("Components")) {
        result._components.push_back({component->getInt("Amount"),
                                      static_cast<DamageType>(component->getInt("Type")),
                                      component->getBool("Bonus")});
    }
    const auto &values = gff.getList("Resolution");
    if (values.size() == kResolutionValueCount) {
        auto &r = result._resolution.emplace();
        const auto &amounts = gff.getList("ResolvedAmounts");
        for (size_t i = 0; i < r.damageAmounts.size(); ++i) {
            r.damageAmounts[i] = amounts.at(i)->getInt("Amount");
        }
        const auto value = [&values](size_t index) {
            return values[index]->getInt("Value");
        };
        r.damageFlags = value(0);
        r.damagePower = static_cast<DamagePower>(value(1));
        r.rawDamage = value(2);
        r.plotSuppressed = value(3) != 0;
        r.immunityPercent = value(4);
        r.immunityPrevented = value(5);
        r.vulnerabilityAdded = value(6);
        r.damageAfterImmunity = value(7);
        r.resistanceAmount = value(8);
        r.resistancePrevented = value(9);
        r.resistanceFeatPrevented = value(10);
        r.percentageResistanceBonus = value(11);
        r.improvedToughnessBonus = value(12);
        r.wookieeEnduranceBonus = value(13);
        r.damageAfterResistance = value(14);
        r.reductionAmount = value(15);
        r.reductionPower = static_cast<DamagePower>(value(16));
        r.reductionBypassed = value(17) != 0;
        r.reductionPrevented = value(18);
        r.finalDamage = value(19);
        int pool;
        if (gff.readInt(pool, "ResistPool")) {
            r.resistancePoolRemaining = pool;
        }
        if (gff.readInt(pool, "ReducePool")) {
            r.reductionPoolRemaining = pool;
        }
        for (const auto &entry : gff.getList("Feedback")) {
            MitigationFeedback line {static_cast<MitigationFeedbackType>(entry->getInt("Type")),
                                     entry->getInt("Amount"),
                                     std::nullopt,
                                     entry->getInt("Flags")};
            int remaining;
            if (entry->readInt(remaining, "Remaining")) {
                line.remaining = remaining;
            }
            r.mitigationFeedback.push_back(std::move(line));
        }
    }
    return result;
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

    int immunity = object.damageImmunity(static_cast<int>(damageType));
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

// The pool a resistance or reduction has left, when it has one.
static std::optional<int> remainingPool(const EffectInstance &effect) {
    if (effect.integerParameter(2) <= 0) {
        return std::nullopt;
    }
    return effect.integerParameter(2);
}

static int consumePool(
    Object &object, uint64_t order, int damage, size_t amountParameter,
    std::optional<int> &remaining, bool simulate) {
    auto &effect = object.findEffectApplication(order)->get();
    DamageAbsorption absorption {effect.integerParameter(amountParameter), effect.integerParameter(2)};
    int prevented = absorption.absorb(damage);
    // A simulated hit leaves the pool and its effect as they are.
    if (simulate) {
        return prevented;
    }
    remaining = absorption.remainingLimit();
    if (remaining) {
        effect.setIntegerParameter(2, *remaining);
    }
    const EffectId id = effect.id;
    if (absorption.exhausted()) {
        object.removeEffectsById(id);
    }
    return prevented;
}

static int applyDamageResistance(
    Object &object,
    DamageType damageType,
    int damage,
    DamageResolution &resolution,
    bool recordFeatReduction = true,
    std::optional<std::reference_wrapper<const Creature>> versus = std::nullopt,
    std::optional<std::reference_wrapper<const Creature>> source = std::nullopt,
    bool simulate = false) {

    int resistance = 0;

    bool secondaryQualifier = false;
    const EffectInstance *selectedEffect = nullptr;
    for (const EffectInstance &applied : object.effects()) {
        if (!applied.hasLiveRuntimeSource() || applied.type() != EffectType::DamageResistance ||
            (versus && !applied.appliesVersus(versus))) {
            continue;
        }
        if (damageTypeMatches(applied.integerParameter(3), static_cast<int>(damageType))) {
            secondaryQualifier = true;
        }
        if (!damageTypeMatches(applied.integerParameter(0), static_cast<int>(damageType)) ||
            applied.integerParameter(1) <= resistance) {
            continue;
        }
        resistance = applied.integerParameter(1);
        selectedEffect = &applied;
    }
    resolution.resistanceAmount = resistance;
    int consumptionFactor = secondaryQualifier ? 2 : 1;
    // In TSL, a Power Attack spends a Force shield's resistance faster.
    if (object.game().isTSL() && selectedEffect && source &&
        selectedEffect->integerParameter(4) == 1) {
        switch (static_cast<FeatType>(source->get().currentCombatAttackType())) {
        case FeatType::PowerAttack:
        case FeatType::ImprovedPowerAttack:
        case FeatType::MasterPowerAttack:
            ++consumptionFactor;
            break;
        default:
            break;
        }
    }
    int consumptionDamage = damage * consumptionFactor;
    const auto preHitPool = selectedEffect ? remainingPool(*selectedEffect) : std::nullopt;
    int effectiveResistance = resistance;
    if (preHitPool && *preHitPool <= consumptionDamage) {
        effectiveResistance = *preHitPool;
    }
    int reportedAmount = selectedEffect
                             ? consumePool(object, selectedEffect->applicationOrder, consumptionDamage, 1,
                                           resolution.resistancePoolRemaining, simulate)
                             : std::min(consumptionDamage, resistance);

    // The resistance feats are counted after the resistance is spent: an
    // exhausted resistance can take bonus feats linked to it away.
    int featBonus = 0;
    if (const auto *creature = dyn_cast<Creature>(&object)) {
        DamageResolution featReduction;
        creature->getDamageResistanceFeatBonuses(damage, featReduction);
        featBonus = featReduction.percentageResistanceBonus +
                    featReduction.improvedToughnessBonus +
                    featReduction.wookieeEnduranceBonus;
        if (recordFeatReduction) {
            resolution.percentageResistanceBonus = featReduction.percentageResistanceBonus;
            resolution.improvedToughnessBonus = featReduction.improvedToughnessBonus;
            resolution.wookieeEnduranceBonus = featReduction.wookieeEnduranceBonus;
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
    if (recordFeatReduction) {
        resolution.resistanceFeatPrevented = remaining - result;
    }
    resolution.damageAfterResistance = result;
    return result;
}

static int applyDamageReduction(
    Object &object,
    DamageType damageType,
    DamagePower damagePower,
    int damage,
    DamageResolution &resolution,
    bool simulate = false) {

    int reduction = 0;
    DamagePower requiredPower = DamagePower::Normal;
    const EffectInstance *selectedEffect = nullptr;
    for (const EffectInstance &applied : object.effects()) {
        if (!applied.hasLiveRuntimeSource() || applied.type() != EffectType::DamageReduction ||
            applied.integerParameter(0) <= reduction) {
            continue;
        }
        reduction = applied.integerParameter(0);
        requiredPower = static_cast<DamagePower>(applied.integerParameter(1));
        selectedEffect = &applied;
    }
    resolution.reductionAmount = reduction;
    resolution.reductionPower = requiredPower;
    const auto preHitPool = selectedEffect ? remainingPool(*selectedEffect) : std::nullopt;
    int reportedAmount = selectedEffect
                             ? consumePool(object, selectedEffect->applicationOrder, damage, 0,
                                           resolution.reductionPoolRemaining, simulate)
                             : std::min(damage, reduction);

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

// A typed damage bonus meets the target's immunity and then its resistance.
static int applyDamageBonusMitigation(
    Object &object, DamageType damageType, int damage, DamageResolution &resolution,
    const Creature &source, bool simulate) {

    int amount = applyDamageImmunity(object, damageType, damage, resolution);
    return applyDamageResistance(object, damageType, amount, resolution, false, source, source, simulate);
}

int simulateDamageBonusMitigation(Object &object, const Creature &source, int damageFlags, int damage) {
    DamageResolution resolution;
    return applyDamageBonusMitigation(object, static_cast<DamageType>(damageFlags), damage, resolution, source, true);
}

void DamagePacket::resolve(Object &object, std::optional<std::reference_wrapper<const Creature>> source) {
    requireUnresolved();

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
        result,
        true,
        std::nullopt,
        source);
    // Only physical script damage is reduced; a weapon hit is reduced whatever its type.
    if (damageTypeMatches(kPhysicalDamageTypeFlags, _damageFlags)) {
        amount = applyDamageReduction(object, damageType, _power, amount, result);
    }
    result.finalDamage = amount;
    _resolution.emplace(std::move(result));
}

void DamagePacket::resolvePhysical(Object &object, const Creature &source, bool simulate) {
    requireUnresolved();
    DamageResolution result;
    result.damageFlags = _damageFlags;
    result.damagePower = _power;
    result.rawDamage = total();
    std::copy(_baseDamageAmounts.begin(), _baseDamageAmounts.end(), result.damageAmounts.begin());
    // A plot object takes no damage; measuring a hit against one still counts
    // its mitigation.
    if (!simulate && object.plotFlag()) {
        result.plotSuppressed = true;
        for (int &amount : result.damageAmounts) {
            if (amount >= 0) {
                amount = 0;
            }
        }
        result.damageAmounts[kTotalSlot] = 0;
        _resolution.emplace(std::move(result));
        return;
    }
    // The typed slot and the total each take a typed bonus. Negative bonuses
    // are not mitigated.
    auto addTyped = [&](size_t slot, int amount) {
        const int current = result.damageAmounts[slot];
        result.damageAmounts[slot] = current > 0 ? std::max(1, current + amount) : std::max(0, amount);
        const int total = result.damageAmounts[kTotalSlot];
        result.damageAmounts[kTotalSlot] = (total > 0 ? total : 0) + amount;
    };
    for (size_t slot = 0; slot < result.damageAmounts.size(); ++slot) {
        const auto type = static_cast<DamageType>(1 << slot);
        for (const auto &component : _components) {
            if (!component.bonus || component.type != type || component.amount <= 0) {
                continue;
            }
            DamageResolution bonus;
            int amount = applyDamageBonusMitigation(object, type, component.amount, bonus, source, simulate);
            for (const auto &entry : bonus.mitigationFeedback) {
                result.mitigationFeedback.push_back(entry);
            }
            addTyped(slot, amount);
        }
        for (const auto &component : _components) {
            if (component.bonus && component.type == type && component.amount < 0) {
                addTyped(slot, component.amount);
            }
        }
    }
    // Base damage is resolved after the typed bonuses. Both stages apply the
    // resistance feats; the base stage records them and also applies damage
    // reduction.
    int base = 0;
    for (const auto &component : _components) {
        if (!component.bonus) {
            base += component.amount;
        }
    }
    const auto type = static_cast<DamageType>(_damageFlags);
    if (base <= 0) {
        // A hit deals at least one point of its primary type, which the
        // typed slot shows before mitigation and the total does not count twice.
        const auto slot = getDirectDamageSlot(static_cast<int>(getPrimaryDamageType(_damageFlags)));
        const int current = result.damageAmounts[slot];
        result.damageAmounts[slot] = current > 0 ? current + 1 : 1;
        base = 1;
    }
    base = applyDamageImmunity(object, type, base, result);
    base = applyDamageResistance(object, type, base, result, true, source, source, simulate);
    base = applyDamageReduction(object, type, _power, base, result, simulate);
    // A door-cutting weapon takes a door or placeable's whole vitality.
    if (_cutsDoors && (isa<Door>(&object) || isa<Placeable>(&object))) {
        base = object.maxHitPoints();
    }
    base = std::max(0, base);
    const int accumulated = result.damageAmounts[kTotalSlot];
    result.finalDamage = base + (accumulated > 0 ? accumulated : 0);
    result.damageAmounts[kTotalSlot] = result.finalDamage;
    _resolution.emplace(std::move(result));
}

void DamagePacket::resolveReturned(Creature &shooter) {
    requireUnresolved();
    DamageResolution result;
    result.damageFlags = _damageFlags;
    result.damagePower = _power;
    result.rawDamage = total();
    std::copy(_baseDamageAmounts.begin(), _baseDamageAmounts.end(), result.damageAmounts.begin());
    for (const auto &component : _components) {
        if (!component.bonus || component.amount <= 0) {
            continue;
        }
        const auto slot = getDirectDamageSlot(static_cast<int>(component.type));
        if (slot >= kTotalSlot) {
            continue;
        }
        const int current = result.damageAmounts[slot];
        result.damageAmounts[slot] = (current > 0 ? current : 0) + component.amount;
    }
    const auto slot = getScriptDamageSlot(_damageFlags);
    if (shooter.plotFlag()) {
        result.plotSuppressed = true;
        for (int &amount : result.damageAmounts) {
            if (amount >= 0) {
                amount = 0;
            }
        }
        result.damageAmounts[slot] = 0;
        result.damageAmounts[kTotalSlot] = 0;
        _resolution.emplace(std::move(result));
        return;
    }
    const auto type = static_cast<DamageType>(_damageFlags);
    int amount = applyDamageResistance(shooter, type, result.rawDamage, result, true, shooter, shooter);
    amount = applyDamageImmunity(shooter, type, amount, result);
    result.finalDamage = amount;
    if (slot < kTotalSlot) {
        result.damageAmounts[slot] = result.finalDamage;
    }
    result.damageAmounts[kTotalSlot] = result.finalDamage;
    _resolution.emplace(std::move(result));
}

std::shared_ptr<DamageEffect> DamageEffect::fromShieldRetaliation(int amount, int shieldFlags) {
    EffectInstance payload;
    payload.integerParameters.assign(kSuppressShields + 1, 0);
    for (size_t i = 0; i < kSlotCount; ++i) {
        payload.setIntegerParameter(i, ApplicationContext::kAbsentDamageAmount);
    }
    // A shield of no type fills no slot.
    if (shieldFlags > 0) {
        const auto slot = getScriptDamageSlot(shieldFlags);
        if (slot < kSlotCount) {
            payload.setIntegerParameter(slot, amount);
        }
    }
    payload.setIntegerParameter(kReactionDelay, kDefaultReactionDelay);
    return std::make_shared<DamageEffect>(payload);
}

std::shared_ptr<DamageEffect> DamageEffect::fromResolvedAttackAmount(int amount, size_t slot) {
    EffectInstance payload;
    payload.integerParameters.assign(kFeedbackHandled + 1, 0);
    for (size_t i = 0; i < kSlotCount; ++i) {
        payload.setIntegerParameter(i, ApplicationContext::kAbsentDamageAmount);
    }
    payload.setIntegerParameter(slot, amount);
    payload.setIntegerParameter(kTotal, amount);
    payload.setIntegerParameter(kReactionDelay, 0);
    // The damage flags hold the slot, the power of two of the damage type.
    payload.setIntegerParameter(kDamageFlags, static_cast<int>(slot));
    payload.setIntegerParameter(kPreResolved, 1);
    payload.setIntegerParameter(kSuppressShields, 1);
    payload.setIntegerParameter(kFeedbackHandled, 0);
    return std::make_shared<DamageEffect>(payload);
}

void showMitigationFeedback(Object &object, const std::shared_ptr<Creature> &source,
                            const DamageResolution &resolution) {
    if (resolution.mitigationFeedback.empty()) {
        return;
    }
    std::vector<DeferredCombatFeedback> records {resolution.mitigationFeedback.begin(),
                                                 resolution.mitigationFeedback.end()};
    showCombatFeedback(object.game(), source, object, records);
}

EffectApplicationResult DamageEffect::onApply(Object &object, EffectInstance &instance) {
    if (instance.restoring || object.isDead()) {
        return EffectApplicationResult::Applied;
    }
    // A party member at no vitality takes no more damage.
    if (auto *creature = dyn_cast<Creature>(&object); creature && creature->isTemporarilyDead()) {
        return EffectApplicationResult::Applied;
    }
    // The application resolves a copy of the effect's amounts, so the effect
    // can be applied again; a hit already resolved is not mitigated again.
    auto application = resolveDamageEffect(instance, object.plotFlag(), [&](int raw, int flags) {
        DamagePacket packet {DamagePower::Normal};
        packet.add(raw, static_cast<DamageType>(flags));
        packet.setDamageFlags(flags);
        const auto source = std::dynamic_pointer_cast<Creature>(instance.boundCreator());
        if (source) {
            packet.resolve(object, *source);
        } else {
            packet.resolve(object);
        }
        // Damage other than a weapon hit reports its mitigation at once, to
        // the damaged object and the creature that caused it.
        showMitigationFeedback(object, source, packet.resolution());
        return packet.resolvedDamage();
    });
    if (!application) {
        return EffectApplicationResult::Applied;
    }

    // Damage other than a weapon hit makes the target react: electrical or dark
    // side damage jolts it for the effect's reaction time, any other type makes
    // it flinch.
    auto &damageAmounts = application->damageAmounts;
    std::optional<DamageReaction> reaction;
    if (!application->preResolved && application->amount > 0) {
        reaction = DamageReaction {
            damageAmounts[getDirectDamageSlot(static_cast<int>(DamageType::Electrical))] > 0 ||
                damageAmounts[getDirectDamageSlot(static_cast<int>(DamageType::DarkSide))] > 0,
            instance.integerParameter(kReactionDelay)};
    }
    int amount = object.game().scaleDamageForDifficulty(application->amount, object);
    if (application->preResolved && damageAmounts.back() >= 0) {
        damageAmounts.back() = amount;
    }
    if (!application->preResolved && amount == 0) {
        for (int &value : damageAmounts) {
            if (value >= 0) {
                value = 0;
            }
        }
    }

    const auto creator = instance.boundCreator();
    // Damage shields struck back by this hit see it as the last damage. It is
    // not set again afterwards: a hit they cause may have replaced it.
    object.setLastDamager(creator);
    // A creature records the damage's creator as its last hostile actor,
    // except for the damage of its own poison.
    auto *creature = dyn_cast<Creature>(&object);
    if (creature) {
        const bool ownPoison = creature->activePoisonEffectId() != kUnassignedEffectId &&
                               std::any_of(creature->effects().begin(), creature->effects().end(), [&](const auto &record) {
                                   return record.type() == EffectType::Poison && record.id == instance.id;
                               });
        if (!ownPoison) {
            creature->setLastHostileActor(creator ? creator->id() : script::kObjectInvalid);
        }
    }
    object.setLastDamageAmounts(damageAmounts);
    debug(str(boost::format("Damage taken: %s %d") % object.tag() % amount));
    if (application->preResolved && !application->suppressDamageShields) {
        auto attacker = std::dynamic_pointer_cast<Creature>(instance.boundCreator());
        if (creature && attacker) {
            creature->resolveDamageShields(*attacker);
        }
    }
    if (amount > 0 && creature) {
        creature->removeMindTrickEffects();
    }
    // The damage line goes to the party unless the hit that caused it has
    // already told it. A door or container always tells the effect's own
    // amounts.
    if (creature) {
        if (instance.integerParameter(kFeedbackHandled) == 0) {
            addDamageFeedback(object.game(), creator, object, damageAmounts);
        }
    } else if (isa<Door>(&object) || isa<Placeable>(&object)) {
        addDamageFeedback(object.game(), creator, object, application->effectAmounts);
    }
    if (object.isRuntimeLive()) {
        object.applyDamageEffect(amount, creator, reaction);
    }
    return EffectApplicationResult::Applied;
}

void DamagePacket::resolveUnmitigated() {
    requireUnresolved();
    DamageResolution result;
    result.damageFlags = _damageFlags;
    result.damagePower = _power;
    result.rawDamage = total();
    std::copy(_baseDamageAmounts.begin(), _baseDamageAmounts.end(), result.damageAmounts.begin());
    result.finalDamage = result.rawDamage;
    result.damageAmounts[kTotalSlot] = result.finalDamage;
    _resolution.emplace(std::move(result));
}

void DamagePacket::resolveLightsaberThrow(Object &object, const Creature &caster) {
    requireUnresolved();
    DamageResolution result;
    result.damageFlags = _damageFlags;
    result.damagePower = _power;
    result.rawDamage = total();
    auto type = static_cast<DamageType>(_damageFlags);
    int amount = result.rawDamage;
    if (isa<Creature>(&object)) {
        amount = applyDamageResistance(object, type, amount, result, true, caster, caster);
        amount = applyDamageImmunity(object, type, amount, result);
    }
    if (object.plotFlag()) {
        result.plotSuppressed = true;
        amount = 0;
    }
    result.finalDamage = amount;
    _resolution.emplace(std::move(result));
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
    // Damage masks are bit sets: ALL excludes base damage and PHYSICAL
    // includes it.
    return (modifierFlags & damageFlags) != 0;
}

void DamagePacket::requireUnresolved() const {
    assert(!isResolved() && "Damage packet has already been resolved");
}

void DamagePacket::addResolved(int amount, DamageType type, bool bonus) {
    for (Component &component : _components) {
        if (component.type == type && component.bonus == bonus) {
            component.amount = std::max(component.amount + amount, 1);
            return;
        }
    }
    if (amount > 0) {
        _components.push_back({amount, type, bonus});
    }
}

void DamagePacket::add(int amount, DamageType type) {
    requireUnresolved();
    if (amount == 0) {
        return;
    }

    addResolved(amount, type);
}

void DamagePacket::addPhysicalBase(int amount, DamageType type) {
    requireUnresolved();
    _components.push_back({amount, type, false});
}

void DamagePacket::addBonus(int amount, DamageType type) {
    requireUnresolved();
    if (amount == 0) {
        return;
    }
    // Increases and decreases of a type are kept apart; a decrease applies
    // after the type's increases.
    for (auto &component : _components) {
        if (component.bonus && component.type == type && (component.amount < 0) == (amount < 0)) {
            component.amount += amount;
            return;
        }
    }
    _components.push_back({amount, type, true});
}

void DamagePacket::setDamageFlags(int damageFlags) {
    requireUnresolved();
    _damageFlags = damageFlags;
}

void DamagePacket::setPower(DamagePower power) {
    requireUnresolved();
    if (static_cast<int>(power) > static_cast<int>(_power)) {
        _power = power;
    }
}

const DamageResolution &DamagePacket::resolution() const {
    assert(_resolution && "Damage packet has not been resolved");
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

EffectApplicationResult DeathEffect::onApply(Object &object, EffectInstance &instance) {
    if (instance.restoring) {
        return EffectApplicationResult::Applied;
    }
    if (auto *creature = dyn_cast<Creature>(&object)) {
        return creature->applyDeathEffect(instance.boundCreator(), instance.integerParameter(2) != 0, instance)
                   ? EffectApplicationResult::Applied
                   : EffectApplicationResult::Rejected;
    }
    auto *door = dyn_cast<Door>(&object);
    auto *placeable = dyn_cast<Placeable>(&object);
    if (!door && !placeable) {
        return EffectApplicationResult::Rejected;
    }
    if (object.plotFlag()) {
        return EffectApplicationResult::Applied;
    }
    if (object.isMinOneHP()) {
        object.setCurrentHitPoints(1);
        return EffectApplicationResult::Applied;
    }
    object.clearAllActions(true);
    if (placeable) {
        placeable->enterDestroyedState();
    }
    if (door) {
        door->enterDestroyedState();
        // A door linked to another door or to a waypoint destroys the door of
        // that tag in its area too.
        if (door->linkedToFlags() == kLinkedToDoor || door->linkedToFlags() == kLinkedToWaypoint) {
            auto linked = object.spatialArea()->get().getObjectByTag(door->linkedTo());
            auto linkedDoor = std::dynamic_pointer_cast<Door>(linked);
            if (linkedDoor && linkedDoor->state() != DoorState::Destroyed) {
                auto child = instance;
                child.effect = instance.effect->cloneEffect();
                child.setIntegerParameter(0, 0);
                child.setIntegerParameter(1, 1);
                linkedDoor->applyEffect(std::move(child));
            }
        }
    }
    // The death event and the destruction go through the module's event queue.
    auto module = object.game().module();
    const auto when = object.game().worldTimeMilliseconds();
    const auto dayLength = object.game().millisecondsPerWorldDay();
    SavedEventRecord signal;
    signal.day = static_cast<uint32_t>(when / dayLength);
    signal.time = static_cast<uint32_t>(when % dayLength);
    signal.object = SavedObjectReference::fromRuntimeId(object.id());
    signal.caller = SavedObjectReference::fromRuntimeId(instance.creatorId);
    signal.eventId = static_cast<uint32_t>(SavedEventType::SignalEvent);
    SavedScriptEvent deathEvent;
    deathEvent.type = kDeathScriptEvent;
    signal.payload = std::move(deathEvent);
    module->enqueueSaveEvent(std::move(signal));
    SavedEventRecord destroy;
    destroy.day = static_cast<uint32_t>((when + kDestroyDelayMilliseconds) / dayLength);
    destroy.time = static_cast<uint32_t>((when + kDestroyDelayMilliseconds) % dayLength);
    destroy.object = SavedObjectReference::fromRuntimeId(object.id());
    destroy.caller = SavedObjectReference::fromRuntimeId(instance.creatorId);
    destroy.eventId = static_cast<uint32_t>(SavedEventType::DestroyObject);
    module->enqueueSaveEvent(std::move(destroy));
    return EffectApplicationResult::Applied;
}

EffectApplicationResult HitPointChangeWhenDyingEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || !creature->isPC()) {
        return EffectApplicationResult::Rejected;
    }
    // An instant change sets a timed one going, which lasts the rounds a hit
    // point takes at the rate.
    const float rate = instance.floatParameters[0];
    if (instance.durationType() == DurationType::Instant && rate >= kMinimumDyingRate) {
        auto effect = std::make_shared<HitPointChangeWhenDyingEffect>(rate);
        auto child = effect->saveFacingInstance();
        child.effect = effect;
        child.setDuration(DurationType::Temporary, std::fabs(kRoundSeconds / rate));
        child.creatorId = instance.creatorId;
        child.creator = instance.creator;
        if (auto creator = instance.boundCreator()) {
            child.spellId = creator->effectSpellId();
        }
        child.restoring = instance.restoring;
        object.applyEffect(std::move(child));
    }
    return EffectApplicationResult::Retained;
}

void applyBloodVisual(Creature &creature) {
    const auto appearance = getRequiredTwoDA(creature.services().resource.twoDas, "appearance");
    const auto blood = appearance->getString(creature.appearance(), "bloodcolr");
    int visualId = 0;
    if (blood == "R") {
        visualId = kRedBloodVisual;
    } else if (blood == "G") {
        visualId = kGreenBloodVisual;
    } else if (blood == "Y") {
        visualId = kYellowBloodVisual;
    }
    auto effect = std::make_shared<VisualEffectMarkerEffect>(visualId);
    auto child = effect->saveFacingInstance();
    child.effect = effect;
    child.setDuration(DurationType::Instant, 0.0f);
    creature.applyEffect(std::move(child));
}

EffectRemovalResult HitPointChangeWhenDyingEffect::onRemove(Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    // Anything but a creature has nothing to change.
    if (!creature) {
        return EffectRemovalResult::Removed;
    }
    if (creature->isPC()) {
        const float rate = instance.floatParameters[0];
        const int amount = creature->currentHitPointsWithoutTemporary() + (rate > 0.0f ? 1 : -1);
        creature->Object::setCurrentHitPoints(amount);
        if (!(rate > 0.0f)) {
            applyBloodVisual(*creature);
        }
    }
    if (creature->isDead() ||
        (object.game().party().isMember(object) && creature->currentHitPoints() <= 0)) {
        auto effect = std::make_shared<DeathEffect>(false, true, false);
        auto child = effect->saveFacingInstance();
        child.effect = effect;
        child.setDuration(DurationType::Instant, 0.0f);
        child.creatorId = instance.creatorId;
        child.creator = instance.creator;
        if (auto creator = instance.boundCreator()) {
            child.spellId = creator->effectSpellId();
        }
        object.applyEffect(std::move(child));
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult HealEffect::onApply(Object &object, EffectInstance &instance) {
    if (instance.restoring) {
        return EffectApplicationResult::Applied;
    }
    if (instance.integerParameter(1) == kForcePointsResource) {
        if (auto *creature = dyn_cast<Creature>(&object)) {
            creature->regenerateForcePoints(instance.integerParameter(0));
        }
    } else if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->applyHealingEffect(instance.integerParameter(0), instance.boundCreator(),
                                     instance.integerParameter(2) != 0);
    }
    return EffectApplicationResult::Applied;
}

EffectApplicationResult ResurrectionEffect::onApply(Object &object, EffectInstance &instance) {
    if (instance.restoring) {
        return EffectApplicationResult::Applied;
    }
    auto *creature = dyn_cast<Creature>(&object);
    return creature && creature->applyResurrectionEffect(instance.integerParameter(0))
               ? EffectApplicationResult::Applied
               : EffectApplicationResult::Rejected;
}

EffectApplicationResult DamageForcePointsEffect::onApply(Object &object, EffectInstance &instance) {
    if (instance.restoring) {
        return EffectApplicationResult::Applied;
    }
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Rejected;
    }
    creature->damageForcePoints(instance.integerParameter(0));
    return EffectApplicationResult::Applied;
}

EffectApplicationResult ForceDrainEffect::onApply(Object &object, EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->damageForcePoints(instance.integerParameter(0));
    }
    return EffectApplicationResult::Applied;
}

EffectApplicationResult HealForcePointsEffect::onApply(Object &object, EffectInstance &instance) {
    if (instance.restoring) {
        return EffectApplicationResult::Applied;
    }
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Rejected;
    }
    creature->healForcePoints(instance.integerParameter(0));
    return EffectApplicationResult::Applied;
}

} // namespace game

} // namespace reone
