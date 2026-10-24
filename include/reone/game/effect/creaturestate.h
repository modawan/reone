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

#pragma once

#include <functional>
#include <optional>

#include "../effect.h"

namespace reone {

namespace game {

class Creature;

/** The states a creature can be held in by an effect. */
enum class CreatureState {
    None = 0,
    Confusion = 1,
    Fear = 2,
    DroidStun = 3,
    Stun = 4,
    Paralysis = 5,
    Sleep = 6,
    Choke = 7,
    Horrified = 8,
    ForcePushed = 9,
    Whirlwind = 10,
    ForceJumpedOnto = 13,
    Knockdown = 14,
    Crush = 15,
    DroidConfused = 16,
    MindTrick = 18,
    DroidScramble = 19
};

/** What laying a state on a creature did. */
enum class StateApplicationResult {
    Rejected,
    Immune,
    Dormant,
    Applied
};

/** Holds a creature in a state for the effect's duration. */
class CreatureStateEffect : public CopyableEffect<CreatureStateEffect> {
public:
    /**
     * Holds the creature in \p state. With \p bypassPackageInspection the
     * creature's immunity to the linked package is not checked, nor, except
     * for sleep, fear and both confusions, its immunity to the state.
     */
    explicit CreatureStateEffect(CreatureState state, bool bypassPackageInspection = false);

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override;
    void onRemove(Object &, const EffectInstance &) override;

    /** The state the effect holds the creature in. */
    CreatureState state() const { return _state; }
    /** What the last application did. */
    StateApplicationResult result() const { return _result; }

private:
    CreatureState _state;
    StateApplicationResult _result {StateApplicationResult::Rejected};
};

/** The record that lays the strongest state effect's state on the creature. */
class CreatureStateInternalEffect : public CopyableEffect<CreatureStateInternalEffect> {
public:
    explicit CreatureStateInternalEffect(CreatureState state);

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override;
    void onRemove(Object &, const EffectInstance &) override;
};

/** Takes the actions of \p mask from the creature's AI while it lasts. */
class CreatureAIStateEffect : public CopyableEffect<CreatureAIStateEffect> {
public:
    explicit CreatureAIStateEffect(int mask);

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override;
    void onRemove(Object &, const EffectInstance &) override;
};

/** Shows an effect icon on the creature while it lasts. */
class EffectIconMarkerEffect : public CopyableEffect<EffectIconMarkerEffect> {
public:
    explicit EffectIconMarkerEffect(int iconId);

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override;
    void onRemove(Object &, const EffectInstance &) override;
};

/** Plays a visual effect on the object when applied. */
class VisualEffectMarkerEffect : public CopyableEffect<VisualEffectMarkerEffect> {
public:
    explicit VisualEffectMarkerEffect(int visualEffectId);

    EffectApplicationResult onApply(Object &, EffectInstance &) override;
};

/** Keeps the creature from running while it lasts. */
class LimitMovementSpeedEffect : public CopyableEffect<LimitMovementSpeedEffect> {
public:
    LimitMovementSpeedEffect() :
        CopyableEffect(EffectType::Invalid) {
        setSaveFacingInteger(0, 0);
    }

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override;
    void onRemove(Object &, const EffectInstance &) override;
};

/** Records a creature's special walk animation (an iprp_walk row). */
class WalkAnimationEffect : public CopyableEffect<WalkAnimationEffect> {
public:
    explicit WalkAnimationEffect(int walkAnimation) :
        CopyableEffect(EffectType::Invalid) {
        setSaveFacingInteger(0, walkAnimation);
    }

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override;
};

/**
 * Records the visual effect that lights its holder; the visual itself is a
 * separate application from the same source.
 */
class LightEffect : public CopyableEffect<LightEffect> {
public:
    explicit LightEffect(int visualEffectId) :
        CopyableEffect(EffectType::Invalid) {
        setSaveFacingInteger(0, visualEffectId);
    }

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override { return EffectApplicationResult::Retained; }
};

/**
 * Whether \p target is immune to \p state through an immunity to that state
 * alone, from an effect applied against \p creator if there is one.
 */
bool hasStateSpecificImmunity(const Creature &target, CreatureState state, std::optional<std::reference_wrapper<const Creature>> creator);

/** Whether \p target is immune to \p state, specifically or through mind immunity. */
bool hasStateImmunity(const Creature &target, CreatureState state, std::optional<std::reference_wrapper<const Creature>> creator);

/** Whether \p target is immune to being slowed. */
bool hasSlowImmunity(const Creature &target, std::optional<std::reference_wrapper<const Creature>> creator);

/** Lays \p state on the object with its icon and visual, for \p duration seconds. */
StateApplicationResult applyStatePackage(Object &object, CreatureState state, float duration, const std::shared_ptr<Object> &creator);

/** Slows the object with its icon and visual, for \p duration seconds; false when it is immune. */
bool applySlowPackage(Object &object, float duration, const std::shared_ptr<Object> &creator);

/** Whether a movement limit other than the one applied in \p removingOrder remains. */
template <class Records>
inline bool hasMovementLimitSurvivor(const Records &records, uint64_t removingOrder) {
    for (const auto &record : records) {
        if (record.serializedType > SavedEffectTypes::limitMovementSpeed) {
            break;
        }
        if (record.serializedType == SavedEffectTypes::limitMovementSpeed && record.applicationOrder != removingOrder) {
            return true;
        }
    }
    return false;
}

/** Whether a creature held in \p state cannot act. */
inline bool stateBlocksActions(CreatureState state) {
    switch (state) {
    case CreatureState::Stun:
    case CreatureState::Paralysis:
    case CreatureState::Sleep:
    case CreatureState::DroidStun:
    case CreatureState::Choke:
    case CreatureState::Horrified:
    case CreatureState::Whirlwind:
    case CreatureState::MindTrick:
    case CreatureState::DroidScramble:
    case CreatureState::Crush:
    case CreatureState::ForceJumpedOnto:
        return true;
    default:
        return false;
    }
}

/** Whether \p state changes how a creature behaves. */
inline bool stateHasConsumer(CreatureState state) {
    switch (state) {
    case CreatureState::Fear:
    case CreatureState::Confusion:
    case CreatureState::DroidConfused:
    case CreatureState::Knockdown:
    case CreatureState::ForcePushed:
        return true;
    default:
        return stateBlocksActions(state);
    }
}

/**
 * The ambient animation state a creature held by the state takes, which
 * chooses the pose it holds. KotOR knows none of the TSL-only states.
 */
inline int getStateAmbientCode(CreatureState state, bool tsl) {
    // State 12, which has no name, holds the pose of ambient state 10.
    if (static_cast<int>(state) == 12) {
        return 10;
    }
    switch (state) {
    case CreatureState::DroidStun:
        return 8;
    case CreatureState::Stun:
        return 2;
    case CreatureState::Paralysis:
        return 12;
    case CreatureState::Sleep:
        return 11;
    case CreatureState::Choke:
        return 5;
    case CreatureState::Horrified:
        return 7;
    case CreatureState::Whirlwind:
        return 6;
    case CreatureState::ForcePushed:
        return 9;
    case CreatureState::ForceJumpedOnto:
        return 13;
    case CreatureState::Knockdown:
        // A knocked-down creature lies prone.
        return tsl ? 15 : 0;
    case CreatureState::Crush:
        return tsl ? 16 : 0;
    case CreatureState::MindTrick:
    case CreatureState::DroidScramble:
        return tsl ? 2 : 0;
    default:
        return 0;
    }
}

/**
 * Whether laying the state sets the creature's pose afresh. The states that
 * hold its AI do, from droid stun to whirlwind, and in TSL every state from
 * knockdown to droid scramble but droid confusion; the others leave it the
 * animation it is playing.
 */
inline bool stateAppliesPose(int state, bool tsl) {
    if (state >= static_cast<int>(CreatureState::DroidStun) && state <= static_cast<int>(CreatureState::Whirlwind)) {
        return true;
    }
    return tsl && state >= static_cast<int>(CreatureState::Knockdown) &&
           state <= static_cast<int>(CreatureState::DroidScramble) &&
           state != static_cast<int>(CreatureState::DroidConfused);
}

/**
 * Whether lifting the state runs the creature's end-of-round script:
 * confusion and every state from droid stun to whirlwind, and in TSL every
 * state from knockdown to droid scramble.
 */
inline bool stateRemovalRunsEndRoundScript(int state, bool tsl) {
    if (state == static_cast<int>(CreatureState::Confusion)) {
        return true;
    }
    if (state >= static_cast<int>(CreatureState::DroidStun) && state <= static_cast<int>(CreatureState::Whirlwind)) {
        return true;
    }
    return tsl && state >= static_cast<int>(CreatureState::Knockdown) && state <= static_cast<int>(CreatureState::DroidScramble);
}

/**
 * Whether lifting the state sets the creature's pose afresh: the states whose
 * end runs the end-of-round script, state 12, and being jumped onto.
 */
inline bool stateRemovalAppliesPose(int state, bool tsl) {
    return stateRemovalRunsEndRoundScript(state, tsl) || state == 12 || state == static_cast<int>(CreatureState::ForceJumpedOnto);
}

/**
 * The internal record that lays the root's state. A root being applied lays it
 * for as long as it lasts, with its own spell; when a root is removed, the
 * strongest remaining one lays it for good, with no spell.
 */
inline EffectInstance makeInternalStateInstance(const EffectInstance &root, bool afterRemoval) {
    auto effect = std::make_shared<CreatureStateInternalEffect>(static_cast<CreatureState>(root.integerParameter(0)));
    auto descriptor = effect->saveFacingInstance();
    auto result = root.linkedChild(effect);
    result.id = kUnassignedEffectId;
    result.setDurationType(descriptor.durationType());
    result.setCategory(descriptor.category());
    result.markGeneratedForLoad();
    result.restoring = root.restoring;
    if (afterRemoval) {
        result.spellId = UINT32_MAX;
    }
    result.setDuration(afterRemoval ? DurationType::Permanent : DurationType::Innate, 0.0f);
    return result;
}

} // namespace game

} // namespace reone
