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

#include "reone/game/script/runner.h"

#include "reone/game/script/savedsituation.h"
#include "reone/game/game.h"
#include "reone/resource/provider/scripts.h"
#include "reone/script/executioncontext.h"
#include "reone/script/routines.h"
#include "reone/script/virtualmachine.h"


using namespace reone::script;

namespace reone {

namespace game {

int ScriptRunner::run(const std::string &resRef, const std::vector<Argument> &args) {
    auto program = _scripts.get(resRef);
    if (!program)
        return -1;

    auto ctx = std::make_unique<ExecutionContext>();
    ctx->routines = &_routines;
    ctx->args = args;

    return runProgram(program, std::move(ctx));
}

int ScriptRunner::run(const std::string &resRef, uint32_t callerId) {
    std::vector<Argument> args;
    if (callerId) {
        args.emplace_back(script::ArgKind::Caller, Variable::ofObject(callerId));
    }
    return run(resRef, args);
}

int ScriptRunner::run(
    const SavedScriptContinuation &continuation,
    const Game &game,
    const std::vector<Argument> &args) {
    if (!game.hasPlayableRuntimeSession() || !continuation.isCurrent(game)) {
        return -1;
    }

    // Running the continuation advances its script state, even though this
    // runner works on a copy, so a save may no longer reuse the saved snapshot.
    continuation.markAdvanced();

    auto ctx = std::make_unique<ExecutionContext>();
    ctx->routines = &_routines;
    ctx->args = args;
    ctx->savedState = std::make_shared<ExecutionState>(*continuation._state);
    return runProgram(continuation._state->program, std::move(ctx));
}

int ScriptRunner::runProgram(std::shared_ptr<ScriptProgram> program, std::unique_ptr<ExecutionContext> ctx) {
    if (_runDepth == 0) {
        _instructionsRun = std::make_shared<int>(0);
    }
    ctx->instructionsRun = _instructionsRun;
    ++_runDepth;
    const int result = VirtualMachine(std::move(program), std::move(ctx)).run();
    --_runDepth;
    return result;
}

int ScriptRunner::run(
    const SavedScriptContinuation &continuation,
    const Game &game,
    uint32_t callerId) {
    std::vector<Argument> args;
    if (callerId) {
        args.emplace_back(script::ArgKind::Caller, Variable::ofObject(callerId));
    }
    return run(continuation, game, args);
}

} // namespace game

} // namespace reone
