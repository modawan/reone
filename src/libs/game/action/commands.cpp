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

#include "reone/game/action/docommand.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/surrendertoenemies.h"
#include "reone/game/action/wait.h"
#include "reone/game/animationutil.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object.h"
#include "reone/game/savedruntime.h"
#include "reone/game/script/savedsituation.h"
#include "reone/graphics/animation.h"
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

    SavedActionRecord result =
        originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 37;
    result.declaredParameterCount = 1;
    result.parameters = {SavedActionParameter {
        static_cast<uint32_t>(SavedActionParameterType::ScriptSituation),
        std::move(*situation)}};
    return result;
}

void PlayAnimationAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (_playing) {
        _timer.update(dt);
        if (_timer.elapsed()) {
            complete();
        }
        return;
    }

    bool looping = _looping.value_or(isAnimationLooping(_animation));
    if (looping) {
        // Looping animations never finish. Complete the action immediately to
        // avoid stalling the action queue.
        if (_durationSeconds < 0.0f) {
            complete();
        } else {
            _timer.reset(_durationSeconds);
        }
    } else {
        // Set the timer to match duration of the animation.
        auto node = actor.sceneNode();
        if (node->type() != SceneNodeType::Model) {
            complete();
            return;
        }

        const graphics::Model &model = std::static_pointer_cast<ModelSceneNode>(node)->model();
        std::shared_ptr<graphics::Animation> anim = model.getAnimation(actor.getAnimationName(_animation));
        if (!anim) {
            complete();
            return;
        }

        _timer.reset(anim->length());
    }

    AnimationProperties properties;
    properties.speed = _speed;
    properties.duration = _durationSeconds;
    actor.playAnimation(_animation, std::move(properties));
    _playing = true;
}

std::optional<SavedActionRecord> PlayAnimationAction::saveFacingState() const {
    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 6;
    result.declaredParameterCount = 5;
    result.parameters = {
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Object),
            SavedObjectReference::fromRuntimeId(
                static_cast<uint32_t>(_animation))},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Float), _speed},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Float),
            _playing ? _timer.remaining() : _durationSeconds},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Integer),
            static_cast<int32_t>(_playing ? 0 : 1)},
        SavedActionParameter {
            static_cast<uint32_t>(SavedActionParameterType::Integer),
            static_cast<int32_t>(_looping.value_or(isAnimationLooping(_animation)) ? 1 : 0)},
    };
    return result;
}

void WaitAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    _timer.update(dt);
    if (_timer.elapsed()) {
        complete();
    }
}

std::optional<SavedActionRecord> WaitAction::saveFacingState() const {
    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 30;
    result.declaredParameterCount = 1;
    result.parameters = {SavedActionParameter {
        static_cast<uint32_t>(SavedActionParameterType::Float),
        _timer.remaining()}};
    return result;
}

void SurrenderToEnemiesAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // TODO: implement

    complete();
}

} // namespace game

} // namespace reone
