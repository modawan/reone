/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "modulesnapshot.h"
#include "saveslotpublisher.h"
#include "savewidesnapshot.h"

namespace reone {
namespace game {

class Game;

enum class SaveKind {
    Manual,
    Quick,
    Auto,
    Developer,
};

enum class SaveEligibilityReason {
    None,
    NoPlayableSession,
    ReconstructionIncomplete,
    NoModule,
    NoArea,
    NoPlayer,
    TransitionInProgress,
    SaveInProgress,
    NotStableExecutionPoint,
    InvalidSlot,
};

enum class SaveStatus {
    Accepted,
    Busy,
    NotAllowed,
    SnapshotFailure,
    SerializationFailure,
    PublicationFailure,
    DurableSuccess,
    DurableSuccessCleanupPending,
    Cancelled,
    InternalExecutionFailure,
};

struct SaveRequest {
    SaveKind kind {SaveKind::Manual};
    uint32_t slot {0};
    std::string displayName;
    bool captureScreenshot {true};
    uint64_t requestId {0};
};

struct SaveResult {
    SaveStatus status {SaveStatus::NotAllowed};
    SaveEligibilityReason reason {SaveEligibilityReason::None};
    SaveSlotPublishError publicationError {SaveSlotPublishError::None};
    std::string message;
    std::optional<resource::SaveSlotDescriptor> publishedSlot;
    bool durable {false};
    bool cleanupPending {false};
    uint64_t requestId {0};
    SaveKind kind {SaveKind::Manual};
    uint32_t slot {0};
    std::string displayName;
};

/** Narrow synchronous seams for deterministic orchestration tests. */
struct SaveOrchestrationSeams {
    std::function<ModuleSnapshotResult(const Game &, const std::string &)>
        captureModule;
    std::function<SaveWideSnapshotResult(const Game &, SaveMetadataInput)>
        captureSaveWide;
    std::function<SaveSlotPublishResult(SaveSlotPackageInput)> publish;
    std::function<std::optional<ByteBuffer>()> captureScreenshot;
    std::function<uint64_t()> timestamp;
    /** Test/diagnostic observer called once for each accepted request. */
    std::function<void(const SaveRequest &, const SaveResult &)> terminalResult;
};

} // namespace game
} // namespace reone
