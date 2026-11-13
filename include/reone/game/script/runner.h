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

#pragma once

#include <deque>
#include <functional>

#include "reone/script/types.h"
#include "reone/script/variable.h"

namespace reone {

namespace script {

struct ExecutionContext;

class IRoutines;
class ScriptProgram;
class VirtualMachine;

}

namespace resource {

class IScripts;

}

namespace game {

class Game;
class SavedScriptContinuation;

class ScriptRunner {
public:
    ScriptRunner(script::IRoutines &routines, resource::IScripts &scripts);
    ~ScriptRunner();

    int run(
        const std::string &resRef,
        const std::vector<script::Argument> &args);

    // Run a script with a single ArgKind::Caller argument. When callerId is 0,
    // run a script without arguments.
    int run(const std::string &resRef, uint32_t callerId = 0);

    int run(
        const SavedScriptContinuation &continuation,
        const Game &game,
        const std::vector<script::Argument> &args);

    int run(
        const SavedScriptContinuation &continuation,
        const Game &game,
        uint32_t callerId);

    /**
     * Whether runs wait to go on: a routine stopped a run, and with it every
     * run it was started from, as a script waits at PlayMovie. A run under
     * way that has stopped counts, before the run it was started from ends.
     */
    bool hasWaitingRuns() const { return !_waiting.empty() || (_runDepth > 0 && _yield && *_yield); }
    /**
     * Runs \p next at once when no runs wait, or else once none do: the rest
     * of an engine step whose script stopped to wait goes on after the
     * script, as though the movie played inside the call. Steps held this way
     * go on in the order they were held; one whose runs wait again holds the
     * ones after it. A step held while a held step goes on comes before the
     * steps held earlier, as a call it makes would.
     */
    void whenNoRunsWait(std::function<void()> next);
    /**
     * The runs that wait go on, each from where it stopped: the run that
     * stopped first, and then each run it was started from, in turn. A run
     * that stops again waits once more, with the runs it was started from,
     * and the runs that stopped after them wait on. Once none wait, the held
     * steps go on. Nothing happens while a run is under way.
     */
    void resumeWaitingRuns();
    /** The runs that wait, and the steps held for them, are dropped and never go on. */
    void dropWaitingRuns();

private:
    /** Runs that stopped to wait, one started from the next, and their shared state. */
    struct WaitingRuns {
        /** The outermost run first. */
        std::vector<std::unique_ptr<script::VirtualMachine>> runs;
        std::shared_ptr<int> instructionsRun;
        std::shared_ptr<bool> yield;
    };

    script::IRoutines &_routines;
    resource::IScripts &_scripts;

    /** The instruction count of the outermost run, shared by the runs it starts. */
    std::shared_ptr<int> _instructionsRun;
    /** How many runs are under way, one inside another. */
    int _runDepth {0};
    /** Set to stop the runs under way, which all share it. */
    std::shared_ptr<bool> _yield;
    /** The runs under way that have stopped, the innermost first. */
    std::vector<std::unique_ptr<script::VirtualMachine>> _stopped;
    std::vector<WaitingRuns> _waiting;
    /** Engine steps held until no runs wait (see whenNoRunsWait). */
    std::deque<std::function<void()>> _held;
    /** Whether a held step is going on. */
    bool _inHeldStep {false};
    /** The steps held while a held step goes on, in order, to come first. */
    std::deque<std::function<void()>> _heldInStep;

    /** Runs the held steps in order until one leaves runs waiting. */
    void runHeldSteps();
    /** Drops the runs that stopped under an outermost run that has halted. */
    void dropStoppedRuns();

    /**
     * Runs a program. A run started while another is under way shares its
     * instruction count; an outermost run starts a new one.
     */
    int runProgram(std::shared_ptr<script::ScriptProgram> program, std::unique_ptr<script::ExecutionContext> ctx);
};

} // namespace game

} // namespace reone
