/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include <atomic>
#include <fstream>
#include <set>

#include "reone/graphics/format/tgawriter.h"
#include "reone/game/saveslotpublisher.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/gff.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

ByteBuffer encode(const std::string &signature, const Gff &root) {
    return GffWriter(GffFileFormat::v32(signature), root).toBytes();
}

std::shared_ptr<Gff> root(std::vector<Gff::Field> fields = {}) {
    return std::make_shared<Gff>(0xffffffff, std::move(fields));
}

ByteBuffer structured(
    const std::string &signature, std::vector<Gff::Field> fields = {}) {
    return encode(signature, *root(std::move(fields)));
}

std::shared_ptr<Gff> decode(const ByteBuffer &bytes) {
    ByteBuffer copy(bytes);
    MemoryInputStream stream(copy);
    GffReader reader(stream);
    reader.load();
    return reader.root();
}

ByteBuffer moduleBytes(
    const std::string &area, uint32_t areaId, uint32_t nextId,
    const std::string &tag) {
    auto areaEntry = Gff::Builder().type(6)
                         .field(Gff::Field::newResRef("Area_Name", area))
                         .field(Gff::Field::newDword("ObjectId", areaId))
                         .build();
    auto player = Gff::Builder().type(4)
                      .field(Gff::Field::newDword("ObjectId", areaId + 1))
                      .field(Gff::Field::newByte("Mod_IsPrimaryPlr", 1))
                      .build();
    auto ifo = root({
        Gff::Field::newResRef("Mod_Entry_Area", area),
        Gff::Field::newDword("Mod_Area", areaId),
        Gff::Field::newDword("Mod_NextObjId0", nextId),
        Gff::Field::newDword64("Mod_Effect_NxtId", 2),
        Gff::Field::newCExoString("TestTag", tag),
        Gff::Field::newList("Mod_Area_list", {areaEntry}),
        Gff::Field::newList("Mod_PlayerList", {player}),
        Gff::Field::newList("Creature List", {}),
    });
    auto are = root({Gff::Field::newCExoString("TestTag", tag)});
    auto git = root({
        Gff::Field::newCExoString("TestTag", tag),
        Gff::Field::newList("Creature List", {}),
        Gff::Field::newList("Door List", {
            Gff::Builder().type(8)
                .field(Gff::Field::newDword("ObjectId", areaId + 2))
                .field(Gff::Field::newByte("OpenState", 1))
                .build()}),
        Gff::Field::newList("Placeable List", {}),
        Gff::Field::newList("TriggerList", {}),
        Gff::Field::newList("Encounter List", {}),
        Gff::Field::newList("StoreList", {}),
        Gff::Field::newList("WaypointList", {}),
        Gff::Field::newList("SoundList", {}),
        Gff::Field::newList("List", {}),
    });
    ErfWriter writer;
    writer.add({"module", ResType::Ifo, encode("IFO ", *ifo)});
    writer.add({area, ResType::Are, encode("ARE ", *are)});
    writer.add({area, ResType::Git, encode("GIT ", *git)});
    return writer.toBytes(ErfWriter::FileType::MOD);
}

ByteBuffer outerBytes(const ByteBuffer &active, const std::string &tag) {
    ErfWriter writer;
    writer.add({"module_a", ResType::Sav,
                moduleBytes("area_a", 10, 20, "inactive-a")});
    writer.add({"module_b", ResType::Sav, active});
    writer.add({"module_c", ResType::Sav,
                moduleBytes("area_c", 30, 40, "inactive-c")});
    writer.add({"module_b", ResType::Rsv, {'o', 'l', 'd'}});
    writer.add({"module_c", ResType::Rsv, {'k', 'e', 'e', 'p'}});
    writer.add({"inventory", ResType::Res,
                structured("INV ", {Gff::Field::newCExoString("Tag", tag)})});
    writer.add({"repute", ResType::Fac,
                structured("FAC ", {Gff::Field::newCExoString("Tag", tag)})});
    writer.add({"pc", ResType::Utc,
                structured("UTC ", {Gff::Field::newCExoString("Tag", tag)})});
    writer.add({"extension", ResType::Txt, {'p', 'r', 'e', 's', 'e', 'r', 'v', 'e'}});
    return writer.toBytes(ErfWriter::FileType::MOD);
}

ByteBuffer tga() {
    return graphics::TgaWriter(
        1, 1, graphics::PixelFormat::RGB8,
        ByteBuffer {1, 2, 3}).toBytes();
}

void writeBytes(const std::filesystem::path &path, const ByteBuffer &bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(stream.is_open());
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(stream.good());
}

ByteBuffer readBytes(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    EXPECT_TRUE(stream.is_open());
    auto size = stream.tellg();
    ByteBuffer bytes(static_cast<size_t>(size));
    stream.seekg(0);
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

struct TempRoot {
    std::filesystem::path path;

    TempRoot() {
        static std::atomic<uint64_t> sequence {0};
        path = std::filesystem::temp_directory_path() /
               ("reone_e3f_publisher_" + std::to_string(++sequence));
        std::filesystem::remove_all(path);
        std::filesystem::create_directory(path);
    }

    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

SaveSlotDescriptor descriptor(const std::filesystem::path &directory) {
    return {directory, directory / "SAVEGAME.sav"};
}

ByteBuffer partyTable(GameID gameId) {
    auto npcCount = gameId == GameID::TSL ? 12u : 9u;
    std::vector<std::shared_ptr<Gff>> npcs;
    for (size_t index = 0; index < npcCount; ++index) {
        npcs.push_back(Gff::Builder().type(0)
                           .field(Gff::Field::newByte(
                               "PT_NPC_AVAIL",
                               gameId == GameID::TSL && index == 0))
                           .field(Gff::Field::newByte("PT_NPC_SELECT", 0))
                           .build());
    }
    std::vector<Gff::Field> fields {
        Gff::Field::newInt("PT_CONTROLLED_NP", -1),
        Gff::Field::newList("PT_AVAIL_NPCS", std::move(npcs)),
        Gff::Field::newList("PT_MEMBERS", {}),
        Gff::Field::newByte("PT_NUM_MEMBERS", 0),
    };
    if (gameId == GameID::TSL) {
        std::vector<std::shared_ptr<Gff>> puppets;
        for (size_t index = 0; index < 3; ++index) {
            puppets.push_back(Gff::Builder().type(0)
                                  .field(Gff::Field::newByte(
                                      "PT_PUP_AVAIL", index == 0))
                                  .field(Gff::Field::newByte("PT_PUP_SELECT", 0))
                                  .build());
        }
        fields.push_back(Gff::Field::newList(
            "PT_AVAIL_PUPS", std::move(puppets)));
    }
    return structured("PT  ", std::move(fields));
}

std::map<std::string, ByteBuffer> looseFiles(
    GameID gameId = GameID::KotOR,
    const std::string &lastModule = "module_b") {
    return {
        {"GLOBALVARS.res", structured("GVT ")},
        {"PARTYTABLE.res", partyTable(gameId)},
        {"savenfo.res", structured("NFO ", {
            Gff::Field::newCExoString("LASTMODULE", lastModule),
            Gff::Field::newCExoString("AREANAME", "Human-readable area")})},
    };
}

void makeSlot(
    const SaveSlotDescriptor &slot, const ByteBuffer &archive,
    bool screenshot = false, bool targetOnlyJunk = false) {
    std::filesystem::create_directory(slot.directory);
    writeBytes(slot.archive, archive);
    for (const auto &[name, bytes] : looseFiles()) {
        writeBytes(slot.directory / name, bytes);
    }
    if (screenshot) {
        writeBytes(slot.directory / "Screen.tga", tga());
    }
    if (targetOnlyJunk) {
        writeBytes(slot.directory / "target-only.bin", {'n', 'o'});
    }
}

struct PreparedSave {
    TempRoot rootDirectory;
    std::filesystem::path sourceArchive;
    std::shared_ptr<const SaveWorkingState> source;
    SavedModuleSnapshot module;
    SaveWideSnapshot wide;

    explicit PreparedSave(GameID gameId = GameID::KotOR) {
        sourceArchive = rootDirectory.path / "source.sav";
        writeBytes(
            sourceArchive,
            outerBytes(moduleBytes("area_b", 20, 30, "old-active"), "old-wide"));
        source = std::make_shared<const SaveWorkingState>(sourceArchive);

        module.target = {"module_b", ResType::Sav};
        module.archiveBytes = moduleBytes("area_b", 20, 31, "new-active");
        ErfResourceContainer archive {Storage(ByteBuffer(module.archiveBytes))};
        archive.init();
        module.ifoBytes = *archive.findResourceData({"module", ResType::Ifo});
        module.areBytes = *archive.findResourceData({"area_b", ResType::Are});
        module.gitBytes = *archive.findResourceData({"area_b", ResType::Git});
        module.ifo = decode(module.ifoBytes);
        module.are = decode(module.areBytes);
        module.git = decode(module.gitBytes);

        wide.gameId = gameId;
        wide.moduleName = "module_b";
        wide.areaName = "area_b";
        wide.managedOuterResources = {
            {"inventory", ResType::Res}, {"repute", ResType::Fac},
            {"pc", ResType::Utc}, {"availnpc0", ResType::Utc},
            {"availpup0", ResType::Utc}};
        wide.outerWorkingResources.emplace(
            ResourceId("inventory", ResType::Res),
            structured("INV ", {Gff::Field::newCExoString("Tag", "new-wide")}));
        wide.outerWorkingResources.emplace(
            ResourceId("repute", ResType::Fac),
            structured("FAC ", {Gff::Field::newCExoString("Tag", "new-wide")}));
        if (gameId == GameID::TSL) {
            wide.outerWorkingResources.emplace(
                ResourceId("pc", ResType::Utc), structured("UTC "));
            wide.outerWorkingResources.emplace(
                ResourceId("availnpc0", ResType::Utc), structured("UTC "));
            wide.outerWorkingResources.emplace(
                ResourceId("availpup0", ResType::Utc), structured("UTC "));
        }
        auto loose = looseFiles(gameId);
        wide.looseSlotResources.emplace(
            ResourceId("globalvars", ResType::Res), loose.at("GLOBALVARS.res"));
        wide.looseSlotResources.emplace(
            ResourceId("partytable", ResType::Res), loose.at("PARTYTABLE.res"));
        wide.looseSlotResources.emplace(
            ResourceId("savenfo", ResType::Res), loose.at("savenfo.res"));
        for (const auto &[id, bytes] : wide.looseSlotResources) {
            wide.semanticResources.emplace(id, decode(bytes));
        }
        for (const auto &[id, bytes] : wide.outerWorkingResources) {
            wide.semanticResources.emplace(id, decode(bytes));
        }
    }

    SaveSlotPackageInput input(
        const SaveSlotDescriptor &target,
        std::optional<ByteBuffer> screenshot = std::nullopt) const {
        SaveSlotPackageInput result;
        result.committedWorkingState = source;
        result.currentModule = module;
        result.saveWide = wide;
        result.target = target;
        result.screenshot = std::move(screenshot);
        return result;
    }
};

bool hasTransactionArtifact(const std::filesystem::path &parent) {
    for (const auto &entry : std::filesystem::directory_iterator(parent)) {
        if (entry.path().filename().string().find(".reone-") != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(SaveSlotPublisher, publishes_k1_slot_with_deterministic_outer_topology) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "slot-k1");
    auto input = prepared.input(target, tga());
    input.loosePassthrough.emplace("journal.log", ByteBuffer {'o', 'k'});

    auto result = SaveSlotPublisher().publish(std::move(input));

    ASSERT_TRUE(result) << result.message;
    EXPECT_FALSE(result.cleanupPending);
    EXPECT_EQ(result.manifest.gameId, GameID::KotOR);
    EXPECT_TRUE(result.manifest.screenshotPresent);
    EXPECT_EQ(result.manifest.nestedModuleCount, 3);
    EXPECT_TRUE(result.committedWorkingState->contains({"module_a", ResType::Sav}));
    EXPECT_TRUE(result.committedWorkingState->contains({"module_b", ResType::Sav}));
    EXPECT_TRUE(result.committedWorkingState->contains({"module_c", ResType::Sav}));
    EXPECT_FALSE(result.committedWorkingState->contains({"module_b", ResType::Rsv}));
    EXPECT_TRUE(result.committedWorkingState->contains({"module_c", ResType::Rsv}));
    EXPECT_TRUE(result.committedWorkingState->contains({"extension", ResType::Txt}));
    EXPECT_FALSE(result.committedWorkingState->contains({"pc", ResType::Utc}));
    EXPECT_EQ(readBytes(target.directory / "journal.log"), (ByteBuffer {'o', 'k'}));
    EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
}

TEST(SaveSlotPublisher, publishes_k2_party_repository_topology_without_screenshot) {
    PreparedSave prepared(GameID::TSL);
    auto target = descriptor(prepared.rootDirectory.path / "slot-k2");

    auto result = SaveSlotPublisher().publish(prepared.input(target));

    ASSERT_TRUE(result) << result.message;
    EXPECT_EQ(result.manifest.gameId, GameID::TSL);
    EXPECT_FALSE(result.manifest.screenshotPresent);
    EXPECT_FALSE(std::filesystem::exists(target.directory / "Screen.tga"));
    EXPECT_TRUE(result.committedWorkingState->contains({"pc", ResType::Utc}));
    EXPECT_TRUE(result.committedWorkingState->contains({"availnpc0", ResType::Utc}));
    EXPECT_TRUE(result.committedWorkingState->contains({"availpup0", ResType::Utc}));
}

TEST(SaveSlotPublisher, same_slot_overwrite_keeps_old_detached_state_alive) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "same-slot");
    auto oldActive = moduleBytes("area_b", 20, 30, "same-slot-old");
    makeSlot(target, outerBytes(oldActive, "old-target"), true);
    auto oldState = std::make_shared<const SaveWorkingState>(target.archive);
    auto input = prepared.input(target);
    input.committedWorkingState = oldState;

    auto result = SaveSlotPublisher().publish(std::move(input));

    ASSERT_TRUE(result) << result.message;
    ASSERT_TRUE(oldState->find({"module_b", ResType::Sav}));
    EXPECT_EQ(oldState->find({"module_b", ResType::Sav})->data, oldActive);
    EXPECT_EQ(result.committedWorkingState->find({"module_b", ResType::Sav})->data,
              prepared.module.archiveBytes);
    auto oldInventory = oldState->find({"inventory", ResType::Res})->data;
    auto newInventory = result.committedWorkingState
                            ->find({"inventory", ResType::Res})->data;
    EXPECT_NE(oldInventory, newInventory);
    EXPECT_NE(result.committedWorkingState.get(), oldState.get());
    EXPECT_FALSE(std::filesystem::exists(target.directory / "Screen.tga"));
    oldState.reset();
    EXPECT_EQ(result.committedWorkingState->find({"inventory", ResType::Res})->data,
              newInventory);
}

TEST(SaveSlotPublisher, overwrite_never_leaks_unrequested_target_files) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "unrelated-target");
    makeSlot(target, outerBytes(
        moduleBytes("area_b", 20, 30, "target"), "target"), true, true);

    auto result = SaveSlotPublisher().publish(prepared.input(target));

    ASSERT_TRUE(result) << result.message;
    EXPECT_FALSE(std::filesystem::exists(target.directory / "target-only.bin"));
    EXPECT_FALSE(std::filesystem::exists(target.directory / "Screen.tga"));
}

TEST(SaveSlotPublisher, rejects_managed_or_unsafe_passthrough_before_writing) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "bad-passthrough");
    auto input = prepared.input(target);
    input.loosePassthrough.emplace("../escape.bin", ByteBuffer {'x'});

    auto result = SaveSlotPublisher().publish(std::move(input));

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, SaveSlotPublishError::InvalidInput);
    EXPECT_FALSE(std::filesystem::exists(target.directory));
}

TEST(SaveSlotPublisher, overlapping_same_process_publication_reports_busy) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "busy-target");
    SaveSlotPublishResult nested;
    auto input = prepared.input(target);
    input.failureInjector = [&](SaveSlotPublishCheckpoint checkpoint) {
        if (checkpoint == SaveSlotPublishCheckpoint::AfterOuterPackaging) {
            nested = SaveSlotPublisher().publish(prepared.input(target));
        }
        return false;
    };

    auto outer = SaveSlotPublisher().publish(std::move(input));

    ASSERT_TRUE(outer) << outer.message;
    EXPECT_FALSE(nested);
    EXPECT_EQ(nested.error, SaveSlotPublishError::Busy);
}

TEST(SaveSlotPublisher, prepublication_failures_leave_target_unchanged) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "pre-failure");
    auto oldArchive = outerBytes(
        moduleBytes("area_b", 20, 30, "old"), "old");
    makeSlot(target, oldArchive);
    auto input = prepared.input(target);
    input.failureInjector = [](SaveSlotPublishCheckpoint checkpoint) {
        return checkpoint == SaveSlotPublishCheckpoint::DuringCandidateWrite;
    };

    auto result = SaveSlotPublisher().publish(std::move(input));

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, SaveSlotPublishError::CandidateWriteFailure);
    EXPECT_EQ(readBytes(target.archive), oldArchive);
    EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
}

TEST(SaveSlotPublisher, packaging_and_candidate_validation_failures_are_pretarget) {
    for (auto checkpoint : {
             SaveSlotPublishCheckpoint::AfterOuterPackaging,
             SaveSlotPublishCheckpoint::BeforeCandidateValidation}) {
        PreparedSave prepared;
        auto target = descriptor(
            prepared.rootDirectory.path /
            ("pretarget-" + std::to_string(static_cast<int>(checkpoint))));
        auto oldArchive = outerBytes(
            moduleBytes("area_b", 20, 30, "old"), "old");
        makeSlot(target, oldArchive);
        auto input = prepared.input(target);
        input.failureInjector = [checkpoint](SaveSlotPublishCheckpoint value) {
            return value == checkpoint;
        };

        auto result = SaveSlotPublisher().publish(std::move(input));

        EXPECT_FALSE(result);
        EXPECT_EQ(readBytes(target.archive), oldArchive);
        EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
        EXPECT_TRUE(
            result.error == SaveSlotPublishError::PackagingFailure ||
            result.error == SaveSlotPublishError::CandidateValidationFailure);
    }
}

TEST(SaveSlotPublisher, rejects_missing_required_outer_resource_and_bad_module) {
    PreparedSave prepared;
    auto missingTarget = descriptor(prepared.rootDirectory.path / "missing-required");
    auto missing = prepared.input(missingTarget);
    missing.saveWide.outerWorkingResources.erase({"inventory", ResType::Res});
    missing.saveWide.semanticResources.erase({"inventory", ResType::Res});

    auto missingResult = SaveSlotPublisher().publish(std::move(missing));

    EXPECT_FALSE(missingResult);
    EXPECT_EQ(missingResult.error, SaveSlotPublishError::InvalidInput);
    EXPECT_FALSE(std::filesystem::exists(missingTarget.directory));

    auto badTarget = descriptor(prepared.rootDirectory.path / "bad-module");
    auto bad = prepared.input(badTarget);
    bad.currentModule.archiveBytes = {'n', 'o', 't', 'm', 'o', 'd'};
    auto badResult = SaveSlotPublisher().publish(std::move(bad));
    EXPECT_FALSE(badResult);
    EXPECT_EQ(badResult.error, SaveSlotPublishError::InvalidInput);
    EXPECT_FALSE(std::filesystem::exists(badTarget.directory));
}

TEST(SaveSlotPublisher, postbackup_failure_rolls_back_old_target) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "postbackup-failure");
    auto oldArchive = outerBytes(
        moduleBytes("area_b", 20, 30, "old"), "old");
    makeSlot(target, oldArchive);
    auto input = prepared.input(target);
    input.failureInjector = [](SaveSlotPublishCheckpoint checkpoint) {
        return checkpoint == SaveSlotPublishCheckpoint::AfterTargetBackup;
    };

    auto result = SaveSlotPublisher().publish(std::move(input));

    EXPECT_FALSE(result);
    EXPECT_EQ(readBytes(target.archive), oldArchive);
    EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
}

TEST(SaveSlotPublisher, invalid_published_target_is_rejected_and_old_target_restored) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "corrupt-published");
    auto oldArchive = outerBytes(
        moduleBytes("area_b", 20, 30, "old"), "old");
    makeSlot(target, oldArchive);
    auto input = prepared.input(target);
    input.failureInjector = [](SaveSlotPublishCheckpoint checkpoint) {
        return checkpoint == SaveSlotPublishCheckpoint::CorruptPublishedTarget;
    };

    auto result = SaveSlotPublisher().publish(std::move(input));

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, SaveSlotPublishError::PublishedValidationFailure);
    EXPECT_EQ(readBytes(target.archive), oldArchive);
    EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
}

TEST(SaveSlotPublisher, recovery_handles_candidate_backup_and_published_phases) {
    for (auto checkpoint : {
             SaveSlotPublishCheckpoint::AfterCandidateValidation,
             SaveSlotPublishCheckpoint::AfterTargetBackup,
             SaveSlotPublishCheckpoint::AfterTargetPublish}) {
        PreparedSave prepared;
        auto target = descriptor(
            prepared.rootDirectory.path /
            ("recovery-" + std::to_string(static_cast<int>(checkpoint))));
        auto oldArchive = outerBytes(
            moduleBytes("area_b", 20, 30, "old"), "old");
        makeSlot(target, oldArchive);
        auto input = prepared.input(target);
        input.leaveRecoveryStateOnInjectedFailure = true;
        input.failureInjector = [checkpoint](SaveSlotPublishCheckpoint value) {
            return value == checkpoint;
        };
        auto interrupted = SaveSlotPublisher().publish(std::move(input));
        EXPECT_FALSE(interrupted);
        EXPECT_TRUE(hasTransactionArtifact(prepared.rootDirectory.path));

        auto recovered = SaveSlotPublisher().recover(target);

        ASSERT_TRUE(recovered) << recovered.message;
        EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
        auto active = recovered.committedWorkingState->find(
            {"module_b", ResType::Sav});
        ASSERT_TRUE(active);
        if (checkpoint == SaveSlotPublishCheckpoint::AfterTargetPublish) {
            EXPECT_EQ(active->data, prepared.module.archiveBytes);
        } else {
            EXPECT_EQ(readBytes(target.archive), oldArchive);
        }
    }
}

TEST(SaveSlotPublisher, cleanup_failure_is_durable_and_recoverable) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "cleanup-pending");
    makeSlot(target, outerBytes(
        moduleBytes("area_b", 20, 30, "old"), "old"));
    auto input = prepared.input(target);
    input.failureInjector = [](SaveSlotPublishCheckpoint checkpoint) {
        return checkpoint == SaveSlotPublishCheckpoint::BeforeBackupCleanup;
    };

    auto result = SaveSlotPublisher().publish(std::move(input));

    ASSERT_TRUE(result) << result.message;
    EXPECT_EQ(result.error, SaveSlotPublishError::CleanupFailure);
    EXPECT_TRUE(result.cleanupPending);
    EXPECT_EQ(result.committedWorkingState->find({"module_b", ResType::Sav})->data,
              prepared.module.archiveBytes);
    EXPECT_TRUE(hasTransactionArtifact(prepared.rootDirectory.path));

    auto recovered = SaveSlotPublisher().recover(target);
    ASSERT_TRUE(recovered) << recovered.message;
    EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
}

TEST(SaveSlotPublisher, stale_marker_with_valid_target_is_cleaned_safely) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "stale-marker");
    makeSlot(target, outerBytes(
        moduleBytes("area_b", 20, 30, "old"), "old"));
    auto input = prepared.input(target);
    input.failureInjector = [](SaveSlotPublishCheckpoint checkpoint) {
        return checkpoint == SaveSlotPublishCheckpoint::BeforeBackupCleanup;
    };
    auto published = SaveSlotPublisher().publish(std::move(input));
    ASSERT_TRUE(published);
    ASSERT_TRUE(published.cleanupPending);
    for (const auto &entry :
         std::filesystem::directory_iterator(prepared.rootDirectory.path)) {
        if (entry.path().filename().string().find(".reone-bak-") !=
            std::string::npos) {
            std::filesystem::remove_all(entry.path());
        }
    }

    auto recovered = SaveSlotPublisher().recover(target);

    ASSERT_TRUE(recovered) << recovered.message;
    EXPECT_EQ(recovered.committedWorkingState->find({"module_b", ResType::Sav})->data,
              prepared.module.archiveBytes);
    EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
}

TEST(SaveSlotPublisher, invalid_interrupted_new_slot_is_removed_without_backup) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "invalid-new-slot");
    auto input = prepared.input(target);
    input.leaveRecoveryStateOnInjectedFailure = true;
    input.failureInjector = [](SaveSlotPublishCheckpoint checkpoint) {
        return checkpoint == SaveSlotPublishCheckpoint::CorruptPublishedTarget;
    };
    auto interrupted = SaveSlotPublisher().publish(std::move(input));
    ASSERT_FALSE(interrupted);
    ASSERT_TRUE(std::filesystem::exists(target.directory));

    auto recovered = SaveSlotPublisher().recover(target);

    EXPECT_FALSE(recovered);
    EXPECT_EQ(recovered.error, SaveSlotPublishError::RecoveryFailure);
    EXPECT_FALSE(std::filesystem::exists(target.directory));
    EXPECT_FALSE(hasTransactionArtifact(prepared.rootDirectory.path));
}

TEST(SaveSlotPublisher, malformed_marker_cannot_redirect_transaction_cleanup) {
    PreparedSave prepared;
    auto target = descriptor(prepared.rootDirectory.path / "marker-safety");
    makeSlot(target, outerBytes(
        moduleBytes("area_b", 20, 30, "old"), "old"));
    auto input = prepared.input(target);
    input.leaveRecoveryStateOnInjectedFailure = true;
    input.failureInjector = [](SaveSlotPublishCheckpoint checkpoint) {
        return checkpoint == SaveSlotPublishCheckpoint::AfterCandidateValidation;
    };
    ASSERT_FALSE(SaveSlotPublisher().publish(std::move(input)));
    auto victim = prepared.rootDirectory.path / "do-not-remove";
    std::filesystem::create_directory(victim);
    writeBytes(victim / "evidence.bin", {'s', 'a', 'f', 'e'});
    std::filesystem::path marker;
    for (const auto &entry :
         std::filesystem::directory_iterator(prepared.rootDirectory.path)) {
        if (entry.path().extension() == ".marker") marker = entry.path();
    }
    ASSERT_FALSE(marker.empty());
    auto markerBytes = readBytes(marker);
    auto markerText = std::string(markerBytes.begin(), markerBytes.end());
    auto first = markerText.find('\n');
    auto second = markerText.find('\n', first + 1);
    auto third = markerText.find('\n', second + 1);
    auto fourth = markerText.find('\n', third + 1);
    ASSERT_NE(fourth, std::string::npos);
    markerText.replace(third + 1, fourth - third - 1, "do-not-remove");
    writeBytes(marker, ByteBuffer(markerText.begin(), markerText.end()));

    auto recovered = SaveSlotPublisher().recover(target);

    EXPECT_FALSE(recovered);
    EXPECT_EQ(recovered.error, SaveSlotPublishError::RecoveryFailure);
    EXPECT_TRUE(std::filesystem::exists(victim / "evidence.bin"));
    EXPECT_TRUE(std::filesystem::exists(target.archive));
}

TEST(SaveSlotPublisher, identical_prepared_inputs_package_identical_bytes) {
    PreparedSave prepared;
    auto first = descriptor(prepared.rootDirectory.path / "deterministic-a");
    auto second = descriptor(prepared.rootDirectory.path / "deterministic-b");

    auto firstResult = SaveSlotPublisher().publish(prepared.input(first, tga()));
    auto secondResult = SaveSlotPublisher().publish(prepared.input(second, tga()));

    ASSERT_TRUE(firstResult) << firstResult.message;
    ASSERT_TRUE(secondResult) << secondResult.message;
    EXPECT_EQ(readBytes(first.archive), readBytes(second.archive));
    EXPECT_EQ(readBytes(first.directory / "GLOBALVARS.res"),
              readBytes(second.directory / "GLOBALVARS.res"));
    EXPECT_EQ(firstResult.manifest.files.size(), secondResult.manifest.files.size());
    for (size_t i = 0; i < firstResult.manifest.files.size(); ++i) {
        EXPECT_EQ(firstResult.manifest.files[i].filename,
                  secondResult.manifest.files[i].filename);
        EXPECT_EQ(firstResult.manifest.files[i].digest,
                  secondResult.manifest.files[i].digest);
    }
}
