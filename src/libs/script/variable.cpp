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

#include "reone/script/variable.h"

namespace reone {

namespace script {

std::atomic<uint64_t> g_id {0};

Variable Variable::ofNull() {
    Variable result;
    result.type = VariableType::Void;
    return result;
}

Variable Variable::ofInt(int value) {
    Variable result;
    result.type = VariableType::Int;
    result.intValue = value;
    result.id = ++g_id;
    return result;
}

Variable Variable::ofFloat(float value) {
    Variable result;
    result.type = VariableType::Float;
    result.floatValue = value;
    result.id = ++g_id;
    return result;
}

Variable Variable::ofString(std::string value) {
    Variable result;
    result.type = VariableType::String;
    result.strValue = std::move(value);
    result.id = ++g_id;
    return result;
}

Variable Variable::ofVector(glm::vec3 value) {
    Variable result;
    result.type = VariableType::Vector;
    result.vecValue = std::move(value);
    result.id = ++g_id;
    return result;
}

Variable Variable::ofObject(uint32_t objectId) {
    Variable result;
    result.type = VariableType::Object;
    result.objectId = objectId;
    result.id = ++g_id;
    return result;
}

Variable Variable::ofEffect(std::shared_ptr<EngineType> engineType) {
    Variable result;
    result.type = VariableType::Effect;
    result.engineType = std::move(engineType);
    result.id = ++g_id;
    return result;
}

Variable Variable::ofEvent(std::shared_ptr<EngineType> engineType) {
    Variable result;
    result.type = VariableType::Event;
    result.engineType = std::move(engineType);
    result.id = ++g_id;
    return result;
}

Variable Variable::ofLocation(std::shared_ptr<EngineType> engineType) {
    Variable result;
    result.type = VariableType::Location;
    result.engineType = std::move(engineType);
    result.id = ++g_id;
    return result;
}

Variable Variable::ofTalent(std::shared_ptr<EngineType> engineType) {
    Variable result;
    result.type = VariableType::Talent;
    result.engineType = std::move(engineType);
    result.id = ++g_id;
    return result;
}

Variable Variable::ofAction(std::shared_ptr<ExecutionContext> context) {
    Variable result;
    result.type = VariableType::Action;
    result.context = std::move(context);
    result.id = ++g_id;
    return result;
}

const std::string Variable::toString() const {
    switch (type) {
    case VariableType::Void:
        return "void";
    case VariableType::Int:
        return str(boost::format("%%%u:%d") % id % intValue);
    case VariableType::Float:
        return str(boost::format("%%%u:%f") % id % floatValue);
    case VariableType::String:
        return str(boost::format("%%%u:\"%s\"") % id % strValue);
    case VariableType::Object:
        return str(boost::format("%%%u:%u") % id % objectId);
    case VariableType::Vector:
        return str(boost::format("%%%u:[%f,%f,%f]") % id % vecValue.x % vecValue.y % vecValue.z);
    case VariableType::Effect:
        return str(boost::format("%%%u:effect") % id);
    case VariableType::Event:
        return str(boost::format("%%%u:event") % id);
    case VariableType::Location:
        return str(boost::format("%%%u:location") % id);
    case VariableType::Talent:
        return str(boost::format("%%%u:talent") % id);
    case VariableType::Action:
        return str(boost::format("%%%u:action") % id);
    default:
        throw std::logic_error("Unsupported variable type: " + std::to_string(static_cast<int>(type)));
    }
}

const char *argKindToString(ArgKind kind) {
    switch (kind) {
    case ArgKind::Caller:
        return "Caller";
    case ArgKind::ScriptVar:
        return "ScriptVar";
    case ArgKind::UserDefinedEventNumber:
        return "UserDefinedEventNumber";
    case ArgKind::ClickingObject:
        return "ClickingObject";
    case ArgKind::EnteringObject:
        return "EnteringObject";
    case ArgKind::ExitingObject:
        return "ExitingObject";
    case ArgKind::LastClosedBy:
        return "LastClosedBy";
    case ArgKind::LastOpenedBy:
        return "LastOpenedBy";
    case ArgKind::LastPerceived:
        return "LastPerceived";
    case ArgKind::LastPerceptionHeard:
        return "LastPerceptionHeard";
    case ArgKind::LastPerceptionInaudible:
        return "LastPerceptionInaudible";
    case ArgKind::LastPerceptionSeen:
        return "LastPerceptionSeen";
    case ArgKind::LastPerceptionVanished:
        return "LastPerceptionVanished";
    }

    throw std::logic_error("Unsupported arg kind: " +
                           std::to_string(static_cast<int>(kind)));
}

std::string Argument::toString() const {
    return str(boost::format("%s:%s") % argKindToString(kind) % var.toString());
}

Argument Argument::fromString(std::string str) {
    size_t colon = str.find(":");
    if (colon == std::string::npos) {
        throw std::invalid_argument("expected format: kind:value");
    }

    std::string value = str.substr(colon + 1);
    std::string kind(std::move(str));
    kind.resize(colon);

    if (kind == "Caller") {
        return {ArgKind::Caller, Variable::ofObject(std::stoul(value))};
    }
    if (kind == "ScriptVar") {
        return {ArgKind::ScriptVar, Variable::ofInt(std::stoi(value))};
    }
    if (kind == "UserDefinedEventNumber") {
        return {ArgKind::UserDefinedEventNumber, Variable::ofInt(std::stoi(value))};
    }
    if (kind == "ClickingObject") {
        return {ArgKind::ClickingObject, Variable::ofObject(std::stoul(value))};
    }
    if (kind == "EnteringObject") {
        return {ArgKind::EnteringObject, Variable::ofObject(std::stoul(value))};
    }
    if (kind == "ExitingObject") {
        return {ArgKind::ExitingObject, Variable::ofObject(std::stoul(value))};
    }
    if (kind == "LastClosedBy") {
        return {ArgKind::LastClosedBy, Variable::ofObject(std::stoul(value))};
    }
    if (kind == "LastOpenedBy") {
        return {ArgKind::LastOpenedBy, Variable::ofObject(std::stoul(value))};
    }
    if (kind == "LastPerceived") {
        return {ArgKind::LastPerceived, Variable::ofObject(std::stoul(value))};
    }
    if (kind == "LastPerceptionHeard") {
        return {ArgKind::LastPerceptionHeard, Variable::ofInt(std::stoi(value))};
    }
    if (kind == "LastPerceptionInaudible") {
        return {ArgKind::LastPerceptionInaudible, Variable::ofInt(std::stoi(value))};
    }
    if (kind == "LastPerceptionSeen") {
        return {ArgKind::LastPerceptionSeen, Variable::ofInt(std::stoi(value))};
    }
    if (kind == "LastPerceptionVanished") {
        return {ArgKind::LastPerceptionVanished, Variable::ofInt(std::stoi(value))};
    }

    throw std::logic_error("Unsupported arg kind: " + kind);
}

void Argument::verify() {
    switch (kind) {
    case ArgKind::Caller:
    case ArgKind::ClickingObject:
    case ArgKind::EnteringObject:
    case ArgKind::ExitingObject:
    case ArgKind::LastClosedBy:
    case ArgKind::LastOpenedBy:
    case ArgKind::LastPerceived: {
        if (var.type != VariableType::Object || var.objectId == kObjectSelf) {
            throw std::invalid_argument(toString() + ": expected an object != self");
        }
        return;
    }
    case ArgKind::ScriptVar:
    case ArgKind::UserDefinedEventNumber:
    case ArgKind::LastPerceptionHeard:
    case ArgKind::LastPerceptionInaudible:
    case ArgKind::LastPerceptionSeen:
    case ArgKind::LastPerceptionVanished: {
        if (var.type != VariableType::Int) {
            throw std::invalid_argument(toString() + ": expected an integer");
        }
        return;
    }
    }

    throw std::logic_error("Unsupported arg kind: " +
                           std::to_string(static_cast<int>(kind)));
}

} // namespace script

} // namespace reone
