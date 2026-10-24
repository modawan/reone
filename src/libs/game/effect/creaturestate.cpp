/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/effect/creaturestate.h"

#include <algorithm>
#include <cmath>

#include "reone/game/combat.h"
#include "reone/game/combatfeedback.h"
#include "reone/game/combattables.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/beam.h"
#include "reone/game/effect/blind.h"
#include "reone/game/effect/entangle.h"
#include "reone/game/effect/haste.h"
#include "reone/game/effect/invisibility.h"
#include "reone/game/effect/knockdown.h"
#include "reone/game/effect/misschance.h"
#include "reone/game/effect/movementspeeddecrease.h"
#include "reone/game/effect/paralyze.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/seeinvisible.h"
#include "reone/game/effect/sleep.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/effect/trueseeing.h"
#include "reone/game/effect/ultravision.h"
#include "reone/game/effect/visual.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/system/cast.h"

namespace reone {

namespace game {

// Visual effects (visualeffects.2da rows).
static constexpr int kSleepOnsetVisual = 94;
static constexpr int kSlowOnsetVisual = 95;
static constexpr int kFearVisual = 218;
static constexpr int kStunVisual = 208;
static constexpr int kParalysisVisual = 232;
static constexpr int kParalysisHoldVisual = 82;
static constexpr int kSleepVisual = 7;
static constexpr int kDebilitatedVisual = 207;
static constexpr int kDroidStunVisual = 1007;
static constexpr int kStunnedVisual = 2002;
static constexpr int kBlindVisual = 5002;

// Effect icons (effecticon.2da rows).
static constexpr int kFearIcon = 6;
static constexpr int kConfusionIcon = 17;
static constexpr int kStunIcon = 29;
static constexpr int kParalysisIcon = 15;
static constexpr int kSleepIcon = 27;
static constexpr int kSlowIcon = 28;

// A frightened or droid-stunned creature loses this much from every saving throw.
static constexpr int kAllSavingThrows = 0;
static constexpr int kStateSavingThrowPenalty = 2;

// An entangled creature's attack and Dexterity penalties; the attack penalty
// counts against every race.
static constexpr int kEntangleAttackPenalty = 2;
static constexpr int kEntangleDexterityPenalty = 4;
static constexpr int kAllRacesSelector = 28;

// A blinded creature misses half its attacks.
static constexpr int kBlindMissChance = 50;

static constexpr float kKnockdownAIHoldSeconds = 1.5f;
static constexpr float kKnockdownFrontFacing = 0.707f;
static constexpr int kKnockdownBackAnimationId = 10005;
static constexpr int kKnockdownFrontAnimationId = 10007;

bool hasStateSpecificImmunity(const Creature &target, CreatureState state, std::optional<std::reference_wrapper<const Creature>> creator) {
    switch (state) {
    case CreatureState::Sleep:
        return target.hasEffectImmunity(ImmunityType::Sleep, creator);
    case CreatureState::Paralysis:
        return target.hasEffectImmunity(ImmunityType::Paralysis, creator) ||
               target.hasEffectiveFeat(FeatType::ForceImmunityParalysis);
    case CreatureState::Stun:
        return target.hasEffectImmunity(ImmunityType::Stun, creator) ||
               target.hasEffectiveFeat(FeatType::ForceImmunityStun) ||
               target.hasEffectiveFeat(FeatType::ForceImmunityParalysis);
    case CreatureState::Fear:
        return target.hasEffectImmunity(ImmunityType::Fear, creator) ||
               (target.game().isTSL() && target.hasEffectiveFeat(FeatType::MandalorianCourage));
    case CreatureState::Confusion:
        return target.hasEffectImmunity(ImmunityType::Confused, creator);
    case CreatureState::Knockdown:
        return target.hasEffectImmunity(ImmunityType::Knockdown, creator);
    case CreatureState::MindTrick:
        return target.hasEffectImmunity(ImmunityType::MindSpells, creator);
    case CreatureState::DroidConfused:
        return target.hasEffectImmunity(ImmunityType::DroidConfused, creator);
    case CreatureState::DroidScramble:
        return false;
    case CreatureState::Horrified:
        return target.hasEffectiveFeat(FeatType::ForceImmunityFear) ||
               (target.game().isTSL() && target.hasEffectiveFeat(FeatType::MandalorianCourage));
    case CreatureState::DroidStun:
    case CreatureState::Choke:
    case CreatureState::Whirlwind:
    case CreatureState::Crush:
    case CreatureState::ForcePushed:
    case CreatureState::ForceJumpedOnto:
    case CreatureState::None:
        return false;
    }
    return false;
}

bool hasStateImmunity(const Creature &target, CreatureState state, std::optional<std::reference_wrapper<const Creature>> creator) {
    if (hasStateSpecificImmunity(target, state, creator)) {
        return true;
    }
    switch (state) {
    case CreatureState::DroidStun:
        return target.hasEffectImmunity(ImmunityType::Stun, creator) ||
               target.hasEffectImmunity(ImmunityType::Dazed, creator);
    case CreatureState::Choke:
        return target.hasEffectImmunity(ImmunityType::All, creator);
    case CreatureState::Horrified:
        return target.hasEffectImmunity(ImmunityType::MindSpells, creator) ||
               target.hasEffectImmunity(ImmunityType::Fear, creator);
    case CreatureState::Whirlwind:
    case CreatureState::Crush:
    case CreatureState::DroidConfused:
    case CreatureState::DroidScramble:
    case CreatureState::Paralysis:
    case CreatureState::Knockdown:
    case CreatureState::ForcePushed:
    case CreatureState::ForceJumpedOnto:
    case CreatureState::None:
        return false;
    default:
        return target.hasEffectImmunity(ImmunityType::MindSpells, creator);
    }
}

bool hasSlowImmunity(const Creature &target, std::optional<std::reference_wrapper<const Creature>> creator) {
    return target.hasEffectImmunity(ImmunityType::Slow, creator);
}

CreatureStateEffect::CreatureStateEffect(CreatureState state, bool bypassPackageInspection) :
    CopyableEffect(EffectType::Invalid),
    _state(state) {
    switch (state) {
    case CreatureState::Stun:
        _type = EffectType::Stunned;
        break;
    case CreatureState::Paralysis:
        _type = EffectType::Paralyze;
        break;
    case CreatureState::Sleep:
        _type = EffectType::Sleep;
        break;
    case CreatureState::Fear:
        _type = EffectType::Frightened;
        break;
    case CreatureState::Confusion:
        _type = EffectType::Confused;
        break;
    case CreatureState::DroidStun:
        _type = EffectType::DroidStun;
        break;
    case CreatureState::Choke:
        _type = EffectType::Choke;
        break;
    case CreatureState::Horrified:
        _type = EffectType::Horrified;
        break;
    case CreatureState::Whirlwind:
        _type = EffectType::WhirlWind;
        break;
    case CreatureState::Crush:
        _type = EffectType::Crush;
        break;
    case CreatureState::MindTrick:
        _type = EffectType::MindTrick;
        break;
    case CreatureState::DroidConfused:
        _type = EffectType::DroidConfused;
        break;
    case CreatureState::DroidScramble:
        _type = EffectType::DroidScramble;
        break;
    case CreatureState::ForcePushed:
        _type = EffectType::ForcePushed;
        break;
    case CreatureState::Knockdown:
    case CreatureState::ForceJumpedOnto:
    case CreatureState::None:
        break;
    }
    setSaveFacingInteger(0, static_cast<int>(state));
    setSaveFacingInteger(1, bypassPackageInspection ? 1 : 0);
}

EffectInstance CreatureStateEffect::saveFacingInstance() const {
    auto result = Effect::saveFacingInstance();
    result.serializedType = SavedEffectTypes::setState;
    return result;
}

EffectApplicationResult CreatureStateEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    // The root keeps an inert record on anything but a creature, restored or not.
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    if ((_state == CreatureState::MindTrick || _state == CreatureState::DroidScramble) && !object.game().isTSL()) {
        return EffectApplicationResult::Rejected;
    }
    if (!stateHasConsumer(_state)) {
        return instance.restoring ? EffectApplicationResult::Retained : EffectApplicationResult::Rejected;
    }
    auto creator = instance.boundCreator();
    const bool bypass = instance.integerParameter(1) != 0;
    const SavedEffectValue candidate {instance};
    // Sleep, fear and both confusions check their immunities even when the
    // package is not inspected. Restoring changes only how the children are
    // applied.
    const bool checkStateImmunity = !bypass || _state == CreatureState::Sleep ||
                                    _state == CreatureState::Fear || _state == CreatureState::Confusion ||
                                    _state == CreatureState::DroidConfused;
    if (!bypass && creature->isEffectLinkImmune(candidate)) {
        _result = StateApplicationResult::Immune;
        return EffectApplicationResult::Rejected;
    }
    // A state-specific immunity is reported to the creator and the target,
    // whatever applied the state.
    if (checkStateImmunity &&
        hasStateSpecificImmunity(*creature, _state, Creature::asCreature(creator))) {
        addStateImmunityFeedback(object.game(), creator, *creature, _state);
        _result = StateApplicationResult::Immune;
        return EffectApplicationResult::Rejected;
    }
    auto commonAI = instance.linkedChild(std::make_shared<CreatureAIStateEffect>(Creature::kAIStateMaskRestrained));
    commonAI.markGeneratedForLoad();
    object.applyEffect(std::move(commonAI));
    creature->onStateRootApplied(instance);
    _result = creature->activeStateRootId() == instance.id
                  ? StateApplicationResult::Applied
                  : StateApplicationResult::Dormant;
    return EffectApplicationResult::Retained;
}

void CreatureStateEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        // Losing any state makes the creature commandable again; a state that
        // remains takes that back when it is laid again.
        creature->setCommandable(true);
        creature->rebuildStateEffects(std::nullopt, instance.applicationOrder);
    }
}

CreatureStateInternalEffect::CreatureStateInternalEffect(CreatureState state) :
    CopyableEffect(EffectType::Invalid) {
    setSaveFacingInteger(0, static_cast<int>(state));
}

EffectInstance CreatureStateInternalEffect::saveFacingInstance() const {
    auto result = Effect::saveFacingInstance();
    result.serializedType = SavedEffectTypes::setStateInternal;
    return result;
}

EffectApplicationResult CreatureStateInternalEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    const bool tsl = object.game().isTSL();
    const int state = instance.integerParameter(0);
    const auto is = [state](CreatureState candidate) {
        return state == static_cast<int>(candidate);
    };
    if (is(CreatureState::Stun) || is(CreatureState::Paralysis) || is(CreatureState::Sleep)) {
        creature->beginStateImmobilization();
    }
    // Only a Force push leaves the creature's actions alone, restored or not;
    // the actions saved with a restored creature come back afterwards. Every
    // other state clears its actions, even those of a creature that cannot be
    // commanded, and leaves it uncommandable; a restored creature keeps the
    // commandable state it was saved with.
    if (!is(CreatureState::ForcePushed)) {
        creature->clearAllActions(true, true);
        object.game().combat().cancelActions(*creature);
        if (!instance.restoring) {
            creature->setCommandable(false);
        }
    }
    std::vector<EffectInstance> members;
    if (is(CreatureState::DroidStun)) {
        members.push_back(instance.linkedChild(std::make_shared<VisualEffectMarkerEffect>(kDroidStunVisual)));
        members.push_back(instance.linkedChild(std::make_shared<SavingThrowDecreaseEffect>(kAllSavingThrows, kStateSavingThrowPenalty, SavingThrowType::All)));
    }
    if (is(CreatureState::Fear)) {
        members.push_back(instance.linkedChild(std::make_shared<SavingThrowDecreaseEffect>(kAllSavingThrows, kStateSavingThrowPenalty, SavingThrowType::All)));
    }
    const bool stunnedVisual = is(CreatureState::Stun) ||
                               (tsl && (is(CreatureState::MindTrick) || is(CreatureState::DroidScramble)));
    // TSL also holds the AI of a creature knocked down, crushed or in its
    // otherwise unused state 17.
    const bool auxiliaryAI = tsl && (is(CreatureState::Knockdown) || is(CreatureState::Crush) || state == 17);
    const bool holdsAI = state >= static_cast<int>(CreatureState::DroidStun) &&
                         state <= static_cast<int>(CreatureState::Whirlwind);
    if (holdsAI || stunnedVisual || auxiliaryAI) {
        int mask = Creature::kAIStateMaskRooted;
        if (is(CreatureState::Paralysis)) {
            mask = Creature::kAIStateMaskParalyzed;
        } else if (is(CreatureState::Sleep)) {
            mask = Creature::kAIStateMaskHeld;
        }
        members.push_back(instance.linkedChild(std::make_shared<CreatureAIStateEffect>(mask)));
    }
    if (stunnedVisual) {
        members.push_back(instance.linkedChild(std::make_shared<VisualEffectMarkerEffect>(kStunnedVisual)));
    }
    for (auto &member : members) {
        member.markGeneratedForLoad();
    }
    object.applyEffectPackage(members);
    return EffectApplicationResult::Retained;
}

void CreatureStateInternalEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        const int state = instance.integerParameter(0);
        // The end-of-round script runs during a load too.
        if (stateRemovalRunsEndRoundScript(state, object.game().isTSL())) {
            creature->runEndRoundScript();
        }
        creature->onInternalStateRemoved(instance.id, stateRemovalAppliesPose(state, object.game().isTSL()));
        // A creature other than a player character forgets what it perceived
        // and looks around anew.
        if (!creature->isPC()) {
            creature->perceiveAfresh();
        }
    }
}

CreatureAIStateEffect::CreatureAIStateEffect(int mask) :
    CopyableEffect(EffectType::Invalid) {
    setSaveFacingInteger(0, mask);
}

EffectInstance CreatureAIStateEffect::saveFacingInstance() const {
    auto result = Effect::saveFacingInstance();
    result.serializedType = SavedEffectTypes::setAIState;
    return result;
}

EffectApplicationResult CreatureAIStateEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    creature->restrictAIState(instance.integerParameter(0));
    return EffectApplicationResult::Retained;
}

void CreatureAIStateEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        // A creature whose AI has no action left gets none back.
        if (creature->effectAIStateMask() != 0) {
            creature->recomputeAIStateEffects(Creature::kAIStateUnrestricted, instance.applicationOrder);
        }
    }
}

EffectIconMarkerEffect::EffectIconMarkerEffect(int iconId) :
    CopyableEffect(EffectType::Invalid) {
    setSaveFacingInteger(0, iconId);
}

EffectInstance EffectIconMarkerEffect::saveFacingInstance() const {
    auto result = Effect::saveFacingInstance();
    result.serializedType = SavedEffectTypes::icon;
    return result;
}

EffectApplicationResult EffectIconMarkerEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || instance.durationType() == DurationType::Instant) {
        return EffectApplicationResult::Rejected;
    }
    // Only an icon whose row gives its picture and priority is shown; TSL
    // also needs its side and name.
    const int row = instance.integerParameter(0);
    const auto icon = object.services().game.combatTables.effectIcon(row);
    if (!icon.hasIcon || !icon.priority || (object.game().isTSL() && (!icon.good || !icon.nameStrRef))) {
        return EffectApplicationResult::Rejected;
    }
    creature->addEffectIcon(row);
    return EffectApplicationResult::Retained;
}

void EffectIconMarkerEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->removeEffectIcon(instance.integerParameter(0));
    }
}

VisualEffectMarkerEffect::VisualEffectMarkerEffect(int visualEffectId) :
    CopyableEffect(EffectType::Visual) {
    setSaveFacingInteger(0, visualEffectId);
}

EffectApplicationResult VisualEffectMarkerEffect::onApply(Object &object, EffectInstance &instance) {
    auto visual = std::make_shared<VisualEffect>(instance.integerParameter(0), instance.integerParameter(2) != 0, object.services());
    return visual->onApply(object, instance);
}

EffectInstance LimitMovementSpeedEffect::saveFacingInstance() const {
    auto record = Effect::saveFacingInstance();
    record.serializedType = SavedEffectTypes::limitMovementSpeed;
    return record;
}

EffectApplicationResult LimitMovementSpeedEffect::onApply(Object &object, EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        if (object.plotFlag()) {
            return EffectApplicationResult::Rejected;
        }
        creature->setRunLimited(true);
    }
    return EffectApplicationResult::Retained;
}

void LimitMovementSpeedEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setRunLimited(hasMovementLimitSurvivor(object.effects(), instance.applicationOrder));
    }
}

EffectInstance WalkAnimationEffect::saveFacingInstance() const {
    auto record = Effect::saveFacingInstance();
    record.serializedType = SavedEffectTypes::walkAnimation;
    return record;
}

EffectApplicationResult WalkAnimationEffect::onApply(Object &object, EffectInstance &) {
    return isa<Creature>(&object) ? EffectApplicationResult::Retained : EffectApplicationResult::Rejected;
}

EffectInstance LightEffect::saveFacingInstance() const {
    auto record = Effect::saveFacingInstance();
    record.serializedType = SavedEffectTypes::light;
    return record;
}

// State packages

static EffectInstance makeStatePackage(Object &target, const std::shared_ptr<Effect> &effect,
                                       float duration, const std::shared_ptr<Object> &creator) {
    effect->setSaveFacingCreator(creator);
    auto record = effect->saveFacingInstance();
    record.effect = effect;
    record.id = target.game().allocateEffectId();
    record.setDuration(DurationType::Temporary, duration);
    record.setCategory(EffectCategory::Magical);
    record.exposed = 1;
    return record;
}

static void applyStateVisual(Object &target, int visual, const std::shared_ptr<Object> &creator) {
    auto effect = std::make_shared<VisualEffectMarkerEffect>(visual);
    effect->setSaveFacingCreator(creator);
    target.applyEffect(effect, DurationType::Instant, 0.0f);
}

static int stateVisual(CreatureState state) {
    switch (state) {
    case CreatureState::Fear:
        return kFearVisual;
    case CreatureState::Stun:
    case CreatureState::Confusion:
        return kStunVisual;
    case CreatureState::Paralysis:
        return kParalysisVisual;
    default:
        return kSleepVisual;
    }
}

static int stateIcon(CreatureState state) {
    switch (state) {
    case CreatureState::Fear:
        return kFearIcon;
    case CreatureState::Confusion:
        return kConfusionIcon;
    case CreatureState::Stun:
        return kStunIcon;
    case CreatureState::Paralysis:
        return kParalysisIcon;
    default:
        return kSleepIcon;
    }
}

StateApplicationResult applyStatePackage(Object &target, CreatureState state, float duration,
                                         const std::shared_ptr<Object> &creator) {
    if (!stateHasConsumer(state)) {
        return StateApplicationResult::Rejected;
    }
    std::shared_ptr<CreatureStateEffect> root;
    switch (state) {
    case CreatureState::Stun:
        root = std::make_shared<StunnedEffect>();
        break;
    case CreatureState::Paralysis:
        root = std::make_shared<ParalyzeEffect>();
        break;
    case CreatureState::Sleep:
        root = std::make_shared<SleepEffect>();
        break;
    default:
        root = std::make_shared<CreatureStateEffect>(state);
        break;
    }
    auto package = makeStatePackage(target, root, duration, creator);
    // The onset visual shows before the package. Every member is tried, even
    // after an earlier one is rejected.
    if (state == CreatureState::Sleep) {
        applyStateVisual(target, kSleepOnsetVisual, creator);
    }
    target.applyEffect(package);
    target.applyEffect(package.linkedChild(std::make_shared<VisualEffectMarkerEffect>(stateVisual(state))));
    if (state == CreatureState::Paralysis) {
        target.applyEffect(package.linkedChild(std::make_shared<VisualEffectMarkerEffect>(kParalysisHoldVisual)));
    }
    target.applyEffect(package.linkedChild(std::make_shared<VisualEffectMarkerEffect>(kDebilitatedVisual)));
    target.applyEffect(package.linkedChild(std::make_shared<EffectIconMarkerEffect>(stateIcon(state))));
    return root->result();
}

bool applySlowPackage(Object &target, float duration, const std::shared_ptr<Object> &creator) {
    auto root = std::make_shared<HasteSlowEffect>(false);
    auto package = makeStatePackage(target, root, duration, creator);
    applyStateVisual(target, kSlowOnsetVisual, creator);
    const bool applied = target.applyEffect(package);
    target.applyEffect(package.linkedChild(std::make_shared<VisualEffectMarkerEffect>(kDebilitatedVisual)));
    target.applyEffect(package.linkedChild(std::make_shared<EffectIconMarkerEffect>(kSlowIcon)));
    return applied;
}

// END State packages

EffectApplicationResult KnockdownEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    // Anything but a creature keeps an inert record.
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    auto &game = object.game();
    const auto creator = instance.boundCreator();
    if (creature->hasEffectImmunity(ImmunityType::Knockdown, Creature::asCreature(creator))) {
        addStateImmunityFeedback(game, creator, *creature, CreatureState::Knockdown);
        return EffectApplicationResult::Rejected;
    }
    // A plot creature is not knocked down, nor, in TSL, one already lying in
    // the knocked-down state.
    if (object.plotFlag() ||
        (game.isTSL() && creature->effectState() == static_cast<int>(CreatureState::Knockdown))) {
        return EffectApplicationResult::Rejected;
    }
    // The dead and the downed keep the record and nothing more.
    if (creature->isDead() || creature->isTemporarilyDead()) {
        return EffectApplicationResult::Retained;
    }
    game.combat().holdKnockedDown(*creature);
    // The records the knockdown adds belong to it: they end with it, and a
    // restored knockdown adds them again. Its AI is restricted for a second
    // and a half longer than it lasts, which it never outlives.
    auto ai = instance.linkedChild(std::make_shared<CreatureAIStateEffect>(Creature::kAIStateMaskRestrained));
    ai.markGeneratedForLoad();
    if (!instance.restoring) {
        ai.setDuration(ai.durationType(), instance.duration + kKnockdownAIHoldSeconds);
    }
    object.applyEffect(std::move(ai));
    // A restored creature keeps the command and the pose it was saved with.
    if (!instance.restoring) {
        creature->setCommandable(false);
        // It falls one way when its creator stands in front of it, the other
        // way when the creator is behind it or gone.
        bool front = true;
        if (creator) {
            const auto toCreator = glm::normalize(creator->position() - object.position());
            const float facing = object.getFacing();
            const glm::vec3 forward {-std::sin(facing), std::cos(facing), 0.0f};
            front = glm::dot(toCreator, forward) >= kKnockdownFrontFacing;
        }
        creature->showKnockdownFall(front ? kKnockdownFrontAnimationId : kKnockdownBackAnimationId);
    }
    // TSL also lays it in the knocked-down state for as long as it lasts.
    if (game.isTSL()) {
        auto state = instance.linkedChild(std::make_shared<CreatureStateEffect>(CreatureState::Knockdown));
        state.markGeneratedForLoad();
        if (!instance.restoring) {
            state.setDuration(DurationType::Temporary, instance.duration);
        }
        object.applyEffect(std::move(state));
    }
    return EffectApplicationResult::Retained;
}

void KnockdownEffect::onRemove(Object &object, const EffectInstance &) {
    // A creature that is still alive stands ready and takes commands again.
    if (auto *creature = dyn_cast<Creature>(&object);
        creature && !creature->isDead() && !creature->isTemporarilyDead()) {
        creature->setCommandable(true);
        creature->showPauseReadyAnimation(false);
    }
}

BlindEffect::BlindEffect() :
    CopyableEffect(EffectType::Blindness) {
    setSaveFacingInteger(0, Creature::kBlindnessCounter);
}

EffectApplicationResult BlindEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    const int mask = instance.integerParameter(0);
    if (mask != Creature::kDarknessCounter && mask != Creature::kBlindnessCounter) {
        return EffectApplicationResult::Rejected;
    }
    const auto creator = instance.boundCreator();
    if (mask == Creature::kBlindnessCounter &&
        creature->hasEffectImmunity(ImmunityType::Blindness, Creature::asCreature(creator))) {
        addBlindnessImmunityFeedback(object.game(), creator, *creature);
        return EffectApplicationResult::Rejected;
    }
    if (object.plotFlag()) {
        return EffectApplicationResult::Rejected;
    }
    // A creature with Ultravision or True Seeing sees through darkness.
    if (mask == Creature::kDarknessCounter && (creature->visibilityCounterBits() & Creature::kDarkSightCounters) != 0) {
        return EffectApplicationResult::Retained;
    }
    // The restored root rebuilds its children, so they are never saved.
    const auto child = [&instance](const std::shared_ptr<Effect> &effect) {
        auto result = instance.linkedChild(effect);
        result.markGeneratedForLoad();
        return result;
    };
    auto miss = child(std::make_shared<MissChanceEffect>(kBlindMissChance));
    miss.setIntegerParameter(1, mask == Creature::kDarknessCounter ? 1 : 0);
    object.applyEffect(std::move(miss));
    object.applyEffect(child(std::make_shared<VisionEffect>(VisionEffect::kBlind)));
    object.applyEffect(child(std::make_shared<VisualEffectMarkerEffect>(kBlindVisual)));
    creature->setVisibilityCounter(mask);
    return EffectApplicationResult::Retained;
}

void BlindEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->restoreBlindnessCounter(instance.integerParameter(0), instance.applicationOrder);
    }
}

EffectApplicationResult EntangleEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    auto creator = instance.boundCreator();
    if (creature->hasEffectImmunity(ImmunityType::Entangle, Creature::asCreature(creator))) {
        addEntangleImmunityFeedback(object.game(), creator, *creature);
        return EffectApplicationResult::Rejected;
    }
    if (object.plotFlag()) {
        return EffectApplicationResult::Rejected;
    }
    creature->clearAllActions(true);

    std::vector<EffectInstance> children;
    auto attack = instance.linkedChild(std::make_shared<AttackDecreaseEffect>(kEntangleAttackPenalty, AttackBonus::Misc));
    attack.setIntegerParameter(2, kAllRacesSelector);
    children.push_back(std::move(attack));
    children.push_back(instance.linkedChild(std::make_shared<AbilityDecreaseEffect>(Ability::Dexterity, kEntangleDexterityPenalty)));
    children.push_back(instance.linkedChild(std::make_shared<CreatureAIStateEffect>(Creature::kAIStateMaskRooted)));
    // The restored root rebuilds its children, so they are never saved.
    for (auto &child : children) {
        child.markGeneratedForLoad();
    }
    object.applyEffectPackage(children);
    return EffectApplicationResult::Retained;
}

void EntangleEffect::onRemove(Object &object, const EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object); creature && !creature->isPC()) {
        creature->perceiveAfresh();
    }
}

EffectApplicationResult InvisibilityEffect::onApply(Object &object, EffectInstance &) {
    return EffectApplicationResult::Retained;
}

ParalyzeEffect::ParalyzeEffect(bool bypassPackageInspection) :
    CreatureStateEffect(CreatureState::Paralysis, bypassPackageInspection) {
}

std::shared_ptr<script::EngineType> ParalyzeEffect::cloneForScript() const {
    return std::make_shared<ParalyzeEffect>(*this);
}

SleepEffect::SleepEffect() :
    CreatureStateEffect(CreatureState::Sleep) {
}

std::shared_ptr<script::EngineType> SleepEffect::cloneForScript() const {
    return std::make_shared<SleepEffect>(*this);
}

StunnedEffect::StunnedEffect(bool bypassPackageInspection) :
    CreatureStateEffect(CreatureState::Stun, bypassPackageInspection) {
}

std::shared_ptr<script::EngineType> StunnedEffect::cloneForScript() const {
    return std::make_shared<StunnedEffect>(*this);
}

// Visibility

namespace {

size_t firstBlindness(const Object &object) {
    const auto &effects = object.effects();
    const auto first = std::find_if(effects.begin(), effects.end(), [](const EffectInstance &effect) {
        return effect.serializedType >= SavedEffectTypes::blindness;
    });
    return static_cast<size_t>(first - effects.begin());
}

void removeLinkedBlindness(Object &object, bool removeRoot) {
    // Each darkness blinding is walked live, in the order of the effect list,
    // not removed as a whole package: True Seeing removes its root, Ultravision
    // its siblings.
    for (size_t outer = firstBlindness(object); outer < object.effects().size(); ++outer) {
        const auto root = object.effects()[outer];
        if (root.serializedType > SavedEffectTypes::blindness) {
            break;
        }
        if (root.serializedType != SavedEffectTypes::blindness || root.integerParameter(0) != Creature::kDarknessCounter) {
            continue;
        }
        for (size_t inner = 0; inner < object.effects().size();) {
            const auto candidate = object.effects()[inner];
            if (candidate.serializedType > SavedEffectTypes::missChance) {
                break;
            }
            if (candidate.id != root.id || candidate.applicationOrder == root.applicationOrder) {
                ++inner;
                continue;
            }
            if (removeRoot && outer >= object.effects().size()) {
                break;
            }
            const auto order = removeRoot ? object.effects()[outer].applicationOrder : candidate.applicationOrder;
            if (!object.removeEffectApplication(order)) {
                break;
            }
            outer = firstBlindness(object);
            // True Seeing goes on past the removed record; Ultravision starts
            // over. The root is held by value, so a removal cannot free it.
            inner = removeRoot ? inner + 1 : 0;
        }
    }
}

// Removes and applies again each blinding of the object. With \p freshId each
// is applied as a new effect, under an id of its own.
void reapplyBlindness(Object &object, bool freshId) {
    // The walk goes on by index: removing and adding a blinding again can
    // change which blinding is next.
    for (size_t index = firstBlindness(object); index < object.effects().size(); ++index) {
        auto effect = object.effects()[index];
        if (effect.serializedType > SavedEffectTypes::blindness) {
            break;
        }
        if (effect.serializedType != SavedEffectTypes::blindness) {
            continue;
        }
        if (!object.removeEffectApplication(effect.applicationOrder)) {
            continue;
        }
        effect.restoring = false;
        if (freshId) {
            effect.id = kUnassignedEffectId;
        }
        object.applyEffect(std::move(effect));
    }
}

void removeVisibilityCounter(Object &object, const EffectInstance &instance, EffectType type,
                             int counter, bool trueSeeingGivesUltravision = false) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return;
    }
    creature->restoreVisibilityCounter(type, counter, instance.applicationOrder, trueSeeingGivesUltravision);
    // Ultravision's blindings come back as new effects; True Seeing's keep
    // their ids.
    if (type == EffectType::Ultravision || type == EffectType::TrueSeeing) {
        reapplyBlindness(object, type == EffectType::Ultravision);
    }
    creature->refreshVisibilityPerception();
}

} // namespace

EffectApplicationResult TrueSeeingEffect::onApply(Object &object, EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setVisibilityCounter(Creature::kTrueSeeingCounter);
        removeLinkedBlindness(object, true);
    }
    // Anything but a creature keeps an inert record too.
    return EffectApplicationResult::Retained;
}

void TrueSeeingEffect::onRemove(Object &object, const EffectInstance &instance) {
    // Removing True Seeing while another remains clears True Seeing and gives
    // Ultravision.
    removeVisibilityCounter(object, instance, EffectType::TrueSeeing, Creature::kTrueSeeingCounter, true);
}

EffectApplicationResult SeeInvisibleEffect::onApply(Object &object, EffectInstance &) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->setVisibilityCounter(Creature::kSeeInvisibleCounter);
    }
    return EffectApplicationResult::Retained;
}

void SeeInvisibleEffect::onRemove(Object &object, const EffectInstance &instance) {
    removeVisibilityCounter(object, instance, EffectType::SeeInvisible, Creature::kSeeInvisibleCounter);
}

EffectApplicationResult UltravisionEffect::onApply(Object &object, EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        object.applyEffect(instance.linkedChild(std::make_shared<VisionEffect>(VisionEffect::kUltravision)));
        creature->setVisibilityCounter(Creature::kUltravisionCounter);
        removeLinkedBlindness(object, false);
    }
    // Perception is refreshed once the record is kept, when
    // hasVisibilityCounter() sees it.
    return EffectApplicationResult::Retained;
}

void UltravisionEffect::onRemove(Object &object, const EffectInstance &instance) {
    removeVisibilityCounter(object, instance, EffectType::Ultravision, Creature::kUltravisionCounter);
}

EffectInstance VisionEffect::saveFacingInstance() const {
    auto instance = Effect::saveFacingInstance();
    instance.serializedType = SavedEffectTypes::vision;
    return instance;
}

EffectApplicationResult VisionEffect::onApply(Object &, EffectInstance &) {
    // Vision takes effect at once and is not kept, on any object.
    return EffectApplicationResult::Applied;
}

// END Visibility

} // namespace game

} // namespace reone
