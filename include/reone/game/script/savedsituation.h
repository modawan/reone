/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "reone/script/executionstate.h"

#include "../action.h"
#include "../savedruntime.h"

namespace reone {

namespace resource {
class IScripts;
}

namespace game {

class Game;
class ScriptRunner;

enum class SavedScriptSituationImportError {
    None,
    UnboundRuntimeSession,
    UnsupportedCrc,
    UnsupportedSecondaryPointer,
    InvalidCode,
    MissingCode,
    InvalidInstructionPointer,
    InvalidStackBounds,
    InvalidStackValue,
};

/**
 * An ordinary Reone ExecutionState reconstructed from a retail situation.
 *
 * The wrapper only owns code/state and a runtime-session guard. Queue timing
 * and publication remain responsibilities of the action/event coordinator.
 */
class SavedScriptContinuation {
public:
    const script::ExecutionState &executionState() const { return *_state; }
    const std::string &scriptName() const { return _scriptName; }
    bool isCurrent(const Game &game) const;
    const SerializedScriptSituation *originalSavedSituation() const {
        return _originalReusable ? _original.get() : nullptr;
    }
    bool requiresRuntimeExport() const { return !originalSavedSituation(); }
    void markAdvanced() const { _originalReusable = false; }

    static std::shared_ptr<SavedScriptContinuation> fromRuntime(
        std::shared_ptr<script::ExecutionState> state,
        std::string scriptName,
        const Game &game);

private:
    friend class SavedScriptSituationImporter;
    friend class ScriptRunner;

    SavedScriptContinuation(
        std::shared_ptr<script::ExecutionState> state,
        std::string scriptName,
        uint64_t runtimeSession,
        std::shared_ptr<const SerializedScriptSituation> original = nullptr) :
        _state(std::move(state)),
        _scriptName(std::move(scriptName)),
        _runtimeSession(runtimeSession),
        _original(std::move(original)) {
    }

    std::shared_ptr<script::ExecutionState> _state;
    std::string _scriptName;
    uint64_t _runtimeSession {0};
    std::shared_ptr<const SerializedScriptSituation> _original;
    mutable bool _originalReusable {true};
};

struct SavedScriptSituationImportResult {
    SavedScriptSituationImportError error {SavedScriptSituationImportError::None};
    std::string message;
    std::shared_ptr<SavedScriptContinuation> continuation;

    explicit operator bool() const { return static_cast<bool>(continuation); }
};

/** Explicit, synchronous translation from retail wire state to live VM state. */
class SavedScriptSituationImporter {
public:
    SavedScriptSituationImporter(Game &game, resource::IScripts &scripts) :
        _game(game),
        _scripts(scripts) {
    }

    SavedScriptSituationImportResult import(const SerializedScriptSituation &situation) const;

private:
    Game &_game;
    resource::IScripts &_scripts;
};
/** Runtime DoCommand action backed by a validated retail continuation. */
class SavedDoCommandAction : public Action {
public:
    SavedDoCommandAction(
        Game &game,
        ServicesView &services,
        std::shared_ptr<SavedScriptContinuation> continuation) :
        Action(game, services, ActionType::DoCommand),
        _continuation(std::move(continuation)) {
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

private:
    std::shared_ptr<SavedScriptContinuation> _continuation;
};


} // namespace game

} // namespace reone
