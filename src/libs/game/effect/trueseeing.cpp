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

#include "reone/game/effect/trueseeing.h"

#include "reone/game/effect/seeinvisible.h"
#include "reone/game/effect/ultravision.h"
#include "reone/game/object/creature.h"
#include "reone/system/cast.h"

namespace reone {

namespace game {

namespace {

Creature *effectCreature(Object &object) {
    return dyn_cast<Creature>(&object);
}

bool applyVisibilityCounter(Object &object, uint8_t bit) {
    auto *creature = effectCreature(object);
    if (!creature) {
        return false;
    }
    creature->setVisibilityCounter(bit);
    creature->refreshVisibilityPerception();
    return true;
}

void removeVisibilityCounter(
    Object &object,
    const EffectInstance &instance,
    EffectType type,
    uint8_t bit,
    bool trueSeeingRemovalQuirk = false) {

    auto *creature = effectCreature(object);
    if (!creature) {
        return;
    }
    creature->restoreVisibilityCounter(
        type,
        bit,
        instance.id,
        trueSeeingRemovalQuirk);
    creature->refreshVisibilityPerception();
}

} // namespace

bool TrueSeeingEffect::onApply(Object &object, const EffectInstance &) {
    return applyVisibilityCounter(object, Creature::kTrueSeeingCounter);
}

void TrueSeeingEffect::onRemove(
    Object &object,
    const EffectInstance &instance) {

    // K2's native handler clears True Seeing and sets Ultravision while
    // another True Seeing effect remains. Preserve that observable quirk.
    removeVisibilityCounter(
        object,
        instance,
        EffectType::TrueSeeing,
        Creature::kTrueSeeingCounter,
        true);
}

bool SeeInvisibleEffect::onApply(Object &object, const EffectInstance &) {
    return applyVisibilityCounter(object, Creature::kSeeInvisibleCounter);
}

void SeeInvisibleEffect::onRemove(
    Object &object,
    const EffectInstance &instance) {

    removeVisibilityCounter(
        object,
        instance,
        EffectType::SeeInvisible,
        Creature::kSeeInvisibleCounter);
}

bool UltravisionEffect::onApply(Object &object, const EffectInstance &) {
    return applyVisibilityCounter(object, Creature::kUltravisionCounter);
}

void UltravisionEffect::onRemove(
    Object &object,
    const EffectInstance &instance) {

    removeVisibilityCounter(
        object,
        instance,
        EffectType::Ultravision,
        Creature::kUltravisionCounter);
}

} // namespace game

} // namespace reone
