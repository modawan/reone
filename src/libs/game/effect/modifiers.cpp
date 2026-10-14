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

#include <optional>

#include "reone/game/combatfeedback.h"
#include "reone/game/d20/class.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/abilityincrease.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/assureddeflection.h"
#include "reone/game/effect/assuredhit.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/blasterdeflectionincrease.h"
#include "reone/game/effect/bonusfeat.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damagedecrease.h"
#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damageincrease.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/disguise.h"
#include "reone/game/effect/factionmodifier.h"
#include "reone/game/effect/forceresistancedecrease.h"
#include "reone/game/effect/forceresistanceincrease.h"
#include "reone/game/effect/haste.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/modifyattacks.h"
#include "reone/game/effect/movementspeeddecrease.h"
#include "reone/game/effect/movementspeedincrease.h"
#include "reone/game/effect/purealignmentpowers.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/effect/skilldecrease.h"
#include "reone/game/effect/skillincrease.h"
#include "reone/game/effect/temporaryforcepoints.h"
#include "reone/game/effect/temporaryhitpoints.h"
#include "reone/game/effect/vpregenmodifier.h"
#include "reone/game/forcerules.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"

namespace reone {

namespace game {

// In KotOR, a creature with more extra attacks than this gains no more.
static constexpr int kMaxModifiedAttacks = 2;

// The most a creature's speed can be lowered, in percent.
static constexpr int kMaxMovementSpeedDecrease = 99;

EffectApplicationResult AbilityIncreaseEffect::onApply(Object &object, EffectInstance &instance) {
    // A restored effect is kept on the dead and dying; the other checks apply
    // as ever, whatever the effect's duration.
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Rejected;
    }
    return admitsAbilityEffect(!instance.restoring && creature->isDead(),
                               !instance.restoring && creature->isTemporarilyDead(),
                               instance.integerParameter(1), false, creature->plotFlag())
               ? EffectApplicationResult::Retained
               : EffectApplicationResult::Rejected;
}

namespace {

enum class PureAlignmentClassGroup {
    None,
    Force,
    Strength,
    Vitality
};

// Consulars and their prestige classes draw on the Force, Guardians and theirs
// on strength, Sentinels and theirs on vitality. KotOR has only the base classes.
PureAlignmentClassGroup pureAlignmentClassGroup(ClassType clazz, bool tsl) {
    switch (clazz) {
    case ClassType::JediConsular:
        return PureAlignmentClassGroup::Force;
    case ClassType::JediGuardian:
        return PureAlignmentClassGroup::Strength;
    case ClassType::JediSentinel:
        return PureAlignmentClassGroup::Vitality;
    case ClassType::JediMaster:
    case ClassType::SithLord:
        return tsl ? PureAlignmentClassGroup::Force : PureAlignmentClassGroup::None;
    case ClassType::JediWeaponMaster:
    case ClassType::SithMarauder:
        return tsl ? PureAlignmentClassGroup::Strength : PureAlignmentClassGroup::None;
    case ClassType::JediWatchman:
    case ClassType::SithAssassin:
        return tsl ? PureAlignmentClassGroup::Vitality : PureAlignmentClassGroup::None;
    default:
        return PureAlignmentClassGroup::None;
    }
}

// The bonuses of the pure alignment powers; their damage bonus and poison
// immunity count against every race.
constexpr int kPureAlignmentAbilityBonus = 3;
constexpr int kPureEvilForcePoints = 50;
constexpr int kPureEvilDamageBonus = 8;
constexpr int kPureEvilDamageType = 3;
constexpr int kAllRacesSelector = 28;
constexpr int kPureGoodIcon = 60;
constexpr int kPureEvilIcon = 62;
constexpr int kKotorPureEvilIcon = 61;

} // namespace

EffectApplicationResult PureAlignmentPowersEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Rejected;
    }
    const bool tsl = object.game().isTSL();
    const bool good = type() == EffectType::PureGoodPowers;
    // The pools are saved relative to their maxima, so the powers top them up
    // again whenever they are applied, restored powers included.
    const auto child = [&instance](const std::shared_ptr<Effect> &effect) {
        auto result = instance.linkedChild(effect);
        result.markGeneratedForLoad();
        result.restoring = false;
        return result;
    };
    const auto raiseAbility = [&](Ability ability) {
        object.applyEffect(child(std::make_shared<AbilityIncreaseEffect>(ability, kPureAlignmentAbilityBonus)));
    };

    if (object.game().party().getLeader().get() == creature) {
        for (const auto &[clazz, level] : creature->attributes().classLevels()) {
            switch (pureAlignmentClassGroup(clazz->type(), tsl)) {
            case PureAlignmentClassGroup::Force:
                if (good) {
                    // Admitting the ability increase already moved current Force
                    // points with the maximum; the powers add the rise again.
                    const int before = creature->maxForcePoints();
                    raiseAbility(tsl ? Ability::Wisdom : Ability::Charisma);
                    creature->addCurrentForcePoints(creature->maxForcePoints() - before);
                } else {
                    creature->addCurrentForcePoints(kPureEvilForcePoints);
                }
                break;
            case PureAlignmentClassGroup::Strength:
                if (good) {
                    raiseAbility(Ability::Strength);
                } else {
                    // One increase for the main hand, one for the off hand.
                    for (int hand : {1, 2}) {
                        auto damage = child(std::make_shared<DamageIncreaseEffect>(
                            kPureEvilDamageBonus, static_cast<DamageType>(kPureEvilDamageType)));
                        damage.setIntegerParameter(2, kAllRacesSelector);
                        damage.setIntegerParameter(5, hand);
                        object.applyEffect(std::move(damage));
                    }
                }
                break;
            case PureAlignmentClassGroup::Vitality:
                if (good) {
                    // Admitting the Constitution increase already moved current
                    // vitality with the maximum; the powers add the rise again.
                    const int before = creature->maxHitPoints();
                    raiseAbility(Ability::Constitution);
                    creature->Object::setCurrentHitPoints(
                        creature->currentHitPointsWithoutTemporary() + creature->maxHitPoints() - before);
                } else if (tsl) {
                    raiseAbility(Ability::Dexterity);
                } else {
                    auto immunity = child(std::make_shared<ImmunityEffect>(ImmunityType::Poison));
                    immunity.setIntegerParameter(1, kAllRacesSelector);
                    object.applyEffect(std::move(immunity));
                }
                break;
            case PureAlignmentClassGroup::None:
                break;
            }
        }
    }

    const int icon = good ? kPureGoodIcon : (tsl ? kPureEvilIcon : kKotorPureEvilIcon);
    object.applyEffect(child(std::make_shared<EffectIconMarkerEffect>(icon)));
    return EffectApplicationResult::Retained;
}

EffectApplicationResult AbilityDecreaseEffect::onApply(Object &object, EffectInstance &instance) {
    // A restored effect is kept on the dead and dying; the other checks apply
    // as ever, whatever the effect's duration.
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Rejected;
    }
    auto creator = instance.boundCreator();
    const bool immune = creature->hasEffectImmunity(ImmunityType::AbilityDecrease,
                                                    Creature::asCreature(creator));
    return !immune && admitsAbilityEffect(!instance.restoring && creature->isDead(),
                                          !instance.restoring && creature->isTemporarilyDead(),
                                          instance.integerParameter(1), true, creature->plotFlag())
               ? EffectApplicationResult::Retained
               : EffectApplicationResult::Rejected;
}

EffectApplicationResult ACIncreaseEffect::onApply(Object &object, EffectInstance &instance) {
    if (isa<Creature>(&object) && instance.integerParameter(1) <= 0) {
        return EffectApplicationResult::Rejected;
    }
    return EffectApplicationResult::Retained;
}

EffectApplicationResult ACDecreaseEffect::onApply(
    Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    if (instance.integerParameter(1) <= 0 || creature->plotFlag()) {
        return EffectApplicationResult::Rejected;
    }
    auto creator = instance.boundCreator();
    return creature->hasEffectImmunity(ImmunityType::AcDecrease, Creature::asCreature(creator))
               ? EffectApplicationResult::Rejected
               : EffectApplicationResult::Retained;
}

EffectApplicationResult AttackIncreaseEffect::onApply(Object &object, EffectInstance &) {
    if (isa<Creature>(&object) && _bonus <= 0) {
        return EffectApplicationResult::Rejected;
    }
    return EffectApplicationResult::Retained;
}

EffectApplicationResult AttackDecreaseEffect::onApply(
    Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    if (instance.integerParameter(0) <= 0 || creature->plotFlag()) {
        return EffectApplicationResult::Rejected;
    }
    auto creator = instance.boundCreator();
    return creature->hasEffectImmunity(ImmunityType::AttackDecrease, Creature::asCreature(creator))
               ? EffectApplicationResult::Rejected
               : EffectApplicationResult::Retained;
}

EffectApplicationResult AssuredHitEffect::onApply(Object &object, EffectInstance &) {
    auto *creature = dyn_cast<Creature>(&object);
    return creature && creature->applyAssuredHit()
               ? EffectApplicationResult::Retained
               : EffectApplicationResult::Rejected;
}

EffectRemovalResult AssuredHitEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->removeAssuredHit();
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult AssuredDeflectionEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    return creature && creature->applyAssuredDeflection(instance.integerParameter(0))
               ? EffectApplicationResult::Retained
               : EffectApplicationResult::Rejected;
}

EffectRemovalResult AssuredDeflectionEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->removeAssuredDeflection();
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult BlasterDeflectionIncreaseEffect::onApply(Object &object, EffectInstance &) {
    // Deflection reads the kept record; applying it changes nothing else.
    return EffectApplicationResult::Retained;
}

EffectApplicationResult DamageIncreaseEffect::onApply(Object &, EffectInstance &) {
    // Attack damage reads retained increases when it is rolled.
    return EffectApplicationResult::Retained;
}

EffectApplicationResult DamageDecreaseEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    const auto creator = instance.boundCreator();
    if (creature && creature->hasEffectImmunity(ImmunityType::DamageDecrease, Creature::asCreature(creator))) {
        return EffectApplicationResult::Rejected;
    }
    return EffectApplicationResult::Retained;
}

// Damage immunity is a signed byte, changed before it is limited: a change
// that takes it past 127 or below -128 wraps around.
static int wrapImmunity(int value) {
    return static_cast<int8_t>(static_cast<uint8_t>(value));
}

// Removing an immunity change rebuilds the value from every change with the
// same damage types, the one being removed included, less its own amount.
static void restoreImmunityWithout(Object &object, int flags, int amount) {
    int sum = 0;
    for (const EffectInstance &applied : object.effects()) {
        if (applied.integerParameter(0) != flags) {
            continue;
        }
        if (applied.type() == EffectType::DamageImmunityIncrease) {
            sum += applied.integerParameter(1);
        } else if (applied.type() == EffectType::DamageImmunityDecrease) {
            sum -= applied.integerParameter(1);
        }
    }
    object.setDamageImmunity(flags, wrapImmunity(sum - amount));
}

EffectApplicationResult DamageImmunityIncreaseEffect::onApply(
    Object &object, EffectInstance &) {
    if (_percentImmunity < 0) {
        return EffectApplicationResult::Rejected;
    }
    const int flags = static_cast<int>(_damageType);
    object.setDamageImmunity(flags, wrapImmunity(object.damageImmunity(flags) + _percentImmunity));
    return EffectApplicationResult::Retained;
}

EffectRemovalResult DamageImmunityIncreaseEffect::onRemove(Object &object, const EffectInstance &) {
    restoreImmunityWithout(object, static_cast<int>(_damageType), _percentImmunity);
    return EffectRemovalResult::Removed;
}

EffectApplicationResult DamageImmunityDecreaseEffect::onApply(
    Object &object, EffectInstance &instance) {
    if (_percentImmunity < 0 || object.plotFlag()) {
        return EffectApplicationResult::Rejected;
    }
    auto *target = dyn_cast<Creature>(&object);
    if (target) {
        auto creator = instance.boundCreator();
        if (target->hasEffectImmunity(ImmunityType::DamageImmunityDecrease,
                                      Creature::asCreature(creator))) {
            return EffectApplicationResult::Rejected;
        }
    }
    const int flags = static_cast<int>(_damageType);
    object.setDamageImmunity(flags, wrapImmunity(object.damageImmunity(flags) - _percentImmunity));
    return EffectApplicationResult::Retained;
}

// The decrease is still counted when it is taken out, so it is subtracted twice.
EffectRemovalResult DamageImmunityDecreaseEffect::onRemove(Object &object, const EffectInstance &) {
    restoreImmunityWithout(object, static_cast<int>(_damageType), _percentImmunity);
    return EffectRemovalResult::Removed;
}

EffectApplicationResult DamageResistanceEffect::onApply(Object &, EffectInstance &) {
    return EffectApplicationResult::Retained;
}

EffectApplicationResult ImmunityEffect::onApply(Object &object, EffectInstance &) {
    // The immunity checks read the kept record; applying it changes nothing else.
    return EffectApplicationResult::Retained;
}

EffectApplicationResult ForceResistanceIncreaseEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    const int amount = instance.integerParameter(0);
    if (amount < 0) {
        return EffectApplicationResult::Rejected;
    }
    creature->forceResistance().applyIncrease(amount);
    return EffectApplicationResult::Retained;
}

EffectRemovalResult ForceResistanceIncreaseEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->forceResistance().removeIncrease(object.effects(), instance);
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult ForceResistanceDecreaseEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    auto creator = instance.boundCreator();
    if (creature->hasEffectImmunity(ImmunityType::ForceResistanceDecrease, Creature::asCreature(creator))) {
        return EffectApplicationResult::Rejected;
    }
    const int amount = instance.integerParameter(0);
    if (amount < 0 || object.plotFlag()) {
        return EffectApplicationResult::Rejected;
    }
    creature->forceResistance().applyDecrease(amount);
    return EffectApplicationResult::Retained;
}

EffectRemovalResult ForceResistanceDecreaseEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->forceResistance().removeDecrease(object.effects(), instance);
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult ModifyAttacksEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    // In KotOR, a creature that already has more than two extra attacks
    // gains no more.
    if (!creature->game().isTSL() && creature->modifiedAttacks() > kMaxModifiedAttacks) {
        return EffectApplicationResult::Rejected;
    }

    creature->adjustModifiedAttacks(instance.integerParameter(0));
    return EffectApplicationResult::Retained;
}

EffectRemovalResult ModifyAttacksEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->adjustModifiedAttacks(-instance.integerParameter(0));
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult MovementSpeedIncreaseEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (creature) {
        instance.setIntegerParameter(0, normalizeMovementSpeedIncrease(instance.integerParameter(0)));
        creature->multiplyMovementRate(getMovementSpeedMultiplier(true, instance.integerParameter(0)));
    }
    return EffectApplicationResult::Retained;
}

EffectRemovalResult MovementSpeedIncreaseEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->recomputeMovementRate(instance.id);
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult MovementSpeedDecreaseEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (creature) {
        // A creature cannot be slowed by all of its speed or more.
        auto creator = instance.boundCreator();
        if (creature->hasEffectImmunity(ImmunityType::MovementSpeedDecrease,
                                        Creature::asCreature(creator)) ||
            instance.integerParameter(0) > kMaxMovementSpeedDecrease || creature->plotFlag()) {
            return EffectApplicationResult::Rejected;
        }
        creature->multiplyMovementRate(getMovementSpeedMultiplier(false, instance.integerParameter(0)));
    }
    return EffectApplicationResult::Retained;
}

EffectRemovalResult MovementSpeedDecreaseEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->recomputeMovementRate(instance.id);
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult SavingThrowIncreaseEffect::onApply(Object &object, EffectInstance &) {
    if (isa<Creature>(&object) && _value <= 0) {
        return EffectApplicationResult::Rejected;
    }
    return EffectApplicationResult::Retained;
}

EffectApplicationResult SavingThrowDecreaseEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    auto creator = instance.boundCreator();
    const bool immune = creature->hasEffectImmunity(ImmunityType::SavingThrowDecrease,
                                                    Creature::asCreature(creator));
    return instance.integerParameter(0) > 0 && !creature->plotFlag() && !immune
               ? EffectApplicationResult::Retained
               : EffectApplicationResult::Rejected;
}

EffectApplicationResult BonusFeatEffect::onApply(Object &object, EffectInstance &) {
    return isa<Creature>(&object) ? EffectApplicationResult::Retained : EffectApplicationResult::Rejected;
}

EffectApplicationResult SkillIncreaseEffect::onApply(Object &object, EffectInstance &instance) {
    return isa<Creature>(&object) && instance.integerParameter(1) >= 0
               ? EffectApplicationResult::Retained
               : EffectApplicationResult::Rejected;
}

EffectApplicationResult SkillDecreaseEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || instance.integerParameter(1) < 0 || object.plotFlag()) {
        return EffectApplicationResult::Rejected;
    }
    const auto creator = instance.boundCreator();
    return creature->hasEffectImmunity(ImmunityType::SkillDecrease, Creature::asCreature(creator))
               ? EffectApplicationResult::Rejected
               : EffectApplicationResult::Retained;
}

EffectApplicationResult TemporaryHitPointsEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    creature->addTemporaryHitPoints(instance.integerParameter(0), instance.restoring);
    return EffectApplicationResult::Retained;
}

EffectRemovalResult TemporaryHitPointsEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->removeTemporaryHitPoints(instance.integerParameter(0));
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult TemporaryForcePointsEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    creature->addTemporaryForcePoints(instance.integerParameter(0), instance.restoring);
    return EffectApplicationResult::Retained;
}

EffectRemovalResult TemporaryForcePointsEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->removeTemporaryForcePoints(instance.integerParameter(0));
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult RegenerateEffect::onApply(Object &object, EffectInstance &instance) {
    if (isa<Creature>(&object)) {
        const auto now = object.game().worldTimeMilliseconds();
        const auto day = object.game().millisecondsPerWorldDay();
        // Every application starts the interval afresh, a restored one included.
        instance.setIntegerParameter(2, static_cast<int>(now / day));
        instance.setIntegerParameter(3, static_cast<int>(now % day));
    }
    return EffectApplicationResult::Retained;
}

void RegenerateEffect::onUpdate(Object &object, const EffectInstance &instance, float) {
    auto *creature = dyn_cast<Creature>(&object);
    auto &game = object.game();
    if (object.isDead()) {
        return;
    }
    // Nothing regenerates on a party member at no vitality.
    if (creature && creature->isTemporarilyDead()) {
        return;
    }
    // A full pool regenerates no more.
    const int selector = instance.integerParameter(4);
    if (creature && selector == HealEffect::kForcePointsResource) {
        if (creature->currentForceWithoutTemporary() >= creature->maxForcePoints()) {
            return;
        }
    } else if (object.currentHitPointsWithoutTemporary() >= object.maxHitPoints()) {
        return;
    }

    // Regeneration runs on the creature's own time, which a time stop may
    // leave running.
    const auto nowDay = game.activeTimeDay(object);
    const auto nowTime = game.activeTimeOfDay(object);
    // Only the time of day part of the world time since the last heal counts,
    // and it must exceed integer 1. A last heal still ahead has no interval.
    uint32_t days = 0;
    uint32_t elapsed = 0;
    game.subtractWorldTimes(nowDay, nowTime,
                            static_cast<uint32_t>(instance.integerParameter(2)),
                            static_cast<uint32_t>(instance.integerParameter(3)), days, elapsed);
    if (elapsed <= static_cast<uint32_t>(instance.integerParameter(1))) {
        return;
    }

    auto heal = std::make_shared<HealEffect>(instance.integerParameter(0));
    auto child = heal->saveFacingInstance();
    child.effect = heal;
    child.setDuration(DurationType::Instant, 0.0f);
    child.setIntegerParameter(1, selector);
    if (game.isTSL()) {
        child.setIntegerParameter(2, 1);
    }
    child.creatorId = instance.creatorId;
    child.creator = instance.creator;
    // The heal takes the spell its creator is casting now, not the
    // regeneration's.
    if (auto creator = instance.boundCreator()) {
        child.spellId = creator->effectSpellId();
    }
    // The heal can change the effect list, which \p instance is part of, so
    // the regeneration is found again before the time of the heal is kept.
    // One heal is given however long the interval was.
    const uint64_t order = instance.applicationOrder;
    object.applyEffect(std::move(child));
    if (auto record = object.findEffectApplication(order)) {
        record->get().setIntegerParameter(2, static_cast<int>(nowDay));
        record->get().setIntegerParameter(3, static_cast<int>(nowTime));
    }
}

EffectApplicationResult VPRegenModifierEffect::onApply(Object &, EffectInstance &) {
    // A marker: vitality regeneration reads its percentage.
    return EffectApplicationResult::Retained;
}

namespace {

// Haste makes a creature half again as fast, with a dodge bonus to its
// defence; slow halves its speed, keeps it from running and costs it defence,
// attack and Reflex.
constexpr int kHasteSpeedPercent = 150;
constexpr int kHasteDefenceBonus = 4;
constexpr int kSlowSpeedPercent = 50;
constexpr int kSlowPenalty = 2;

void changeHasteSlowInternal(Object &object, HasteSlowTransition transition,
                             std::optional<std::reference_wrapper<const EffectInstance>> applyingRoot) {
    if (transition.before == transition.after) {
        return;
    }
    if (transition.before != 0) {
        const auto previousType = transition.before > 0 ? SavedEffectTypes::hasteInternal : SavedEffectTypes::slowInternal;
        for (const auto &record : object.effects()) {
            if (record.serializedType > previousType) {
                break;
            }
            if (record.serializedType == previousType) {
                const auto id = record.id;
                object.removeEffectsById(id);
                break;
            }
        }
    } else {
        object.applyEffect(makeHasteSlowInternal(transition.after, applyingRoot));
    }
}

} // namespace

EffectApplicationResult HasteSlowEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    const auto creator = instance.boundCreator();
    // A slow checks immunity even when restored; a haste does not.
    if (instance.serializedType == SavedEffectTypes::slow &&
        hasSlowImmunity(*creature, Creature::asCreature(creator))) {
        addSlowImmunityFeedback(object.game(), creator, *creature);
        return EffectApplicationResult::Rejected;
    }
    changeHasteSlowInternal(object, getHasteSlowTransition(object.effects(), instance.serializedType, false), instance);
    return EffectApplicationResult::Retained;
}

EffectRemovalResult HasteSlowEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        // The creature is neither hasted nor slowed until the haste or slow
        // left over is applied again, even when it is not replaced.
        creature->setHasted(false);
        creature->setSlowed(false);
        changeHasteSlowInternal(object, getHasteSlowTransition(object.effects(), instance.serializedType, true), std::nullopt);
    }
    return EffectRemovalResult::Removed;
}

EffectInstance HasteSlowInternalEffect::saveFacingInstance() const {
    auto record = Effect::saveFacingInstance();
    record.serializedType = _haste ? SavedEffectTypes::hasteInternal : SavedEffectTypes::slowInternal;
    return record;
}

EffectApplicationResult HasteSlowInternalEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    std::vector<EffectInstance> members;
    if (_haste) {
        members.push_back(instance.linkedChild(std::make_shared<MovementSpeedIncreaseEffect>(kHasteSpeedPercent)));
        members.push_back(instance.linkedChild(std::make_shared<ACIncreaseEffect>(kHasteDefenceBonus, ACBonus::Dodge, kPhysicalDamageTypeFlags)));
    } else {
        members = {
            instance.linkedChild(std::make_shared<MovementSpeedDecreaseEffect>(kSlowSpeedPercent)),
            instance.linkedChild(std::make_shared<ACDecreaseEffect>(kSlowPenalty, ACBonus::Dodge, kPhysicalDamageTypeFlags)),
            instance.linkedChild(std::make_shared<AttackDecreaseEffect>(kSlowPenalty, AttackBonus::Misc)),
            instance.linkedChild(std::make_shared<SavingThrowDecreaseEffect>(static_cast<int>(SavingThrow::Reflex), kSlowPenalty, SavingThrowType::All)),
            instance.linkedChild(std::make_shared<LimitMovementSpeedEffect>())};
    }
    for (auto &member : members) {
        member.markGeneratedForLoad();
    }
    object.applyEffectPackage(members);
    creature->setHasted(_haste);
    creature->setSlowed(!_haste);
    return EffectApplicationResult::Retained;
}

EffectRemovalResult HasteSlowInternalEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        if (_haste) {
            creature->setHasted(false);
        } else {
            creature->setSlowed(false);
        }
    }
    return EffectRemovalResult::Removed;
}

EffectApplicationResult DisguiseEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || (!instance.restoring && creature->isDead())) {
        return EffectApplicationResult::Rejected;
    }
    // A new disguise ends the one before it.
    for (const auto &record : object.effects()) {
        if (record.serializedType != SavedEffectTypes::disguise || record.id == instance.id) {
            continue;
        }
        const EffectId previousId = record.id;
        if (auto previous = object.findEffectApplication(record.applicationOrder)) {
            previous->get().setIntegerParameter(1, 1);
        }
        object.removeEffectsById(previousId);
        break;
    }
    creature->applyDisguiseAppearance(instance.integerParameter(0));
    return EffectApplicationResult::Retained;
}

EffectRemovalResult DisguiseEffect::onRemove(Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || (creature->isDead() && !creature->isPC() && object.isDestroyable())) {
        return EffectRemovalResult::Removed;
    }
    if (instance.integerParameter(1) == 0) {
        if (auto record = object.findEffectApplication(instance.applicationOrder)) {
            record->get().setIntegerParameter(1, 1);
            record->get().setDurationType(DurationType::Temporary);
            record->get().duration = 0.0f;
        }
    }
    creature->removeDisguiseAppearance();
    return EffectRemovalResult::Removed;
}

namespace {

std::optional<Faction> factionOf(Object &object) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        return creature->faction();
    }
    if (auto *door = dyn_cast<Door>(&object)) {
        return door->faction();
    }
    if (auto *placeable = dyn_cast<Placeable>(&object)) {
        return placeable->faction();
    }
    if (auto *trigger = dyn_cast<Trigger>(&object)) {
        return trigger->faction();
    }
    return std::nullopt;
}

// Placeables and triggers only record the faction. Creatures and doors change
// membership and drop their actions; a creature that is not dead then forgets
// what it perceived and looks around anew.
void changeFaction(Object &object, Faction faction) {
    if (auto *placeable = dyn_cast<Placeable>(&object)) {
        placeable->setFaction(faction);
        return;
    }
    if (auto *trigger = dyn_cast<Trigger>(&object)) {
        trigger->setFaction(faction);
        return;
    }
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setFaction(faction);
    } else if (auto *door = dyn_cast<Door>(&object)) {
        door->setFaction(faction);
    }
    object.clearAllActions(true);
    if (auto *creature = dyn_cast<Creature>(&object);
        creature && !creature->isDead() && !creature->isTemporarilyDead()) {
        creature->perceiveAfresh();
    }
}

bool isNpcFaction(const Object &object, int faction) {
    if (faction <= static_cast<int>(Faction::Player)) {
        return false;
    }
    return static_cast<size_t>(faction) < object.services().game.reputes.state().factions.size();
}

} // namespace

EffectApplicationResult FactionModifierEffect::onApply(Object &object, EffectInstance &instance) {
    if (instance.restoring) {
        return EffectApplicationResult::Retained;
    }
    const auto oldFaction = factionOf(object);
    if (!oldFaction || !isNpcFaction(object, _newFaction)) {
        return EffectApplicationResult::Rejected;
    }
    if (auto *creature = dyn_cast<Creature>(&object); creature && creature->isPC()) {
        return EffectApplicationResult::Rejected;
    }

    instance.setIntegerParameter(1, static_cast<int>(*oldFaction));
    changeFaction(object, static_cast<Faction>(_newFaction));
    return EffectApplicationResult::Retained;
}

EffectRemovalResult FactionModifierEffect::onRemove(Object &object, const EffectInstance &instance) {
    // The end of a confusion power clears the global its script set.
    switch (static_cast<SpellType>(instance.spellId)) {
    case SpellType::BeastConfusion:
        object.game().setGlobalNumber("000_Beast_Conf_Active", 0);
        break;
    case SpellType::Confusion:
        object.game().setGlobalNumber("000_Human_Conf_Active", 0);
        break;
    case SpellType::DroidConfusion:
        object.game().setGlobalNumber("000_Droid_Conf_Active", 0);
        break;
    default:
        break;
    }
    changeFaction(object, static_cast<Faction>(instance.integerParameter(1)));
    return EffectRemovalResult::Removed;
}

} // namespace game

} // namespace reone
