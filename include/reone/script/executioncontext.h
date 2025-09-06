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

#include "types.h"
#include "variable.h"

namespace reone {

namespace script {

struct ExecutionState;

class IRoutines;

struct ExecutionContext {
    IRoutines *routines {nullptr};
    std::shared_ptr<ExecutionState> savedState;

    std::vector<Argument> args;

    /// Find an argument variable or return nullptr.
    ///
    /// Arguments are specific to a script run. For example, LastOpenedBy
    /// argument is passed to an onOpen script (typically set on a
    /// door). Scripts can read arguments by calling corresponding routine
    /// actions. For example, scripts use GetLastOpenedBy routine to read
    /// LastOpenedBy argument.
    const Variable *findArg(ArgKind kind);
};

} // namespace script

} // namespace reone
