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

#include "reone/system/exception/notimplemented.h"

#include "types.h"

namespace reone {

namespace script {

struct ExecutionContext;

class EngineType;
class ScriptObject;

struct Variable {
    VariableType type {VariableType::Void};
    std::string strValue;
    glm::vec3 vecValue {0.0f};
    std::shared_ptr<EngineType> engineType;
    std::shared_ptr<ExecutionContext> context;
    uint64_t id {0};

    union {
        int32_t intValue {0};
        uint32_t objectId;
        float floatValue;
    };

    const std::string toString() const;

    bool operator==(const Variable &other) const {
        return type == other.type &&
               strValue == other.strValue &&
               vecValue == other.vecValue &&
               engineType == other.engineType &&
               context == other.context &&
               intValue == other.intValue;
    }

    bool operator!=(const Variable &other) const {
        return !operator==(other);
    }

    Variable operator-() {
        switch (type) {
        case VariableType::Int:
            return Variable::ofInt(-intValue);
        case VariableType::Float:
            return Variable::ofFloat(-floatValue);
        default:
            throw NotImplementedException(str(boost::format("Negate operator on variable type %d not implemented") % static_cast<int>(type)));
        }
    }

    static Variable ofNull();
    static Variable ofInt(int value);
    static Variable ofFloat(float value);
    static Variable ofString(std::string value);
    static Variable ofVector(glm::vec3 value);
    static Variable ofObject(uint32_t objectId);
    static Variable ofEffect(std::shared_ptr<EngineType> engineType);
    static Variable ofEvent(std::shared_ptr<EngineType> engineType);
    static Variable ofLocation(std::shared_ptr<EngineType> engineType);
    static Variable ofTalent(std::shared_ptr<EngineType> engineType);
    static Variable ofAction(std::shared_ptr<ExecutionContext> context);
};

enum class ArgKind {
    Caller,
    ScriptVar,
    UserDefinedEventNumber,
    EnteringObject,
    ExitingObject,
    LastClosedBy,
    LastOpenedBy,
    LastPerceived,
    LastPerceptionHeard,
    LastPerceptionInaudible,
    LastPerceptionSeen,
    LastPerceptionVanished,
};

struct Argument {
    Argument() = default;
    Argument(ArgKind kind, Variable var) :
        kind(kind), var(var) {

        verify();
    }

    ArgKind kind;
    Variable var;

    std::string toString() const;

    /// Parse "kind:value" and construct an Argument
    static Argument fromString(std::string str);

private:
    void verify();
};

} // namespace script

} // namespace reone
