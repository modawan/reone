/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "modulesnapshot.h"
#include "savewidesnapshot.h"
#include "reone/resource/saveworkingstate.h"

namespace reone {
namespace game {

enum class SaveSlotPublishError {
    None,
    InvalidInput,
    Busy,
    CompositionFailure,
    PackagingFailure,
    CandidateWriteFailure,
    CandidateValidationFailure,
    TransactionFailure,
    PublishedValidationFailure,
    ReopenFailure,
    CleanupFailure,
    RecoveryFailure,
};

/** Stable seams used by failure-injection tests and diagnostic harnesses. */
enum class SaveSlotPublishCheckpoint {
    AfterOuterPackaging,
    DuringCandidateWrite,
    BeforeCandidateValidation,
    AfterCandidateValidation,
    AfterTargetBackup,
    AfterTargetPublish,
    CorruptPublishedTarget,
    BeforeBackupCleanup,
};

using SaveSlotFailureInjector =
    std::function<bool(SaveSlotPublishCheckpoint checkpoint)>;

struct SaveSlotManifestEntry {
    std::string filename;
    uintmax_t size {0};
    std::string digest;
};

struct SaveSlotManifest {
    std::vector<SaveSlotManifestEntry> files;
    size_t outerResourceCount {0};
    size_t nestedModuleCount {0};
    resource::ResourceId activeModule;
    resource::GameID gameId {resource::GameID::KotOR};
    bool screenshotPresent {false};
};

struct SaveSlotPackageInput {
    std::shared_ptr<const resource::SaveWorkingState> committedWorkingState;
    SavedModuleSnapshot currentModule;
    SaveWideSnapshot saveWide;
    resource::SaveSlotDescriptor target;
    std::optional<ByteBuffer> screenshot;

    /**
     * Explicit, already-discovered loose files which are safe to retain.
     * E3f never discovers or copies loose files from either source or target.
     */
    std::map<std::string, ByteBuffer> loosePassthrough;

    SaveSlotFailureInjector failureInjector;

    /** Test-only crash simulation: leave a valid marker and sibling artifacts. */
    bool leaveRecoveryStateOnInjectedFailure {false};
};

struct SaveSlotPublishResult {
    SaveSlotPublishError error {SaveSlotPublishError::None};
    std::string message;
    std::optional<resource::SaveSlotDescriptor> publishedSlot;
    std::shared_ptr<const resource::SaveWorkingState> committedWorkingState;
    SaveSlotManifest manifest;
    bool durable {false};
    bool cleanupPending {false};

    explicit operator bool() const {
        return durable && static_cast<bool>(committedWorkingState);
    }
};

/**
 * Synchronous durable save-slot publisher.
 *
 * Snapshot capture, packaging, validation, publication, and runtime adoption
 * remain separate operations. This class mutates slot files only; it never
 * mutates Game, a runtime session, or the caller's committed state.
 * File streams are flushed and closed before validation. The current portable
 * stream abstraction does not claim a physical-device cache flush.
 */
class SaveSlotPublisher {
public:
    SaveSlotPublishResult publish(SaveSlotPackageInput input) const noexcept;

    /** Recover one interrupted transaction identified by the target slot. */
    SaveSlotPublishResult recover(
        const resource::SaveSlotDescriptor &target) const noexcept;
};

} // namespace game
} // namespace reone
