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

#include "reone/game/effect/poison.h"

#include <array>
#include <cmath>
#include <cstdint>

#include "reone/game/combatfeedback.h"
#include "reone/game/combattables.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damageforcepoints.h"
#include "reone/game/effect/forceresisted.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

// The visual a poisoned creature shows (a visualeffects.2da row).
static constexpr int kPoisonedVisual = 1003;

// The periodic factor a poison starts with, after its first damage.
static constexpr float kInitialPoisonFactor = 2.0f;

namespace {

int poisonAbilityAmount(int base, float factor) {
    return base * static_cast<int>(factor);
}

int poisonAbilityDuration(int duration, int period, float factor) {
    return duration - (static_cast<int>(factor) - 1) * period;
}

/**
 * The timing of a poison, read from its record and written back to it at
 * each update; the record alone holds it.
 */
struct PoisonState {
    enum class Step {
        None,
        Damage,
        Conversation,
        Expired
    };

    uint32_t startDay {0};
    uint32_t startTime {0};
    uint32_t tickDay {0};
    uint32_t tickTime {0};
    int durationSeconds {0};
    int periodSeconds {0};
    float factor {kInitialPoisonFactor};

    void start(uint32_t day, uint32_t time, int duration, int period) {
        startDay = tickDay = day;
        startTime = tickTime = time;
        durationSeconds = duration;
        periodSeconds = period;
        factor = kInitialPoisonFactor;
    }

    /**
     * \p age is the time of day part of the world time since the start and
     * \p elapsed that since the last tick; whole days do not count. A period
     * that passes in conversation deals no damage and lengthens the poison
     * by a period.
     */
    Step step(uint32_t age, uint32_t elapsed, uint32_t day, uint32_t time, bool conversation) {
        if (age > static_cast<uint32_t>(durationSeconds) * 1000U) {
            return Step::Expired;
        }
        if (elapsed <= static_cast<uint32_t>(periodSeconds) * 1000U) {
            return Step::None;
        }
        if (conversation) {
            tickDay = day;
            tickTime = time;
            durationSeconds += periodSeconds;
            return Step::Conversation;
        }
        return Step::Damage;
    }

    /** A tick's damage was dealt: the factor grows and the tick is timed from now. */
    void completeTick(uint32_t day, uint32_t time) {
        factor += 1.0f;
        tickDay = day;
        tickTime = time;
    }
};

// The record keeps the poison's row in integer 0, its start day and time in
// 1 and 2, its last tick's in 3 and 4, its duration and period in 5 and 6,
// and its periodic factor in float 0.
PoisonState readPoisonState(const EffectInstance &record) {
    PoisonState state;
    state.startDay = static_cast<uint32_t>(record.integerParameter(1));
    state.startTime = static_cast<uint32_t>(record.integerParameter(2));
    state.tickDay = static_cast<uint32_t>(record.integerParameter(3));
    state.tickTime = static_cast<uint32_t>(record.integerParameter(4));
    state.durationSeconds = record.integerParameter(5);
    state.periodSeconds = record.integerParameter(6);
    state.factor = record.floatParameters[0];
    return state;
}

void storePoisonState(EffectInstance &record, const PoisonState &state) {
    record.setIntegerParameter(1, static_cast<int>(state.startDay));
    record.setIntegerParameter(2, static_cast<int>(state.startTime));
    record.setIntegerParameter(3, static_cast<int>(state.tickDay));
    record.setIntegerParameter(4, static_cast<int>(state.tickTime));
    record.setIntegerParameter(5, state.durationSeconds);
    record.setIntegerParameter(6, state.periodSeconds);
    record.floatParameters[0] = state.factor;
}

} // namespace

EffectApplicationResult PoisonEffect::queueRemoval(Object &object, EffectId id) {
    object.game().queueEffectRemoval(object, id);
    return EffectApplicationResult::Retained;
}

EffectApplicationResult PoisonEffect::onApply(Object &object, EffectInstance &instance) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature) {
        return EffectApplicationResult::Retained;
    }
    if (object.plotFlag() || creature->isDead() || creature->isTemporarilyDead() ||
        creature->activePoisonEffectId() != kUnassignedEffectId) {
        return queueRemoval(object, instance.id);
    }
    auto &game = object.game();
    auto &services = object.services();
    if (!instance.restoring) {
        auto source = instance.boundCreator();
        if (creature->hasEffectImmunity(ImmunityType::Poison, Creature::asCreature(source))) {
            addPoisonImmunityFeedback(game, source, *creature);
            return queueRemoval(object, instance.id);
        }
        // The start time is kept even when the poison then ends at once.
        const auto day = game.worldTimeDay();
        const auto time = game.worldTimeOfDay();
        instance.setIntegerParameter(1, static_cast<int>(day));
        instance.setIntegerParameter(2, static_cast<int>(time));
        const auto data = services.game.combatTables.poison(instance.integerParameter(0));
        std::optional<std::reference_wrapper<const Object>> versus;
        if (source) {
            versus = *source;
        }
        auto breakdown = creature->getSavingThrowBreakdown(SavingThrow::Fortitude, SavingThrowType::Poison, versus);
        const int roll = randomInt(1, 20);
        const int dc = data.difficultyClass;
        addSavingThrowFeedback(game, *creature, versus, SavingThrow::Fortitude, breakdown, roll, dc);
        if (creature->getSavingThrowResult(roll + breakdown.total(), dc, SavingThrowType::Poison, versus) !=
            SavingThrowResult::Failed) {
            // In TSL a resisted Affliction or Plague shows as resisted.
            const auto spell = static_cast<SpellType>(instance.spellId);
            if (game.isTSL() && (spell == SpellType::Affliction || spell == SpellType::Plague)) {
                auto resisted = std::make_shared<ForceResistedEffect>(source);
                auto marker = resisted->saveFacingInstance();
                marker.effect = resisted;
                marker.objectParameters[0] = instance.creatorId;
                marker.objectParameterObjects[0] = instance.creator;
                object.applyEffect(std::move(marker));
            }
            return queueRemoval(object, instance.id);
        }
        addPoisonedFeedback(game, *creature, data.nameStrRef);
        creature->playSound(resource::SoundSetEntry::Poisoned);
        // The first damage sees the record as it stands but for its start
        // time and factor; the rest of its timing is kept afterwards.
        instance.floatParameters[0] = kInitialPoisonFactor;
        applyDamage(*creature, instance, 1.0f);
        PoisonState state;
        state.start(day, time, data.duration, data.period);
        storePoisonState(instance, state);
        instance.setIntegerParameter(7, 0);
        // The record's own duration is the poison's, in milliseconds.
        instance.setDuration(DurationType::Temporary, static_cast<float>(data.duration) * 1000.0f);
    }
    auto visual = std::make_shared<VisualEffectMarkerEffect>(kPoisonedVisual);
    auto child = visual->saveFacingInstance();
    child.effect = visual;
    child.objectParameters[0] = instance.creatorId;
    child.objectParameterObjects[0] = instance.creator;
    child.setDuration(DurationType::Instant, 0.0f);
    child.restoring = false;
    object.applyEffect(std::move(child));
    creature->setActivePoisonEffectId(instance.id);
    return EffectApplicationResult::Retained;
}

void PoisonEffect::onUpdate(Object &object, const EffectInstance &instance, float) {
    auto *creature = dyn_cast<Creature>(&object);
    if (!creature || creature->activePoisonEffectId() != instance.id) {
        return;
    }
    auto &game = object.game();
    // The poison runs on the creature's own time, which a time stop may leave running.
    const auto day = game.activeTimeDay(object);
    const auto time = game.activeTimeOfDay(object);
    auto state = readPoisonState(instance);
    // Only the time of day part of each difference counts.
    uint32_t days = 0;
    uint32_t age = 0;
    uint32_t elapsed = 0;
    game.subtractWorldTimes(day, time, state.startDay, state.startTime, days, age);
    game.subtractWorldTimes(day, time, state.tickDay, state.tickTime, days, elapsed);
    switch (state.step(age, elapsed, day, time, game.isConversationActive())) {
    case PoisonState::Step::Expired:
        queueRemoval(object, instance.id);
        return;
    case PoisonState::Step::Damage: {
        // The damage changes the effect list, which \p instance is part of.
        const EffectInstance source = instance;
        applyDamage(*creature, source, state.factor);
        state.completeTick(day, time);
        if (auto record = object.findEffectApplication(source.applicationOrder)) {
            storePoisonState(record->get(), state);
        }
        return;
    }
    case PoisonState::Step::Conversation:
        break;
    case PoisonState::Step::None:
        return;
    }
    if (auto record = object.findEffectApplication(instance.applicationOrder)) {
        storePoisonState(record->get(), state);
    }
}

void PoisonEffect::applyDamage(Creature &creature, const EffectInstance &source, float factor) {
    const auto data = creature.services().game.combatTables.poison(source.integerParameter(0));
    // The poison's damage is part of it, and hidden from scripts.
    const auto child = [&](const std::shared_ptr<Effect> &effect, DurationType type, float seconds) {
        auto result = source.linkedChild(effect);
        result.setDuration(type, seconds);
        result.restoring = false;
        result.exposed = 0;
        return result;
    };
    if (data.hitPointDamage > 0) {
        auto damage = std::make_shared<DamageEffect>(data.hitPointDamage, DamageType::Poison, DamagePower::Normal, true);
        creature.game().queueEffectApplication(creature, child(damage, DurationType::Instant, 0.0f));
    }
    if (data.forcePointDamage > 0) {
        creature.applyEffect(child(std::make_shared<DamageForcePointsEffect>(data.forcePointDamage), DurationType::Instant, 0.0f));
    }
    const int remaining = poisonAbilityDuration(data.duration, data.period, factor);
    for (size_t ability = 0; ability < data.abilityDamage.size(); ++ability) {
        if (data.abilityDamage[ability] <= 0 || remaining <= 0) {
            continue;
        }
        // The ability damage replaces that of the same spell: each decrease of
        // the ability with the poison's spell is removed, walking on by index
        // without starting over.
        for (size_t index = 0; index < creature.effects().size(); ++index) {
            const auto record = creature.effects()[index];
            if (record.type() == EffectType::AbilityDecrease && record.spellId == source.spellId &&
                record.integerParameter(0) == static_cast<int>(ability)) {
                creature.removeEffectApplication(record.applicationOrder);
            }
        }
        auto decrease = std::make_shared<AbilityDecreaseEffect>(static_cast<Ability>(ability),
                                                                poisonAbilityAmount(data.abilityDamage[ability], factor));
        creature.applyEffect(child(decrease, DurationType::Temporary, static_cast<float>(remaining)));
    }
}

void PoisonEffect::onRemove(Object &object, const EffectInstance &instance) {
    if (auto *creature = dyn_cast<Creature>(&object)) {
        creature->clearActivePoisonEffectId(instance.id);
    }
}

} // namespace game

} // namespace reone
