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

#include <cmath>

#include "reone/game/action/appear.h"
#include "reone/game/action/docommand.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/surrendertoenemies.h"
#include "reone/game/action/wait.h"
#include "reone/game/animationutil.h"
#include "reone/game/game.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/savedruntime.h"
#include "reone/game/script/savedsituation.h"
#include "reone/scene/animproperties.h"
#include "reone/script/executioncontext.h"
#include "reone/script/executionstate.h"
#include "reone/script/program.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/exception/validation.h"

using namespace reone::script;
using namespace reone::scene;

namespace reone {

namespace game {

void runCommandAsActor(const ExecutionContext &command, Object &actor) {
    auto executionCtx = std::make_unique<ExecutionContext>(command);

    // ExecutionContext may be applied to another actor - update the Caller
    // argument to match. We keep other arguments intact because this is a
    // continuation of the original context.
    //
    // For example, if a context starts as an onOpen script of a door, saves
    // state, and reassigns itself via AssignCommand to a character - this
    // continuation should to keep LastOpenedBy argument and return it via
    // GetLastOpenedBy routine.
    //
    // Besides the Caller, scripts do not seem to use other arguments with
    // AssignCommand.

    bool foundCaller = false;
    for (Argument &arg : executionCtx->args) {
        if (arg.kind == script::ArgKind::Caller) {
            arg.var.objectId = actor.id();
            foundCaller = true;
            break;
        }
    }
    if (!foundCaller) {
        executionCtx->args.emplace_back(script::ArgKind::Caller,
                                        Variable::ofObject(actor.id()));
    }

    std::shared_ptr<ScriptProgram> program(command.savedState->program);
    VirtualMachine(program, std::move(executionCtx)).run();
}

void DoCommandAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    runCommandAsActor(*_actionToDo, actor);
    complete();
}

std::optional<SavedActionRecord> DoCommandAction::saveFacingState() const {
    if (!_actionToDo || !_actionToDo->savedState ||
        !_actionToDo->savedState->program) {
        throw ValidationException("DoCommand action has no serializable continuation");
    }

    auto continuation = SavedScriptContinuation::fromRuntime(
        _actionToDo->savedState,
        _actionToDo->savedState->program->name(),
        _game);
    std::string error;
    auto situation = exportScriptSituation(*continuation, error);
    if (!situation) {
        const auto &state = *_actionToDo->savedState;
        std::ostringstream message;
        message << "DoCommand continuation is not serializable: " << error
                << "; script=\"" << state.program->name() << '\"'
                << " continuationProvenance=absent"
                << " continuationOrigin=runtime-created"
                << " globals=" << state.globals.size()
                << " locals=" << state.locals.size()
                << " runtimeBP=" << state.globals.size()
                << " runtimeSP=" << state.globals.size() + state.locals.size()
                << " instructionOffset=" << state.insOffset;
        throw ValidationException(message.str());
    }

    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::DoCommand);
    result.declaredParameterCount = 1;
    result.parameters = {SavedActionParameter {
        static_cast<uint32_t>(SavedActionParameterType::ScriptSituation),
        std::move(*situation)}};
    return result;
}

// An animation the actor has no clip for still takes a second.
static constexpr float kNoClipSeconds = 1.0f;
static constexpr float kCoalescingSeconds = 30.0f;
// The animation constants a negative duration plays at once: in KotOR those up
// to this one, in TSL those below the first and from the second on.
static constexpr int kLastImmediateAnimation = 32;
static constexpr int kFirstQueuedAnimationTSL = 47;
static constexpr int kFirstImmediateDialogAnimationTSL = 10001;

void requestScriptAnimation(Game &game, Object &caller, int constant, float speed, float seconds, bool replaceActions) {
    auto *creature = dyn_cast<Creature>(&caller);
    if (creature && game.isConversationSpeakerOrListener(caller)) {
        creature->playScriptAnimation(creature->getScriptAnimation(static_cast<int>(AnimationType::LoopingPause)), 1.0f);
    }
    if (seconds < 0.0f) {
        const bool direct = game.isTSL() ? (constant < kFirstQueuedAnimationTSL || constant >= kFirstImmediateDialogAnimationTSL)
                                         : constant <= kLastImmediateAnimation;
        if (direct && creature) {
            creature->playScriptAnimation(creature->getScriptAnimation(constant), 1.0f);
        }
        return;
    }
    if (!caller.isCommandable()) {
        return;
    }
    auto action = game.newAction<PlayAnimationAction>(static_cast<AnimationType>(constant), speed, seconds);
    if (replaceActions) {
        caller.clearAllActions();
        caller.addActionOnTop(std::move(action));
    } else {
        caller.addAction(std::move(action));
    }
}

// The clip plays for its length at the speed given, and a loop given a
// duration plays for that long; without a clip the action takes a second.
// From 30 seconds on, the same animation queued right behind it at the same
// speed is dropped. At the end a creature goes back to its pause or ready
// pose.
void PlayAnimationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto *creature = dyn_cast<Creature>(&actor);
    // A downed party member cannot play it.
    if (creature && creature->isTemporarilyDead()) {
        complete();
        return;
    }
    const int constant = static_cast<int>(_animation);
    // The clip is looked up only while it is still to be timed or shown.
    std::optional<Creature::ScriptAnimation> animation;
    if (creature && (!_playing || !_shown)) {
        animation = creature->getScriptAnimation(constant);
    }
    if (_playing) {
        _timer.update(dt);
    } else {
        float duration = animation && animation->length > 0.0f ? animation->length : kNoClipSeconds;
        if (_speed != 0.0f) {
            duration /= std::abs(_speed);
        }
        const bool fireForget = animation && !animation->loop;
        if (_durationSeconds > 0.0f && !fireForget) {
            duration = _durationSeconds;
        }
        _timer.reset(duration);
        _playing = true;
    }
    if (_durationSeconds >= kCoalescingSeconds) {
        const bool characterModel = creature && creature->modelType() != Creature::ModelType::Creature;
        const int id = scriptAnimationId(constant, _game.isTSL(), characterModel);
        actor.removeActionsBehind(*this, [&](const Action &next) {
            if (next.type() != ActionType::PlayAnimation) {
                return false;
            }
            const auto &other = static_cast<const PlayAnimationAction &>(next);
            return scriptAnimationId(static_cast<int>(other.animation()), _game.isTSL(), characterModel) == id &&
                   other.speed() == _speed;
        });
    }
    if (!_timer.elapsed()) {
        if (!_shown) {
            if (animation) {
                creature->playScriptAnimation(*animation, _speed);
            } else {
                AnimationProperties properties;
                properties.speed = _speed;
                actor.playAnimation(_animation, std::move(properties));
            }
            _shown = true;
        }
        return;
    }
    if (creature) {
        creature->refreshPauseAnimation();
    }
    complete();
}

std::optional<SavedActionRecord> PlayAnimationAction::saveFacingState() const {
    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::PlayAnimation);
    result.declaredParameterCount = 5;
    result.parameters = {
        SavedActionParameter {
            kSavedObjectParameter,
            SavedObjectReference::rawValue(
                static_cast<uint32_t>(_animation))},
        SavedActionParameter {
            kSavedFloatParameter, _speed},
        SavedActionParameter {
            kSavedFloatParameter,
            _playing ? _timer.remaining() : _durationSeconds},
        SavedActionParameter {
            kSavedIntegerParameter,
            static_cast<int32_t>(_playing ? 0 : 1)},
        SavedActionParameter {
            kSavedIntegerParameter,
            static_cast<int32_t>(_looping.value_or(isAnimationLooping(_animation)) ? 1 : 0)},
    };
    return result;
}

std::shared_ptr<Action> PlayAnimationAction::restore(const SavedActionRecord &record, Game &game) {
    // The animation is kept in the command's object slot. Any animation
    // constant the command queues comes back; it is resolved as the action runs.
    const auto animation = record.objectParameter(0).id;
    const float speed = record.floatParameter(1);
    const float duration = record.floatParameter(2);
    const int32_t start = record.integerParameter(3);
    const int32_t looping = record.integerParameter(4);
    if (!std::isfinite(speed) || !std::isfinite(duration) ||
        (start != 0 && start != 1) || (looping != 0 && looping != 1)) {
        return nullptr;
    }
    return game.newAction<PlayAnimationAction>(static_cast<AnimationType>(animation), speed, duration, start == 0, looping != 0);
}

void AppearAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    _timer.update(dt);
    if (!_timer.elapsed()) {
        return;
    }
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->resumeStateDrivenAnimation();
    }
    complete();
}

// An appearance taken up again holds the creature for its full two seconds.
std::optional<SavedActionRecord> AppearAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::Appear, {});
}

std::shared_ptr<Action> AppearAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<AppearAction>();
}

void WaitAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    _timer.update(dt);
    if (_timer.elapsed()) {
        complete();
    }
}

std::optional<SavedActionRecord> WaitAction::saveFacingState() const {
    SavedActionRecord result;
    result.actionId = static_cast<uint32_t>(SavedActionType::Wait);
    result.declaredParameterCount = 1;
    result.parameters = {SavedActionParameter {
        kSavedFloatParameter,
        _timer.remaining()}};
    return result;
}

std::shared_ptr<Action> WaitAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<WaitAction>(record.floatParameter(0));
}

// Only non-player creatures are given this action; a player character's
// surrender fails.
void SurrenderToEnemiesAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto &creature = static_cast<Creature &>(actor);
    if (!creature.isPC()) {
        creature.surrenderToEnemies(false);
    }
    complete();
}

std::optional<SavedActionRecord> SurrenderToEnemiesAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::SurrenderToEnemies, {});
}

std::shared_ptr<Action> SurrenderToEnemiesAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<SurrenderToEnemiesAction>();
}

} // namespace game

} // namespace reone
