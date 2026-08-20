/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/script/savedsituation.h"

#include <cmath>

#include "reone/game/effect.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/script/runner.h"
#include "reone/game/location.h"
#include "reone/game/talent.h"
#include "reone/game/modulesnapshot.h"
#include "reone/resource/provider/scripts.h"
#include "reone/script/format/ncsreader.h"
#include "reone/script/program.h"
#include "reone/script/variable.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/exception/validation.h"

namespace reone {

namespace game {

namespace {

constexpr uint32_t kNcsHeaderSize = 13;

SavedScriptSituationImportResult failure(
    SavedScriptSituationImportError error,
    std::string message) {
    SavedScriptSituationImportResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

bool hasInstructionAt(const script::ScriptProgram &program, uint32_t offset) {
    for (const auto &instruction : program.instructions()) {
        if (instruction.offset == offset) {
            return true;
        }
    }
    return false;
}

ByteBuffer withNcsHeader(const ByteBuffer &code) {
    uint64_t length = static_cast<uint64_t>(code.size()) + kNcsHeaderSize;
    if (length > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument("embedded NCS is too large");
    }

    ByteBuffer result;
    result.reserve(static_cast<size_t>(length));
    const std::string signature("NCS V1.0", 8);
    result.insert(result.end(), signature.begin(), signature.end());
    result.push_back(static_cast<char>(0x42));
    uint32_t size = static_cast<uint32_t>(length);
    result.push_back(static_cast<char>((size >> 24) & 0xff));
    result.push_back(static_cast<char>((size >> 16) & 0xff));
    result.push_back(static_cast<char>((size >> 8) & 0xff));
    result.push_back(static_cast<char>(size & 0xff));
    result.insert(result.end(), code.begin(), code.end());
    return result;
}

script::Variable objectVariable(const SavedObjectReference &reference) {
    uint32_t id = reference.isInvalid() ? script::kObjectInvalid : reference.id;
    return script::Variable::ofObject(id);
}

std::shared_ptr<Event> eventValue(const SavedScriptEvent &saved) {
    std::vector<uint32_t> objects;
    objects.reserve(saved.objects.size());
    for (const auto &reference : saved.objects) {
        objects.push_back(reference.isInvalid() ? script::kObjectInvalid : reference.id);
    }
    return std::make_shared<Event>(
        saved.type,
        saved.integers,
        saved.floats,
        saved.strings,
        std::move(objects));
}

bool convertStackValue(
    const SavedVmStackValue &saved,
    script::Variable &result,
    std::vector<EffectId> &effectIds) {
    switch (static_cast<SavedVmStackType>(saved.type)) {
    case SavedVmStackType::Integer:
        if (auto value = std::get_if<int32_t>(&saved.payload)) {
            result = script::Variable::ofInt(*value);
            return true;
        }
        break;
    case SavedVmStackType::Float:
        if (auto value = std::get_if<float>(&saved.payload)) {
            result = script::Variable::ofFloat(*value);
            return true;
        }
        break;
    case SavedVmStackType::String:
        if (auto value = std::get_if<std::string>(&saved.payload)) {
            result = script::Variable::ofString(*value);
            return true;
        }
        break;
    case SavedVmStackType::Object:
        if (auto value = std::get_if<SavedObjectReference>(&saved.payload)) {
            result = objectVariable(*value);
            return true;
        }
        break;
    case SavedVmStackType::Effect:
        if (auto value = std::get_if<EffectInstance>(&saved.payload)) {
            result = script::Variable::ofEffect(std::make_shared<SavedEffectValue>(*value));
            if (value->hasStableId()) {
                effectIds.push_back(value->id);
            }
            return true;
        }
        break;
    case SavedVmStackType::Event:
        if (auto value = std::get_if<SavedScriptEvent>(&saved.payload)) {
            result = script::Variable::ofEvent(eventValue(*value));
            return true;
        }
        break;
    case SavedVmStackType::Location:
        if (auto value = std::get_if<SavedLocationValue>(&saved.payload)) {
            float facing = std::atan2(value->orientation.y, value->orientation.x);
            result = script::Variable::ofLocation(
                std::make_shared<Location>(value->position, facing));
            return true;
        }
        break;
    case SavedVmStackType::Talent:
        if (auto value = std::get_if<SavedTalentValue>(&saved.payload)) {
            if (value->type < static_cast<int32_t>(TalentType::Force) ||
                value->type > static_cast<int32_t>(TalentType::Invalid)) {
                break;
            }
            result = script::Variable::ofTalent(std::make_shared<Talent>(
                static_cast<TalentType>(value->type),
                value->id,
                value->multiClass,
                value->item.id,
                value->itemPropertyIndex,
                value->casterLevel,
                value->metaType));
            return true;
        }
        break;
    }
    return false;
}

} // namespace

bool SavedScriptContinuation::isCurrent(const Game &game) const {
    return _runtimeSession == game._runtimeSessionGeneration;
}

std::shared_ptr<SavedScriptContinuation> SavedScriptContinuation::fromRuntime(
    std::shared_ptr<script::ExecutionState> state,
    std::string scriptName,
    const Game &game) {
    return std::shared_ptr<SavedScriptContinuation>(
        new SavedScriptContinuation(
            std::move(state),
            std::move(scriptName),
            game._runtimeSessionGeneration));
}

SavedScriptSituationImportResult SavedScriptSituationImporter::import(
    const SerializedScriptSituation &situation) const {
    if (!situation.isBoundToCurrentRuntimeSession(_game)) {
        return failure(
            SavedScriptSituationImportError::UnboundRuntimeSession,
            "script situation is not bound to the current runtime session");
    }
    if (situation.crc != 0) {
        return failure(
            SavedScriptSituationImportError::UnsupportedCrc,
            "retail only executes saved script situations with CRC zero");
    }
    if (situation.secondaryPointer != 0) {
        return failure(
            SavedScriptSituationImportError::UnsupportedSecondaryPointer,
            "non-zero SecondaryPtr is not executable by the retail resume path");
    }
    if (situation.codeSize < 0 ||
        static_cast<size_t>(situation.codeSize) != situation.code.size()) {
        return failure(
            SavedScriptSituationImportError::InvalidCode,
            "CodeSize does not match the embedded code payload");
    }
    if (situation.stackSize < 0 || situation.basePointer < 0 ||
        situation.stackPointer < 0 || situation.totalSize < 0 ||
        situation.basePointer > situation.stackPointer ||
        static_cast<size_t>(situation.stackPointer) != situation.stack.size() ||
        situation.stackSize < situation.stackPointer ||
        situation.totalSize < situation.stackPointer) {
        return failure(
            SavedScriptSituationImportError::InvalidStackBounds,
            "saved BP/SP/capacity fields are inconsistent");
    }

    std::shared_ptr<script::ScriptProgram> program;
    try {
        if (!situation.code.empty()) {
            ByteBuffer ncs = withNcsHeader(situation.code);
            MemoryInputStream stream(ncs);
            script::NcsReader reader(stream, situation.scriptName);
            reader.load();
            program = reader.program();
        } else {
            if (situation.codeSize != 0 || situation.scriptName.empty()) {
                return failure(
                    SavedScriptSituationImportError::MissingCode,
                    "saved situation has neither embedded code nor a script name");
            }
            program = _scripts.get(situation.scriptName);
            if (!program) {
                return failure(
                    SavedScriptSituationImportError::MissingCode,
                    "named script resource is unavailable");
            }
        }
    } catch (const std::exception &ex) {
        return failure(
            SavedScriptSituationImportError::InvalidCode,
            std::string("invalid saved NCS: ") + ex.what());
    }

    if (situation.instructionPointer < 0 ||
        static_cast<uint64_t>(situation.instructionPointer) + kNcsHeaderSize >
            std::numeric_limits<uint32_t>::max()) {
        return failure(
            SavedScriptSituationImportError::InvalidInstructionPointer,
            "InstructionPtr is outside the supported NCS offset range");
    }
    uint32_t instructionPointer =
        static_cast<uint32_t>(situation.instructionPointer) + kNcsHeaderSize;
    if (!hasInstructionAt(*program, instructionPointer)) {
        return failure(
            SavedScriptSituationImportError::InvalidInstructionPointer,
            "InstructionPtr is not an instruction boundary");
    }

    auto state = std::make_shared<script::ExecutionState>();
    state->program = std::move(program);
    state->insOffset = instructionPointer;
    state->globals.reserve(static_cast<size_t>(situation.basePointer));
    state->locals.reserve(static_cast<size_t>(
        situation.stackPointer - situation.basePointer));

    std::vector<EffectId> effectIds;
    for (int32_t index = 0; index < situation.stackPointer; ++index) {
        script::Variable value;
        if (!convertStackValue(situation.stack[static_cast<size_t>(index)], value, effectIds)) {
            return failure(
                SavedScriptSituationImportError::InvalidStackValue,
                "saved stack contains a mismatched or unsupported VM value");
        }
        if (index < situation.basePointer) {
            state->globals.push_back(std::move(value));
        } else {
            state->locals.push_back(std::move(value));
        }
    }

    for (EffectId id : effectIds) {
        _game.importEffectId(id);
    }
    SavedScriptSituationImportResult result;
    result.continuation = std::shared_ptr<SavedScriptContinuation>(
        new SavedScriptContinuation(
            std::move(state),
            situation.scriptName,
            *situation._runtimeSession,
            std::make_shared<const SerializedScriptSituation>(situation)));
    return result;
}

void SavedDoCommandAction::execute(
    std::shared_ptr<Action>, Object &actor, float) {
    if (_continuation) {
        _game.scriptRunner().run(*_continuation, _game, actor.id());
    }
    complete();
}

std::optional<SavedActionRecord> SavedDoCommandAction::saveFacingState() const {
    if (!_continuation) {
        throw ValidationException("DoCommand action has no continuation");
    }
    std::string error;
    auto situation = exportScriptSituation(*_continuation, error);
    if (!situation) {
        const auto &state = _continuation->executionState();
        auto original = _continuation->originalSavedSituationProvenance();
        std::ostringstream message;
        message << "DoCommand continuation is not serializable: " << error
                << "; script=\"" << _continuation->scriptName() << '\"'
                << " continuationProvenance=" << (original ? "present" : "absent")
                << " continuationReusable="
                << (_continuation->originalSavedSituationReusable() ? "yes" : "no")
                << " globals=" << state.globals.size()
                << " locals=" << state.locals.size()
                << " runtimeBP=" << state.globals.size()
                << " runtimeSP=" << state.globals.size() + state.locals.size();
        if (original) {
            message << " originalBP=" << original->basePointer
                    << " originalSP=" << original->stackPointer
                    << " originalTotalSize=" << original->totalSize
                    << " originalInstructionPtr=" << original->instructionPointer;
        }
        throw ValidationException(message.str());
    }
    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 37;
    result.declaredParameterCount = 1;
    result.parameters = {SavedActionParameter {
        static_cast<uint32_t>(SavedActionParameterType::ScriptSituation),
        std::move(*situation)}};
    return result;
}

} // namespace game

} // namespace reone
