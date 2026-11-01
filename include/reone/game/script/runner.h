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

#include "reone/script/types.h"
#include "reone/script/variable.h"

namespace reone {

namespace script {

struct ExecutionContext;

class IRoutines;
class ScriptProgram;

}

namespace resource {

class IScripts;

}

namespace game {

class Game;
class SavedScriptContinuation;

class ScriptRunner {
public:
    ScriptRunner(script::IRoutines &routines, resource::IScripts &scripts) :
        _routines(routines),
        _scripts(scripts) {
    }

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

private:
    script::IRoutines &_routines;
    resource::IScripts &_scripts;

    /** The instruction count of the outermost run, shared by the runs it starts. */
    std::shared_ptr<int> _instructionsRun;
    /** How many runs are under way, one inside another. */
    int _runDepth {0};

    /**
     * Runs a program. A run started while another is under way shares its
     * instruction count; an outermost run starts a new one.
     */
    int runProgram(std::shared_ptr<script::ScriptProgram> program, std::unique_ptr<script::ExecutionContext> ctx);
};

} // namespace game

} // namespace reone
