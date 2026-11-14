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
#include "reone/system/logutil.h"


using namespace reone::script;

namespace reone {

namespace game {

ScriptRunner::ScriptRunner(script::IRoutines &routines, resource::IScripts &scripts) :
    _routines(routines),
    _scripts(scripts) {
}

ScriptRunner::~ScriptRunner() = default;

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

// A run that stops to wait is kept. Once the outermost run under way has
// stopped with the runs started from it, they wait together.
int ScriptRunner::runProgram(std::shared_ptr<ScriptProgram> program, std::unique_ptr<ExecutionContext> ctx) {
    if (_runDepth == 0) {
        _instructionsRun = std::make_shared<int>(0);
        _yield = std::make_shared<bool>(false);
        _stopped.clear();
    }
    ctx->instructionsRun = _instructionsRun;
    ctx->yield = _yield;
    ++_runDepth;
    auto vm = std::make_unique<VirtualMachine>(std::move(program), std::move(ctx));
    const int result = vm->run();
    --_runDepth;
    if (vm->isWaiting()) {
        _stopped.push_back(std::move(vm));
        if (_runDepth == 0) {
            WaitingRuns waiting;
            waiting.instructionsRun = _instructionsRun;
            waiting.yield = _yield;
            for (auto it = _stopped.rbegin(); it != _stopped.rend(); ++it) {
                waiting.runs.push_back(std::move(*it));
            }
            _stopped.clear();
            *waiting.yield = false;
            _waiting.push_back(std::move(waiting));
        }
    } else if (_runDepth == 0) {
        dropStoppedRuns();
    }
    return result;
}

// The outermost run halted while runs started from it waited: with nothing to
// go on from, they are dropped.
void ScriptRunner::dropStoppedRuns() {
    if (_stopped.empty()) {
        return;
    }
    warn(str(boost::format("Script runner: %d waiting runs dropped, the run they were started from having halted") % _stopped.size()));
    _stopped.clear();
    *_yield = false;
}

// Within a held step, a step whose runs don't wait goes on at once, as
// though the step had never been held; one whose runs wait is held ahead of
// the steps held before.
void ScriptRunner::whenNoRunsWait(std::function<void()> next) {
    if (!hasWaitingRuns() && (_inHeldStep || _held.empty())) {
        next();
        return;
    }
    (_inHeldStep ? _heldInStep : _held).push_back(std::move(next));
}

// Whatever a held step does, thrown out of included, the steps it held go
// ahead of the steps held before it once it ends.
void ScriptRunner::runHeldSteps() {
    struct HeldStepScope {
        ScriptRunner &runner;
        const bool outer;
        explicit HeldStepScope(ScriptRunner &runner) :
            runner(runner),
            outer(!runner._inHeldStep) {
            runner._inHeldStep = true;
        }
        ~HeldStepScope() {
            if (!outer) {
                return;
            }
            runner._inHeldStep = false;
            while (!runner._heldInStep.empty()) {
                runner._held.push_front(std::move(runner._heldInStep.back()));
                runner._heldInStep.pop_back();
            }
        }
    };
    while (!_held.empty() && !hasWaitingRuns()) {
        auto next = std::move(_held.front());
        _held.pop_front();
        HeldStepScope scope(*this);
        next();
    }
}

// The groups go on in the order they stopped, the runs of each innermost
// first, counting their instructions afresh; runs they start belong to the
// group. A run that stops again keeps the runs it was started from waiting
// behind it, and the runs that stopped inside it ahead; its group waits
// again, and the groups after it wait on behind it, as nothing goes on while
// its movie plays. Runs that stopped under an outermost run that then halted
// are dropped, as in runProgram.
void ScriptRunner::resumeWaitingRuns() {
    if (_runDepth != 0) {
        return;
    }
    while (!_waiting.empty()) {
        auto group = std::move(_waiting.front());
        _waiting.erase(_waiting.begin());
        *group.instructionsRun = 0;
        _instructionsRun = group.instructionsRun;
        _yield = group.yield;
        _stopped.clear();
        _runDepth = 1;
        bool waitsAgain = false;
        while (!group.runs.empty()) {
            auto vm = std::move(group.runs.back());
            group.runs.pop_back();
            vm->resume();
            if (vm->isWaiting()) {
                _stopped.push_back(std::move(vm));
                for (auto it = _stopped.rbegin(); it != _stopped.rend(); ++it) {
                    group.runs.push_back(std::move(*it));
                }
                _stopped.clear();
                *group.yield = false;
                waitsAgain = true;
                break;
            }
        }
        _runDepth = 0;
        if (waitsAgain) {
            _waiting.insert(_waiting.begin(), std::move(group));
            return;
        }
        dropStoppedRuns();
    }
    runHeldSteps();
}

void ScriptRunner::dropWaitingRuns() {
    _waiting.clear();
    _held.clear();
    _heldInStep.clear();
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
