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

#include <algorithm>
#include <cmath>

#include "reone/game/di/services.h"
#include "reone/game/effect/assassinate.h"
#include "reone/game/effect/beam.h"
#include "reone/game/effect/bodyfuel.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/forcebody.h"
#include "reone/game/effect/forcefizzle.h"
#include "reone/game/effect/forcejump.h"
#include "reone/game/effect/forcepushed.h"
#include "reone/game/effect/forcepushtargeted.h"
#include "reone/game/effect/forceresisted.h"
#include "reone/game/effect/forceshield.h"
#include "reone/game/effect/forcesight.h"
#include "reone/game/effect/fury.h"
#include "reone/game/effect/lightsaberthrow.h"
#include "reone/game/effect/psychicstatic.h"
#include "reone/game/effect/visual.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/projectiles.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"

namespace reone {

namespace game {

// The Force jump's leap starts this long after the jump, and comes down this
// far short of the target.
static constexpr uint32_t kForceJumpLeapDelayMilliseconds = 300;
static constexpr float kForceJumpLandingRange = 1.4f;

// Visual effects (visualeffects.2da rows).
static constexpr int kForceJumpVisual = 1002;
static constexpr int kForceFizzleVisual = 4036;
static constexpr int kForceResistedVisual = 4037;

// When a push ends, the creature's AI stays held this long.
static constexpr float kForcePushAIHoldSeconds = 2.55f;
// A push that names no distance carries the creature this far.
static constexpr float kForcePushDefaultDistance = 5.0f;
// The push's line of travel is tested a little above the ground.
static constexpr float kForcePushTestHeight = 0.1f;

// The damage event slot an assassination's damage is reported in.
static constexpr size_t kAssassinateDamageSlot = 4;

// The unit vector along delta; +X when delta is shorter than 1e-9.
static glm::vec3 unitDirection(const glm::vec3 &delta) {
    const float length = std::sqrt((delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z);
    return static_cast<double>(length) < 1e-9 ? glm::vec3 {1.0f, 0.0f, 0.0f} : delta * (1.0f / length);
}

// Pushes the creature away from the centre and lays it in the force-pushed
// state for the push effect's duration.
static bool applyForcePushMovement(Object &object, const glm::vec3 &centre, bool ignoreDirectLine, EffectInstance &owner) {
    auto *target = dyn_cast<Creature>(&object);
    if (!target) {
        return false;
    }
    const auto direction = unitDirection(target->position() - centre);
    const float distance = owner.floatParameters[3] == 0.0f ? kForcePushDefaultDistance : owner.floatParameters[3];
    auto destination = target->position() + direction * distance;
    const float facing = -glm::atan(-direction.x, -direction.y);
    target->turnTo(facing);
    if (!ignoreDirectLine && target->sceneNode()) {
        const glm::vec3 lift {0.0f, 0.0f, kForcePushTestHeight};
        scene::Collision collision;
        if (target->sceneNode()->graph().testWalk(target->position() + lift, destination + lift, target, collision)) {
            destination = collision.intersection;
        }
    }
    target->beginForcePush(destination);
    // The state is laid afresh whenever the push is applied, a restored push
    // included, so it is never saved on its own.
    auto state = owner.linkedChild(std::make_shared<CreatureStateEffect>(CreatureState::ForcePushed));
    state.setDuration(DurationType::Temporary, owner.duration);
    state.markGeneratedForLoad();
    state.restoring = false;
    object.applyEffect(std::move(state));
    return true;
}

// The creator a push comes from; only an object in the world pushes.
static std::shared_ptr<Object> getForcePushCreator(const EffectInstance &owner) {
    auto creator = owner.boundCreator();
    if (!creator || isa<Area>(creator.get()) || isa<Module>(creator.get())) {
        return nullptr;
    }
    return creator;
}

// A push ending frees the creature's facing and holds its AI a moment.
static void endForcePushEffect(Object &object) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return;
    }
    creature->setOrientationLock(script::kObjectInvalid);
    auto hold = std::make_shared<CreatureAIStateEffect>(Creature::kAIStateMaskHeld);
    object.applyEffect(hold, DurationType::Temporary, kForcePushAIHoldSeconds);
}

EffectApplicationResult ForcePushedEffect::onApply(Object &object, EffectInstance &record) {
    if (auto creator = getForcePushCreator(record)) {
        applyForcePushMovement(object, creator->position(), false, record);
    }
    // The record is kept even when nothing is pushed.
    return EffectApplicationResult::Retained;
}

void ForcePushedEffect::onRemove(Object &object, const EffectInstance &) {
    endForcePushEffect(object);
}

EffectApplicationResult ForceJumpEffect::onApply(Object &object, EffectInstance &instance) {
    auto *jumper = dyn_cast<Creature>(&object);
    auto target = std::dynamic_pointer_cast<Creature>(instance.boundObjectParameter(0));
    // The record is retained whether or not it can leap, and it leaps again
    // each time it is applied, a restored record included.
    if (!jumper || !target) {
        return EffectApplicationResult::Retained;
    }
    jumper->setOrientationLock(target->id());
    auto leap = instance.linkedChild(std::make_shared<ForceJumpDelayedEffect>(target));
    leap.setDuration(DurationType::Temporary, instance.duration);
    leap.markGeneratedForLoad();
    leap.restoring = false;
    object.game().queueEffectApplication(object, std::move(leap), kForceJumpLeapDelayMilliseconds);
    auto visual = instance.linkedChild(std::make_shared<VisualEffectMarkerEffect>(kForceJumpVisual));
    visual.setDuration(DurationType::Instant, 0.0f);
    visual.markGeneratedForLoad();
    visual.restoring = false;
    object.applyEffect(std::move(visual));
    return EffectApplicationResult::Retained;
}

EffectInstance ForceJumpDelayedEffect::saveFacingInstance() const {
    auto record = Effect::saveFacingInstance();
    record.serializedType = SavedEffectTypes::forceJumpDelayed;
    return record;
}

EffectApplicationResult ForceJumpDelayedEffect::onApply(Object &object, EffectInstance &instance) {
    auto *jumper = dyn_cast<Creature>(&object);
    auto target = std::dynamic_pointer_cast<Creature>(instance.boundObjectParameter(0));
    if (!jumper || !target) {
        return EffectApplicationResult::Retained;
    }
    jumper->setOrientationLock(target->id());
    auto area = object.game().module()->area();
    const auto direction = unitDirection(target->position() - jumper->position());
    auto destination = target->position() - direction * kForceJumpLandingRange;
    if (!area->isSafeLocationPoint(destination, *target)) {
        // With no room for the target there, the jumper comes down where the
        // target stands. The target is ordered off its spot, which lets go of
        // its orientation lock, but the state it is put in for the jump's
        // duration clears its actions before it takes a step.
        destination = target->position();
        target->setOrientationLock(script::kObjectInvalid);
        auto state = instance.linkedChild(std::make_shared<CreatureStateEffect>(CreatureState::ForceJumpedOnto));
        state.setDuration(DurationType::Temporary, instance.duration);
        state.markGeneratedForLoad();
        state.restoring = false;
        target->applyEffect(std::move(state));
    }
    jumper->beginLeap(destination);
    return EffectApplicationResult::Retained;
}

EffectApplicationResult AssassinateEffect::onApply(Object &object, EffectInstance &instance) {
    const auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Applied;
    }
    // Ranks 1 to 3 take a quarter, a third or half of the creature's vitality.
    const int hitPoints = creature->currentHitPoints();
    int amount;
    switch (instance.integerParameter(0)) {
    case 1:
        amount = hitPoints / 4;
        break;
    case 2:
        amount = hitPoints / 3;
        break;
    case 3:
        amount = hitPoints / 2;
        break;
    default:
        return EffectApplicationResult::Applied;
    }
    if (amount <= 0) {
        return EffectApplicationResult::Applied;
    }
    auto damage = DamageEffect::fromResolvedAttackAmount(amount, kAssassinateDamageSlot);
    damage->setSaveFacingCreator(instance.boundCreator());
    object.applyEffect(std::move(damage), DurationType::Instant);
    return EffectApplicationResult::Applied;
}

EffectApplicationResult BeamEffect::onApply(Object &object, EffectInstance &instance) {
    // Only a creature, a placeable or a door shows the beam, and in TSL a
    // trigger; the record is kept on any object.
    const auto type = object.type();
    const bool showsBeam = type == ObjectType::Creature || type == ObjectType::Placeable || type == ObjectType::Door ||
                           (object.game().isTSL() && type == ObjectType::Trigger);
    if (!showsBeam) {
        return EffectApplicationResult::Retained;
    }
    auto visual = std::make_shared<VisualEffect>(instance.integerParameter(0), instance.integerParameter(2) != 0, object.services());
    auto child = instance.linkedChild(visual);
    child.setIntegerParameter(1, instance.integerParameter(1));
    child.setIntegerParameter(2, instance.integerParameter(2));
    child.markGeneratedForLoad();
    child.objectParameters = instance.objectParameters;
    child.objectParameterObjects = instance.objectParameterObjects;
    child.setCategory(EffectCategory::Magical);
    object.applyEffect(std::move(child));
    return EffectApplicationResult::Retained;
}

EffectApplicationResult BodyFuelEffect::onApply(Object &object, EffectInstance &) {
    // The record is kept on any object; only a creature refills at the end of
    // its round.
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setBodyFuel(true);
    }
    return EffectApplicationResult::Retained;
}

void BodyFuelEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setBodyFuel(false);
    }
}

EffectApplicationResult ForceBodyEffect::onApply(Object &object, EffectInstance &) {
    // A marker: casting reads its level to pay a power's cost; applying it
    // grants or spends nothing.
    return EffectApplicationResult::Retained;
}

EffectApplicationResult ForceFizzleEffect::onApply(Object &object, EffectInstance &instance) {
    // The visual is applied on its own, not as part of the fizzle's group.
    auto visual = std::make_shared<VisualEffect>(kForceFizzleVisual, false, object.services());
    auto child = visual->saveFacingInstance();
    child.effect = visual;
    child.creatorId = object.id();
    child.creator = object.game().getObjectById(object.id());
    child.setDuration(DurationType::Instant, 0.0f);
    child.restoring = instance.restoring;
    object.applyEffect(std::move(child));
    return EffectApplicationResult::Applied;
}

EffectApplicationResult ForcePushTargetedEffect::onApply(Object &object, EffectInstance &record) {
    if (getForcePushCreator(record)) {
        const glm::vec3 centre {record.floatParameters[0], record.floatParameters[1], record.floatParameters[2]};
        applyForcePushMovement(object, centre, record.integerParameter(1) != 0, record);
    }
    return EffectApplicationResult::Retained;
}

void ForcePushTargetedEffect::onRemove(Object &object, const EffectInstance &) {
    endForcePushEffect(object);
}

EffectApplicationResult ForceResistedEffect::onApply(Object &object, EffectInstance &instance) {
    // The visual is applied on its own, not as part of the resisted effect's group.
    auto visual = std::make_shared<VisualEffect>(kForceResistedVisual, false, object.services());
    auto child = visual->saveFacingInstance();
    child.effect = visual;
    child.creatorId = object.id();
    child.creator = object.game().getObjectById(object.id());
    child.setDuration(DurationType::Instant, 0.0f);
    child.restoring = instance.restoring;
    child.objectParameters[0] = instance.objectParameters[0];
    child.objectParameterObjects[0] = instance.objectParameterObjects[0];
    object.applyEffect(std::move(child));
    // A creature plays its resist animation whether or not the visual shows.
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->playForceResistedAnimation();
    }
    return EffectApplicationResult::Applied;
}

EffectApplicationResult ForceShieldEffect::onApply(Object &object, EffectInstance &record) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    if (creature->sceneNode()) {
        if (auto previous = replacedForceShield(object.effects())) {
            object.removeEffectsById(*previous);
        }
    }
    const auto shield = forceShieldDefinition(
        object.services().game.combatTables.forceShield(record.integerParameter(0)), creature->appearance());
    auto visual = record.linkedChild(std::make_shared<VisualEffectMarkerEffect>(shield.visual));
    visual.markGeneratedForLoad();
    auto target = object.game().getObjectById(object.id());
    visual.creator = target;
    visual.creatorId = object.id();
    visual.spellId = object.effectSpellId();
    visual.restoring = false;
    auto protection = record.linkedChild(std::make_shared<DamageResistanceEffect>(
        static_cast<DamageType>(shield.damageFlags), shield.resistance, shield.amount, shield.vulnerabilities));
    // In TSL, integer 4 marks the resistance as a Force shield's.
    if (object.game().isTSL()) {
        protection.setIntegerParameter(4, 1);
    }
    protection.markGeneratedForLoad();
    protection.creator = target;
    protection.creatorId = object.id();
    protection.spellId = object.effectSpellId();
    protection.restoring = false;
    protection.setCategory(EffectCategory::Magical);
    object.applyEffect(visual);
    object.applyEffect(protection);
    return EffectApplicationResult::Retained;
}

EffectApplicationResult ForceSightEffect::onApply(Object &, EffectInstance &) {
    // A marker: the leader's video effect follows it (Game::updateLeaderVideoEffect).
    return EffectApplicationResult::Retained;
}

EffectApplicationResult FuryEffect::onApply(Object &object, EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->applyFuryState(instance.spellId);
    }
    return EffectApplicationResult::Retained;
}

void FuryEffect::onRemove(Object &object, const EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return;
    }
    const bool survivor = std::any_of(
        object.effects().begin(), object.effects().end(),
        [&instance](const EffectInstance &record) {
            return record.applicationOrder != instance.applicationOrder &&
                   record.hasLiveRuntimeSource() &&
                   record.serializedType == SavedEffectTypes::fury;
        });
    if (!survivor) {
        creature->clearFuryState();
    }
}

EffectApplicationResult LightsaberThrowEffect::onApply(Object &object, EffectInstance &record) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    // Only a TSL thrower with Force Repulsion or Force Redirection may parry
    // while its saber is thrown.
    if (!object.game().isTSL() ||
        (!creature->attributes().hasSpell(SpellType::ForceRepulsion) &&
         !creature->attributes().hasSpell(SpellType::ForceRedirection))) {
        creature->setThrowParryBlocked(true);
    }
    if (!record.restoring) {
        object.services().game.projectiles.launchLightsaberThrow(*creature, record, object.game(), object.services());
    }
    return EffectApplicationResult::Retained;
}

void LightsaberThrowEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setThrowParryBlocked(false);
    }
}

EffectApplicationResult PsychicStaticEffect::onApply(Object &object, EffectInstance &) {
    // The record is kept; applying it changes nothing else.
    return EffectApplicationResult::Retained;
}

} // namespace game

} // namespace reone
