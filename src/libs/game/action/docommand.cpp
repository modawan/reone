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

#include "reone/script/executioncontext.h"
#include "reone/script/executionstate.h"
#include "reone/script/program.h"
#include "reone/script/virtualmachine.h"

#include "reone/game/modulesnapshot.h"
#include "reone/game/object.h"
#include "reone/game/script/savedsituation.h"
#include "reone/system/exception/validation.h"

using namespace reone::script;

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
        throw ValidationException(
            "DoCommand continuation is not serializable: " + error);
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

} // namespace game

} // namespace reone
