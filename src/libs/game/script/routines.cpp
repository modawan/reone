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

#include "reone/game/script/routines.h"

#include <set>

#include "reone/game/di/services.h"
#include "reone/game/effect.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/script/routine/context.h"
#include "reone/game/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone::resource;
using namespace reone::script;

namespace reone {

namespace game {

static constexpr int kBaseItemInvalid = 256;

namespace {

// What an effect constructor routine stamps on the effect it builds. Most
// make it magical and give it OBJECT_SELF as its creator (and the creator's
// spell); a few make it magical but take OBJECT_SELF as the creator only when
// it is a creature; a few give it only the creator, and a few neither, leaving
// the effect without a category, creator or spell.
enum class EffectConstructorStamp {
    MagicalWithCreator,
    MagicalWithCreatureCreator,
    CreatorOnly,
    None
};

EffectConstructorStamp getEffectConstructorStamp(const std::string &name) {
    static const std::set<std::string> kUnstamped {
        "EffectBlasterDeflectionDecrease", "EffectBlasterDeflectionIncrease", "EffectBodyFuel",
        "EffectChoke", "EffectCrush", "EffectDamageForcePoints", "EffectDroidStun",
        "EffectHealForcePoints", "EffectHorrified", "EffectLightsaberThrow", "EffectPsychicStatic",
        "EffectWhirlWind"};
    static const std::set<std::string> kCreatureCreator {
        "EffectHeal", "EffectTemporaryForcePoints", "EffectTemporaryHitpoints"};
    static const std::set<std::string> kCreatorOnly {
        "EffectBlind", "EffectFPRegenModifier", "EffectFactionModifier", "EffectForceBody",
        "EffectForceJump", "EffectForcePushTargeted", "EffectForcePushed", "EffectForceShield",
        "EffectForceSight", "EffectFury", "EffectHitPointChangeWhenDying", "EffectVPRegenModifier"};
    if (kUnstamped.count(name)) {
        return EffectConstructorStamp::None;
    }
    if (kCreatureCreator.count(name)) {
        return EffectConstructorStamp::MagicalWithCreatureCreator;
    }
    if (kCreatorOnly.count(name)) {
        return EffectConstructorStamp::CreatorOnly;
    }
    return EffectConstructorStamp::MagicalWithCreator;
}

} // namespace

void Routines::init() {
    if (_gameId == GameID::TSL) {
        registerTslRoutines();
    } else {
        registerKotorRoutines();
    }
}

void Routines::registerKotorRoutines() {
    registerMainKotorRoutines();
    registerActionKotorRoutines();
    registerEffectKotorRoutines();
    registerMinigameKotorRoutines();
}

void Routines::registerTslRoutines() {
    registerMainTslRoutines();
    registerActionTslRoutines();
    registerEffectTslRoutines();
    registerMinigameTslRoutines();
}

Routine &Routines::get(int index) {
    if (_routines.count(index) == 0) {
        throw std::out_of_range("index out of range: " + std::to_string(index));
    }
    return _routines.at(index);
}

int Routines::getIndexByName(const std::string &name) const {
    for (auto it = _routines.begin(); it != _routines.end(); ++it) {
        if (it->second.name() == name) {
            return it->first;
        }
    }
    return -1;
}

void Routines::insert(
    int index,
    std::string name,
    VariableType retType,
    std::vector<VariableType> argTypes,
    Variable (*fn)(const std::vector<Variable> &args, const RoutineContext &ctx)) {

    Variable defRetValue;
    defRetValue.type = retType;
    switch (retType) {
    case VariableType::Float:
        defRetValue.floatValue = -1.0f;
        break;
    case VariableType::Object:
        defRetValue.objectId = kObjectInvalid;
        break;
    default:
        break;
    }

    const bool constructsEffect =
        retType == VariableType::Effect && name.rfind("Effect", 0) == 0;
    const auto stamp = constructsEffect ? getEffectConstructorStamp(name) : EffectConstructorStamp::None;
    _routines[index] = Routine(
        std::move(name),
        retType,
        std::move(defRetValue),
        std::move(argTypes),
        [this, fn, retType, constructsEffect, stamp](auto &args, auto &execution) {
            RoutineContext ctx(*_game, *_services, execution);
            auto result = fn(args, std::move(ctx));
            // An effect a script constructs is stamped with OBJECT_SELF.
            if (constructsEffect) {
                auto effect = std::dynamic_pointer_cast<Effect>(result.engineType);
                if (effect) {
                    effect->setSaveFacingId(_game->allocateEffectId());
                    effect->captureSaveFacingScriptArguments(args, *_game);
                    // The constructed value itself becomes magical; linked members
                    // keep their own values until the link is applied.
                    if (stamp == EffectConstructorStamp::MagicalWithCreator ||
                        stamp == EffectConstructorStamp::MagicalWithCreatureCreator)
                        effect->Effect::setCategory(EffectCategory::Magical);
                }
                auto caller = execution.findArg(ArgKind::Caller);
                // A creator-only constructor that yields an effect of no type
                // gives it no creator.
                const bool typeless = effect && effect->type() == EffectType::Invalid;
                if (effect && caller && stamp != EffectConstructorStamp::None &&
                    !(stamp == EffectConstructorStamp::CreatorOnly && typeless)) {
                    auto creator = _game->getObjectById(caller->objectId);
                    if (stamp != EffectConstructorStamp::MagicalWithCreatureCreator ||
                        std::dynamic_pointer_cast<Creature>(creator)) {
                        effect->setCreatorFromCaller(creator);
                    }
                }
            }
            return result;
        });
}

} // namespace game

} // namespace reone
