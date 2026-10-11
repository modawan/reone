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

#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/abilityincrease.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/assuredhit.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/blasterdeflectionincrease.h"
#include "reone/game/effect/damagedecrease.h"
#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damageincrease.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/disguise.h"
#include "reone/game/effect/factionmodifier.h"
#include "reone/game/effect/forceresistanceincrease.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/modifyattacks.h"
#include "reone/game/effect/movementspeeddecrease.h"
#include "reone/game/effect/movementspeedincrease.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/effect/skillincrease.h"
#include "reone/game/effect/temporaryforcepoints.h"
#include "reone/game/effect/temporaryhitpoints.h"
#include "reone/game/effect/vpregenmodifier.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

void AbilityIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void AbilityDecreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void ACIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

bool ACDecreaseEffect::onApply(
    Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || _value <= 0 || creature->plotFlag()) {
        return false;
    }
    auto creator = instance.boundCreator();
    return !creature->hasEffectImmunity(
        ImmunityType::AcDecrease,
        creator ? dyn_cast<Creature>(creator.get()) : nullptr);
}

void AttackIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

bool AttackDecreaseEffect::onApply(
    Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || _penalty <= 0 || creature->plotFlag()) {
        return false;
    }
    auto creator = instance.boundCreator();
    return !creature->hasEffectImmunity(
        ImmunityType::AttackDecrease,
        creator ? dyn_cast<Creature>(creator.get()) : nullptr);
}

bool AssuredHitEffect::onApply(Object &object, const EffectInstance &) {
    auto *creature = dyn_cast<Creature>(&object);
    return creature && creature->applyAssuredHit();
}

void AssuredHitEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->removeAssuredHit();
    }
}

void BlasterDeflectionIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void DamageIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void DamageDecreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

bool DamageImmunityIncreaseEffect::onApply(
    Object &, const EffectInstance &) {
    return _percentImmunity >= 0;
}

bool DamageImmunityDecreaseEffect::onApply(
    Object &object, const EffectInstance &instance) {
    if (_percentImmunity < 0 || object.plotFlag()) {
        return false;
    }
    auto *target = dyn_cast<Creature>(&object);
    if (!target) {
        return true;
    }
    auto creator = instance.boundCreator();
    return !target->hasEffectImmunity(
        ImmunityType::DamageImmunityDecrease,
        creator ? dyn_cast<Creature>(creator.get()) : nullptr);
}

void DamageResistanceEffect::applyTo(Object &) {
}

void ImmunityEffect::applyTo(Object &object) {
    // TODO: implement
}

void ForceResistanceIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

bool ModifyAttacksEffect::onApply(Object &object, const EffectInstance &) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return false;
    }

    creature->adjustModifiedAttacks(_attacks);
    return true;
}

void ModifyAttacksEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->adjustModifiedAttacks(-_attacks);
    }
}

void MovementSpeedIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void MovementSpeedDecreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void SavingThrowIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void SavingThrowDecreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void SkillIncreaseEffect::applyTo(Object &object) {
    // TODO: implement
}

void TemporaryHitPointsEffect::applyTo(Object &object) {
    // TODO: implement
}

void TemporaryForcePointsEffect::applyTo(Object &object) {
    // TODO: implement
}

void RegenerateEffect::applyTo(Object &object) {
    // TODO: implement
}

void VPRegenModifierEffect::applyTo(Object &object) {
    // TODO: implement
}

void DisguiseEffect::applyTo(Object &object) {
    // TODO: implement
}

void FactionModifierEffect::applyTo(Object &object) {
    // TODO: implement
}

} // namespace game

} // namespace reone
