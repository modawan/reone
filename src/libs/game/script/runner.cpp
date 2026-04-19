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

#include "reone/game/game.h"
#include "reone/resource/provider/scripts.h"
#include "reone/script/executioncontext.h"
#include "reone/script/routines.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/logutil.h"


using namespace reone::script;

namespace reone {

namespace game {

static bool isTraskAutoDialogTraceScript(const std::string &resRef) {
    return resRef == "k_pend_reset";
}

static std::string describeTraceArgs(const std::vector<Argument> &args) {
    if (args.empty()) {
        return "[]";
    }
    std::string result("[");
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            result += ", ";
        }
        result += args[i].toString();
    }
    result += "]";
    return result;
}

int ScriptRunner::run(const std::string &resRef, const std::vector<Argument> &args) {
    if (isTraskAutoDialogTraceScript(resRef)) {
        info("reone trask autodialog trace: scriptRunner run begin resRef='" + resRef + "' args=" + describeTraceArgs(args));
    }

    auto program = _scripts.get(resRef);
    if (!program) {
        if (isTraskAutoDialogTraceScript(resRef)) {
            info("reone trask autodialog trace: scriptRunner run missing resRef='" + resRef + "'");
        }
        return -1;
    }

    auto ctx = std::make_unique<ExecutionContext>();
    ctx->routines = &_routines;
    ctx->args = args;

    int result = VirtualMachine(program, std::move(ctx)).run();
    if (isTraskAutoDialogTraceScript(resRef)) {
        info("reone trask autodialog trace: scriptRunner run end resRef='" + resRef + "' result=" + std::to_string(result));
    }
    return result;
}

int ScriptRunner::run(const std::string &resRef, uint32_t callerId) {
    std::vector<Argument> args;
    if (callerId) {
        args.emplace_back(script::ArgKind::Caller, Variable::ofObject(callerId));
    }
    return run(resRef, args);
}

} // namespace game

} // namespace reone
