/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "reone/resource/id.h"
#include "reone/resource/types.h"
#include "reone/system/types.h"

namespace reone {
namespace resource {
class Gff;
class SaveWorkingStateCandidate;
}
namespace game {

class Game;
class Creature;

struct SaveMetadataInput {
    std::string displayName;
    uint8_t cheatUsed {0};
    uint8_t gameplayHint {0};
    uint8_t storyHint {0};
    std::array<uint8_t, 10> storyHints {};
    std::array<std::string, 6> liveContentNames {};
    uint8_t liveContent {0};
    std::array<std::string, 3> portraits {};
    uint64_t timestamp {0};
    uint32_t saveNumber {0};
};

enum class SaveWideSnapshotError {
    None,
    NoPlayableModule,
    InvalidRuntimeGraph,
    UnsupportedLiveState,
    EncodingFailure,
    ValidationFailure,
};

using SaveResourcePayloads = std::map<resource::ResourceId, ByteBuffer>;
using SaveResourceGffs =
    std::map<resource::ResourceId, std::shared_ptr<const resource::Gff>>;

struct SaveWideSnapshot {
    resource::GameID gameId {resource::GameID::KotOR};
    SaveResourcePayloads outerWorkingResources;
    SaveResourcePayloads looseSlotResources;
    SaveResourceGffs semanticResources;
    std::set<resource::ResourceId> managedOuterResources;
    std::string moduleName;
    std::string areaName;

    void applyTo(resource::SaveWorkingStateCandidate &candidate) const;
};

struct SaveWideSnapshotResult {
    SaveWideSnapshotError error {SaveWideSnapshotError::None};
    std::string message;
    std::optional<SaveWideSnapshot> snapshot;

    explicit operator bool() const { return snapshot.has_value(); }
};

/** Pure, synchronous, non-publishing capture of stable save-wide state. */
class SaveWideSnapshotBuilder {
public:
    SaveWideSnapshotBuilder(const Game &game, SaveMetadataInput metadata) :
        _game(game),
        _metadata(std::move(metadata)) {
    }

    SaveWideSnapshotResult build() const noexcept;

    /**
     * The availnpc record for one roster creature, in the same shape a save
     * writes, so a companion persisted outside a save reads back identically.
     */
    static ByteBuffer availableNpcRecord(
        const Game &game, const Creature &creature);

private:
    const Game &_game;
    SaveMetadataInput _metadata;

    std::shared_ptr<resource::Gff> buildGlobals() const;
    std::shared_ptr<resource::Gff> buildPartyTable() const;
    std::shared_ptr<resource::Gff> buildInventory() const;
    std::shared_ptr<resource::Gff> buildFactions() const;
    std::shared_ptr<resource::Gff> buildNfo() const;
    void validate(const SaveWideSnapshot &snapshot) const;
};

} // namespace game
} // namespace reone
