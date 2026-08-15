/* Copyright (c) 2026 The reone project contributors */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/archive.h"
#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/resource/saveworkingstate.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

void reone::game::TestGameModule::configureSaveOrchestration(
    Game &game, SaveOrchestrationSeams seams) {
    game._saveSeams = std::move(seams);
}

void reone::game::TestGameModule::processPendingSave(Game &game) {
    game.processPendingSave();
}

bool reone::game::TestGameModule::storeCurrentModuleForTransition(Game &game) {
    return game.storeCurrentModuleForTransition();
}

void reone::game::TestGameModule::setSnapshotModuleName(
    Game &game, std::string name) {
    game._module->_name = std::move(name);
}

bool reone::game::TestGameModule::hasPendingSave(const Game &game) {
    return game._pendingSave.has_value();
}

void reone::game::TestGameModule::setRuntimeSessionPlayable(
    Game &game, bool playable) {
    game._runtimeSessionPlayable = playable;
}

void reone::game::TestGameModule::clearSnapshotModule(Game &game) {
    game._module.reset();
}

void reone::game::TestGameModule::clearSnapshotArea(Game &game) {
    game._module->_area.reset();
}

void reone::game::TestGameModule::clearSnapshotPlayers(Game &game) {
    game._party.setPlayer(nullptr);
    game._party.setActualPlayer(nullptr);
}

void reone::game::TestGameModule::setTransitionInProgress(
    Game &game, bool inProgress) {
    game._transitionInProgress = inProgress;
}

void reone::game::TestGameModule::setSaveInProgress(
    Game &game, bool inProgress) {
    game._saveInProgress = inProgress;
}

namespace {

ModuleSnapshotResult moduleSnapshot(
    const std::string &group, uint8_t payload = 1) {
    SavedModuleSnapshot snapshot;
    snapshot.target = {group, ResType::Sav};
    snapshot.archiveBytes = {static_cast<char>(payload)};
    ModuleSnapshotResult result;
    result.snapshot = std::move(snapshot);
    return result;
}

SaveWideSnapshotResult saveWideSnapshot(
    const std::string &module = "module_a") {
    SaveWideSnapshot snapshot;
    snapshot.moduleName = module;
    snapshot.areaName = module;
    SaveWideSnapshotResult result;
    result.snapshot = std::move(snapshot);
    return result;
}

struct SaveFixture : Test {
    void SetUp() override {
        engine.init();
        portraitRows.push_back({"po_live_player", 0, -1, -1, true, 0});
        EXPECT_CALL(engine.gameModule().portraits(), portraits())
            .Times(AnyNumber())
            .WillRepeatedly(ReturnRef(portraitRows));
        game = std::make_unique<Game>(
            GameID::KotOR, root.path, engine.options(), engine.services(), console);
        area = game->newArea();
        player = game->newCreature();
        TestGameModule::configureModuleSnapshot(
            *game, area, player, "module_a", "module_a");

        committed = std::make_shared<const SaveWorkingState>();
        auto &director = engine.resourceModule().director();
        EXPECT_CALL(director, committedSaveWorkingState())
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this]() { return committed; }));
        EXPECT_CALL(director, saveSlotDescriptor())
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this]() { return sourceSlot; }));
        EXPECT_CALL(director, adoptSaveWorkingState(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](auto state) {
                ++workingAdoptions;
                committed = std::move(state);
            }));
        EXPECT_CALL(director, adoptPublishedSave(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](auto descriptor, auto state) {
                ++publishedAdoptions;
                sourceSlot = std::move(descriptor);
                committed = std::move(state);
            }));
        EXPECT_CALL(director, saveNames())
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::set<std::string> {}));

        SaveOrchestrationSeams seams;
        seams.captureModule = [this](const Game &, const std::string &group) {
            capturedGroups.push_back(group);
            return moduleSnapshot(group, ++snapshotSequence);
        };
        seams.captureSaveWide = [this](const Game &, SaveMetadataInput metadata) {
            capturedMetadata.push_back(std::move(metadata));
            return saveWideSnapshot(game->module()->name());
        };
        seams.captureScreenshot = [this]() -> std::optional<ByteBuffer> {
            ++screenshotCaptures;
            return ByteBuffer {9, 8, 7};
        };
        seams.timestamp = []() { return 130950622286030000ULL; };
        seams.publish = [this](SaveSlotPackageInput input) {
            packageCommitted.push_back(input.committedWorkingState);
            packageTargets.push_back(input.target);
            packageScreenshots.push_back(std::move(input.screenshot));
            packageLoosePassthrough.push_back(std::move(input.loosePassthrough));
            SaveSlotPublishResult result;
            if (publicationFailure) {
                result.error = SaveSlotPublishError::TransactionFailure;
                result.message = "injected publication failure";
                return result;
            }
            result.publishedSlot = input.target;
            result.committedWorkingState =
                std::make_shared<const SaveWorkingState>();
            result.durable = true;
            result.cleanupPending = cleanupPending;
            result.error = cleanupPending ? SaveSlotPublishError::CleanupFailure
                                          : SaveSlotPublishError::None;
            result.message = cleanupPending ? "cleanup pending" : "published";
            return result;
        };
        TestGameModule::configureSaveOrchestration(*game, std::move(seams));
    }

    test::TmpDir root {"reone_e3g_save_orchestration"};
    TestEngine engine;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
    std::shared_ptr<const SaveWorkingState> committed;
    std::optional<SaveSlotDescriptor> sourceSlot;
    std::vector<std::string> capturedGroups;
    std::vector<SaveMetadataInput> capturedMetadata;
    std::vector<std::shared_ptr<const SaveWorkingState>> packageCommitted;
    std::vector<SaveSlotDescriptor> packageTargets;
    std::vector<std::optional<ByteBuffer>> packageScreenshots;
    std::vector<std::map<std::string, ByteBuffer>> packageLoosePassthrough;
    std::vector<Portrait> portraitRows;
    int workingAdoptions {0};
    int publishedAdoptions {0};
    int screenshotCaptures {0};
    uint8_t snapshotSequence {0};
    bool publicationFailure {false};
    bool cleanupPending {false};
};

TEST_F(SaveFixture, first_unsaved_manual_save_is_deferred_and_adopted_without_reload) {
    auto module = game->module();
    auto playerBefore = game->party().actualPlayer();

    auto accepted = game->requestManualSave(42, "First Save");

    EXPECT_EQ(SaveStatus::Accepted, accepted.status);
    EXPECT_TRUE(TestGameModule::hasPendingSave(*game));
    EXPECT_EQ(0, publishedAdoptions);

    TestGameModule::processPendingSave(*game);

    ASSERT_TRUE(game->lastSaveResult());
    EXPECT_EQ(SaveStatus::DurableSuccess, game->lastSaveResult()->status);
    EXPECT_EQ(1, publishedAdoptions);
    ASSERT_EQ(1, packageTargets.size());
    EXPECT_EQ(root.path / "saves" / "000042 - First Save",
              packageTargets[0].directory);
    ASSERT_EQ(1, capturedMetadata.size());
    EXPECT_EQ("First Save", capturedMetadata[0].displayName);
    EXPECT_EQ(42u, capturedMetadata[0].saveNumber);
    EXPECT_EQ(130950622286030000ULL, capturedMetadata[0].timestamp);
    EXPECT_EQ("po_live_player", capturedMetadata[0].portraits[0]);
    ASSERT_EQ(1, packageScreenshots.size());
    EXPECT_EQ((ByteBuffer {9, 8, 7}), *packageScreenshots[0]);
    EXPECT_EQ(module, game->module());
    EXPECT_EQ(playerBefore, game->party().actualPlayer());
}

TEST_F(SaveFixture, successful_cheat_command_is_reflected_in_live_metadata) {
    TestGameModule::initConsole(*game);
    console.execute("givegold", {"10"});
    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(43, "Cheat Metadata").status);

    TestGameModule::processPendingSave(*game);

    ASSERT_EQ(1, capturedMetadata.size());
    EXPECT_EQ(1, capturedMetadata[0].cheatUsed);
}

TEST_F(SaveFixture, one_pending_request_is_enforced_and_console_only_enqueues) {
    TestGameModule::initConsole(*game);
    ASSERT_TRUE(console.hasCommand("savegame"));

    console.execute("savegame", {"77", "Console", "Name"});
    auto busy = game->requestQuickSave();

    EXPECT_EQ(SaveStatus::Busy, busy.status);
    EXPECT_EQ(0, publishedAdoptions);
    TestGameModule::processPendingSave(*game);
    ASSERT_EQ(1, packageTargets.size());
    EXPECT_EQ(root.path / "saves" / "000077 - Console Name",
              packageTargets[0].directory);
}

TEST_F(SaveFixture, quick_auto_same_slot_overwrite_and_save_as_keep_source_coherent) {
    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(10, "Manual").status);
    TestGameModule::processPendingSave(*game);
    auto firstState = committed;

    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(10, "Manual").status);
    TestGameModule::processPendingSave(*game);
    EXPECT_EQ(firstState, packageCommitted[1]);

    ASSERT_EQ(SaveStatus::Accepted, game->requestQuickSave().status);
    TestGameModule::processPendingSave(*game);
    ASSERT_EQ(SaveStatus::Accepted, game->requestAutoSave().status);
    TestGameModule::processPendingSave(*game);

    ASSERT_EQ(4, packageTargets.size());
    EXPECT_EQ("000010 - Manual", packageTargets[0].directory.filename());
    EXPECT_EQ(packageTargets[0].directory, packageTargets[1].directory);
    EXPECT_EQ("000000 - QUICKSAVE", packageTargets[2].directory.filename());
    EXPECT_EQ("000001 - AUTOSAVE", packageTargets[3].directory.filename());
    EXPECT_EQ(4, publishedAdoptions);
}

TEST_F(SaveFixture, loose_passthrough_comes_only_from_the_current_source_slot) {
    auto source = root.path / "source-slot";
    std::filesystem::create_directories(source / "nested");
    test::detail::writeFile(source / "retail-extra.bin", "source-extra");
    test::detail::writeFile(source / "Screen.tga", "managed-screen");
    test::detail::writeFile(source / ".reone-staging", "transaction-junk");
    test::detail::writeFile(source / "nested" / "ignored.bin", "nested");
    sourceSlot = SaveSlotDescriptor {source, source / "SAVEGAME.sav"};

    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(13, "Passthrough").status);
    TestGameModule::processPendingSave(*game);

    ASSERT_EQ(1, packageLoosePassthrough.size());
    EXPECT_THAT(packageLoosePassthrough[0],
                UnorderedElementsAre(Pair(
                    "retail-extra.bin", test::toBytes("source-extra"))));
}

TEST_F(SaveFixture, publication_failure_does_not_adopt_or_disturb_runtime) {
    auto before = committed;
    auto module = game->module();
    publicationFailure = true;

    ASSERT_EQ(SaveStatus::Accepted, game->requestManualSave(5, "Fail").status);
    TestGameModule::processPendingSave(*game);

    ASSERT_TRUE(game->lastSaveResult());
    EXPECT_EQ(SaveStatus::PublicationFailure, game->lastSaveResult()->status);
    EXPECT_EQ(SaveSlotPublishError::TransactionFailure,
              game->lastSaveResult()->publicationError);
    EXPECT_EQ(0, publishedAdoptions);
    EXPECT_EQ(before, committed);
    EXPECT_EQ(module, game->module());
}

TEST_F(SaveFixture, snapshot_failure_clears_request_without_adoption) {
    auto before = committed;
    auto module = game->module();
    SaveOrchestrationSeams seams;
    seams.captureModule = [](const Game &, const std::string &) {
        ModuleSnapshotResult result;
        result.error = ModuleSnapshotError::EncodingFailure;
        result.message = "injected E3d failure";
        return result;
    };
    TestGameModule::configureSaveOrchestration(*game, std::move(seams));

    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(11, "Snapshot Failure").status);
    TestGameModule::processPendingSave(*game);

    ASSERT_TRUE(game->lastSaveResult());
    EXPECT_EQ(SaveStatus::SnapshotFailure, game->lastSaveResult()->status);
    EXPECT_FALSE(TestGameModule::hasPendingSave(*game));
    EXPECT_EQ(0, publishedAdoptions);
    EXPECT_EQ(before, committed);
    EXPECT_EQ(module, game->module());
}

TEST_F(SaveFixture, save_wide_failure_clears_request_without_adoption) {
    auto before = committed;
    auto module = game->module();
    SaveOrchestrationSeams seams;
    seams.captureModule = [](const Game &, const std::string &group) {
        return moduleSnapshot(group);
    };
    seams.captureSaveWide = [](const Game &, SaveMetadataInput) {
        SaveWideSnapshotResult result;
        result.error = SaveWideSnapshotError::EncodingFailure;
        result.message = "injected E3e failure";
        return result;
    };
    TestGameModule::configureSaveOrchestration(*game, std::move(seams));

    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(12, "Wide Failure").status);
    TestGameModule::processPendingSave(*game);

    ASSERT_TRUE(game->lastSaveResult());
    EXPECT_EQ(SaveStatus::SerializationFailure, game->lastSaveResult()->status);
    EXPECT_FALSE(TestGameModule::hasPendingSave(*game));
    EXPECT_EQ(0, publishedAdoptions);
    EXPECT_EQ(before, committed);
    EXPECT_EQ(module, game->module());
}

TEST_F(SaveFixture, durable_cleanup_pending_is_successfully_adopted) {
    cleanupPending = true;
    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(6, "Cleanup").status);

    TestGameModule::processPendingSave(*game);

    ASSERT_TRUE(game->lastSaveResult());
    EXPECT_EQ(SaveStatus::DurableSuccessCleanupPending,
              game->lastSaveResult()->status);
    EXPECT_TRUE(game->lastSaveResult()->durable);
    EXPECT_TRUE(game->lastSaveResult()->cleanupPending);
    EXPECT_EQ(1, publishedAdoptions);
}

TEST_F(SaveFixture, screenshot_capture_failure_is_nonfatal_and_never_reuses_stale_bytes) {
    SaveOrchestrationSeams seams;
    seams.captureModule = [](const Game &, const std::string &group) {
        return moduleSnapshot(group);
    };
    seams.captureSaveWide = [this](const Game &, SaveMetadataInput) {
        return saveWideSnapshot(game->module()->name());
    };
    seams.captureScreenshot = []() -> std::optional<ByteBuffer> {
        throw std::runtime_error("capture unavailable");
    };
    seams.publish = [this](SaveSlotPackageInput input) {
        packageScreenshots.push_back(input.screenshot);
        SaveSlotPublishResult result;
        result.publishedSlot = input.target;
        result.committedWorkingState =
            std::make_shared<const SaveWorkingState>();
        result.durable = true;
        return result;
    };
    TestGameModule::configureSaveOrchestration(*game, std::move(seams));

    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(7, "No Screen").status);
    TestGameModule::processPendingSave(*game);

    ASSERT_TRUE(game->lastSaveResult()->durable);
    ASSERT_EQ(1, packageScreenshots.size());
    EXPECT_FALSE(packageScreenshots[0]);
}

TEST_F(SaveFixture, transition_working_state_is_normalized_across_a_b_a_without_disk) {
    TestGameModule::setSnapshotModuleName(*game, "module_a");
    ASSERT_TRUE(TestGameModule::storeCurrentModuleForTransition(*game));
    TestGameModule::setSnapshotModuleName(*game, "module_b");
    ASSERT_TRUE(TestGameModule::storeCurrentModuleForTransition(*game));
    TestGameModule::setSnapshotModuleName(*game, "module_a");
    ASSERT_TRUE(TestGameModule::storeCurrentModuleForTransition(*game));

    EXPECT_EQ(3, workingAdoptions);
    EXPECT_TRUE(committed->contains({"module_a", ResType::Sav}));
    EXPECT_TRUE(committed->contains({"module_b", ResType::Sav}));
    EXPECT_FALSE(committed->contains({"module_a", ResType::Rsv}));
    EXPECT_FALSE(committed->contains({"module_b", ResType::Rsv}));
    EXPECT_EQ(2, committed->resourceIds().size());
}

TEST_F(SaveFixture, transition_snapshot_failure_aborts_before_source_teardown) {
    auto sourceModule = game->module();
    SaveOrchestrationSeams seams;
    seams.captureModule = [](const Game &, const std::string &) {
        ModuleSnapshotResult result;
        result.error = ModuleSnapshotError::EncodingFailure;
        result.message = "injected snapshot failure";
        return result;
    };
    TestGameModule::configureSaveOrchestration(*game, std::move(seams));

    EXPECT_FALSE(game->loadModule("module_b"));

    EXPECT_EQ(sourceModule, game->module());
    EXPECT_TRUE(game->hasPlayableRuntimeSession());
    EXPECT_EQ(0, workingAdoptions);
}

TEST_F(SaveFixture, reconstruction_is_distinct_from_an_absent_playable_session) {
    TestGameModule::setRuntimeSessionPlayable(*game, false);

    auto result = game->requestManualSave(8, "Reconstructing");

    EXPECT_EQ(SaveStatus::NotAllowed, result.status);
    EXPECT_EQ(SaveEligibilityReason::ReconstructionIncomplete, result.reason);
    EXPECT_FALSE(TestGameModule::hasPendingSave(*game));
}

TEST_F(SaveFixture, hard_eligibility_gates_report_structured_reasons) {
    EXPECT_EQ(SaveEligibilityReason::NotStableExecutionPoint,
              game->saveEligibility(true));

    TestGameModule::setTransitionInProgress(*game, true);
    EXPECT_EQ(SaveEligibilityReason::TransitionInProgress,
              game->saveEligibility());
    TestGameModule::setTransitionInProgress(*game, false);

    TestGameModule::setSaveInProgress(*game, true);
    EXPECT_EQ(SaveEligibilityReason::SaveInProgress,
              game->saveEligibility());
    TestGameModule::setSaveInProgress(*game, false);

    TestGameModule::clearSnapshotPlayers(*game);
    EXPECT_EQ(SaveEligibilityReason::NoPlayer, game->saveEligibility());
}

TEST_F(SaveFixture, module_and_area_are_independent_hard_eligibility_gates) {
    TestGameModule::clearSnapshotArea(*game);
    EXPECT_EQ(SaveEligibilityReason::NoArea, game->saveEligibility());

    TestGameModule::clearSnapshotModule(*game);
    EXPECT_EQ(SaveEligibilityReason::NoModule, game->saveEligibility());
}

TEST_F(SaveFixture, deferred_save_precedes_transition_snapshot_in_the_same_update) {
    std::vector<std::string> events;
    int captures = 0;
    SaveOrchestrationSeams seams;
    seams.captureScreenshot = [&events]() -> std::optional<ByteBuffer> {
        events.push_back("screenshot");
        return std::nullopt;
    };
    seams.captureModule = [&events, &captures](const Game &, const std::string &group) {
        events.push_back(++captures == 1 ? "save-snapshot" : "transition-snapshot");
        if (captures == 2) {
            ModuleSnapshotResult failed;
            failed.error = ModuleSnapshotError::EncodingFailure;
            failed.message = "stop before destination teardown";
            return failed;
        }
        return moduleSnapshot(group);
    };
    seams.captureSaveWide = [this](const Game &, SaveMetadataInput) {
        return saveWideSnapshot(game->module()->name());
    };
    seams.publish = [&events](SaveSlotPackageInput input) {
        events.push_back("publish");
        SaveSlotPublishResult result;
        result.publishedSlot = input.target;
        result.committedWorkingState =
            std::make_shared<const SaveWorkingState>();
        result.durable = true;
        return result;
    };
    TestGameModule::configureSaveOrchestration(*game, std::move(seams));
    ASSERT_EQ(SaveStatus::Accepted, game->requestAutoSave().status);
    game->scheduleModuleTransition("module_b", "");

    game->update(0.016f);

    EXPECT_THAT(events, ElementsAre(
        "screenshot", "save-snapshot", "publish", "transition-snapshot"));
    EXPECT_EQ("module_a", game->module()->name());
}

TEST(SaveEligibility, rejects_non_playable_and_invalid_semantic_slots) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto noSession = game.requestManualSave(3, "No session");
    EXPECT_EQ(SaveStatus::NotAllowed, noSession.status);
    EXPECT_EQ(SaveEligibilityReason::NoPlayableSession, noSession.reason);

    auto invalid = game.requestManualSave(1000000, "Invalid");
    EXPECT_EQ(SaveEligibilityReason::InvalidSlot, invalid.reason);
}

TEST(SaveSessionState, unsaved_session_has_real_empty_committed_state_and_no_slot) {
    SaveSessionState session;

    EXPECT_FALSE(session.slotDescriptor());
    ASSERT_TRUE(session.workingState());
    EXPECT_TRUE(session.workingState()->resourceIds().empty());
    EXPECT_FALSE(session.findMetadata({"savenfo", ResType::Res}));
}

} // namespace
