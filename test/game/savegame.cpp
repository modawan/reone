/* Copyright (c) 2026 The reone project contributors */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/archive.h"
#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/graphics/format/tgareader.h"
#include "reone/resource/2da.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/layout.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/scene/node/camera.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

TEST(SaveScreenshot, center_crops_resamples_and_preserves_orientation_and_channels) {
    ByteBuffer rgba(4u * 2u * 4u, 0);
    auto pixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        size_t offset = static_cast<size_t>(y * 4 + x) * 4;
        rgba[offset] = static_cast<char>(r);
        rgba[offset + 1] = static_cast<char>(g);
        rgba[offset + 2] = static_cast<char>(b);
        rgba[offset + 3] = static_cast<char>(0x7f);
    };
    // The one-pixel side columns are cropped. Rows use OpenGL bottom-left order.
    pixel(1, 0, 255, 0, 0);   // bottom-left red
    pixel(2, 0, 0, 255, 0);   // bottom-right green
    pixel(1, 1, 0, 0, 255);   // top-left blue
    pixel(2, 1, 255, 255, 0); // top-right yellow

    auto encoded = encodeSaveScreenshot(4, 2, graphics::PixelFormat::RGBA8, rgba);

    ASSERT_EQ(encoded.size(), 18u + 256u * 256u * 3u);
    EXPECT_EQ(static_cast<uint8_t>(encoded[12]), 0u);
    EXPECT_EQ(static_cast<uint8_t>(encoded[13]), 1u);
    EXPECT_EQ(static_cast<uint8_t>(encoded[14]), 0u);
    EXPECT_EQ(static_cast<uint8_t>(encoded[15]), 1u);
    EXPECT_EQ(static_cast<uint8_t>(encoded[16]), 24u);
    EXPECT_EQ(static_cast<uint8_t>(encoded[17]) & 0x20u, 0u);

    MemoryInputStream input(encoded);
    graphics::TgaReader reader(input, "save_preview", graphics::TextureUsage::Default);
    reader.load();
    auto texture = reader.texture();
    ASSERT_TRUE(texture);
    ASSERT_EQ(texture->width(), 256);
    ASSERT_EQ(texture->height(), 256);
    const auto &bgr = *texture->layers().front().pixels;
    auto expectBgr = [&](int x, int y, uint8_t b, uint8_t g, uint8_t r) {
        size_t offset = static_cast<size_t>(y * 256 + x) * 3;
        EXPECT_EQ(static_cast<uint8_t>(bgr[offset]), b);
        EXPECT_EQ(static_cast<uint8_t>(bgr[offset + 1]), g);
        EXPECT_EQ(static_cast<uint8_t>(bgr[offset + 2]), r);
    };
    expectBgr(0, 0, 0, 0, 255);
    expectBgr(255, 0, 0, 255, 0);
    expectBgr(0, 255, 255, 0, 0);
    expectBgr(255, 255, 0, 255, 255);
}

TEST(SaveScreenshot, rejects_missing_malformed_and_unsupported_sources) {
    EXPECT_THROW(
        encodeSaveScreenshot(0, 2, graphics::PixelFormat::RGBA8, {}),
        ValidationException);
    EXPECT_THROW(
        encodeSaveScreenshot(2, 2, graphics::PixelFormat::RGBA8, ByteBuffer(15)),
        ValidationException);
    EXPECT_THROW(
        encodeSaveScreenshot(2, 2, graphics::PixelFormat::R8, ByteBuffer(4)),
        ValidationException);
}

void reone::game::TestGameModule::configureSaveOrchestration(
    Game &game, SaveOrchestrationSeams seams) {
    game._saveSeams = std::move(seams);
}

void reone::game::TestGameModule::processPendingSave(Game &game) {
    game.processPendingSave();
}

std::shared_ptr<const SaveWorkingState>
reone::game::TestGameModule::prepareCurrentModuleWorkingState(Game &game) {
    return game.prepareCurrentModuleWorkingState();
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

void reone::game::TestGameModule::inspectPreparedSaveLoad(
    Game &game,
    const SaveSlotDescriptor &slot,
    int &context,
    std::string &playerTag,
    bool &playerHasObjectId,
    std::string &startWaypoint,
    uint32_t &pauseDay,
    uint32_t &pauseTime) {
    auto prepared = game.prepareSaveLoad(slot);
    context = static_cast<int>(prepared.destination.context);
    const auto &players = prepared.playerInfo->getList("Mod_PlayerList");
    playerTag = players.front()->getString("Tag");
    playerHasObjectId = players.front()->has("ObjectId");
    startWaypoint = prepared.autosave->startWaypoint;
    pauseDay = prepared.autosave->pauseDay;
    pauseTime = prepared.autosave->pauseTime;
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
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sceneGraph));
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

        destinationIfo = Gff::Builder()
                             .field(Gff::Field::newResRef(
                                 "Mod_Entry_Area", "area_b"))
                             .build();
        destinationAre = Gff::Builder().build();
        destinationGit = Gff::Builder().build();
        EXPECT_CALL(director, prepareModuleLoad(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](
                                       const std::string &name,
                                       std::shared_ptr<const SaveWorkingState>) {
                return PreparedModuleLoadTestFactory::validated(
                    name, destinationIfo, destinationAre, destinationGit);
            }));

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
        seams.terminalResult = [this](const SaveRequest &request,
                                      const SaveResult &result) {
            terminalRequests.push_back(request);
            terminalResults.push_back(result);
        };
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
    NiceMock<scene::MockSceneGraph> sceneGraph;
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
    std::vector<SaveRequest> terminalRequests;
    std::vector<SaveResult> terminalResults;
    int workingAdoptions {0};
    int publishedAdoptions {0};
    int screenshotCaptures {0};
    uint8_t snapshotSequence {0};
    bool publicationFailure {false};
    bool cleanupPending {false};
    std::shared_ptr<Gff> destinationIfo;
    std::shared_ptr<Gff> destinationAre;
    std::shared_ptr<Gff> destinationGit;
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
    EXPECT_EQ(root.path / "saves" / "000042 - Game41",
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
    ASSERT_EQ(1, terminalResults.size());
    EXPECT_EQ(accepted.requestId, terminalResults[0].requestId);
    EXPECT_EQ(42u, terminalResults[0].slot);
}

TEST_F(SaveFixture, developer_request_reports_one_deferred_completion) {
    TestGameModule::initConsole(*game);

    console.execute("savegame", {"44", "Developer", "Result"});
    ASSERT_EQ(1, console.lines.size());
    EXPECT_THAT(console.lines[0], HasSubstr("accepted"));
    TestGameModule::processPendingSave(*game);

    ASSERT_EQ(2, console.lines.size());
    EXPECT_THAT(console.lines[1], HasSubstr("completed"));
    EXPECT_THAT(console.lines[1], HasSubstr("slot 44 \"Developer Result\""));
    ASSERT_EQ(1, terminalResults.size());
    EXPECT_EQ(SaveStatus::DurableSuccess, terminalResults[0].status);
}

TEST_F(SaveFixture, non_console_request_keeps_structured_result_without_console_output) {
    auto accepted = game->requestManualSave(45, "No Console");
    TestGameModule::processPendingSave(*game);

    EXPECT_EQ(SaveStatus::Accepted, accepted.status);
    EXPECT_TRUE(console.lines.empty());
    ASSERT_EQ(1, terminalResults.size());
    EXPECT_EQ(accepted.requestId, terminalResults[0].requestId);
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
    EXPECT_EQ(root.path / "saves" / "000077 - Game76",
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
    ASSERT_EQ(SaveStatus::Accepted,
              game->requestManualSave(10, "Renamed").status);
    TestGameModule::processPendingSave(*game);

    ASSERT_EQ(5, packageTargets.size());
    EXPECT_EQ("000010 - Game9", packageTargets[0].directory.filename());
    EXPECT_EQ(packageTargets[0].directory, packageTargets[1].directory);
    EXPECT_EQ("000000 - QUICKSAVE", packageTargets[2].directory.filename());
    EXPECT_EQ("000001 - AUTOSAVE", packageTargets[3].directory.filename());
    EXPECT_EQ("000010 - Game9", packageTargets[4].directory.filename());
    EXPECT_EQ(packageTargets[0].directory, packageTargets[4].directory);
    EXPECT_EQ(5, publishedAdoptions);
    EXPECT_EQ(5, terminalResults.size());
    EXPECT_LT(terminalResults[0].requestId, terminalResults[1].requestId);
    EXPECT_LT(terminalResults[1].requestId, terminalResults[2].requestId);
    EXPECT_LT(terminalResults[2].requestId, terminalResults[3].requestId);
    EXPECT_LT(terminalResults[3].requestId, terminalResults[4].requestId);
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
    ASSERT_EQ(1, terminalResults.size());
    EXPECT_EQ(SaveStatus::PublicationFailure, terminalResults[0].status);
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
    seams.terminalResult = [this](const SaveRequest &request,
                                  const SaveResult &result) {
        terminalRequests.push_back(request);
        terminalResults.push_back(result);
    };
    TestGameModule::configureSaveOrchestration(*game, std::move(seams));

    ASSERT_EQ(SaveStatus::Accepted,
              game->requestSave(
                  {SaveKind::Developer, 11, "Snapshot Failure", true}).status);
    TestGameModule::processPendingSave(*game);

    ASSERT_TRUE(game->lastSaveResult());
    EXPECT_EQ(SaveStatus::SnapshotFailure, game->lastSaveResult()->status);
    EXPECT_FALSE(TestGameModule::hasPendingSave(*game));
    EXPECT_EQ(0, publishedAdoptions);
    EXPECT_EQ(before, committed);
    EXPECT_EQ(module, game->module());
    ASSERT_EQ(1, terminalResults.size());
    EXPECT_EQ(SaveStatus::SnapshotFailure, terminalResults[0].status);
    ASSERT_EQ(1, console.lines.size());
    EXPECT_THAT(console.lines[0], HasSubstr("failed (SnapshotFailure)"));
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
    seams.terminalResult = [this](const SaveRequest &request,
                                  const SaveResult &result) {
        terminalRequests.push_back(request);
        terminalResults.push_back(result);
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
    ASSERT_EQ(1, terminalResults.size());
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
    ASSERT_EQ(1, terminalResults.size());
}

TEST_F(SaveFixture, retirement_terminalizes_pending_request_without_snapshot) {
    auto accepted = game->requestSave(
        {SaveKind::Developer, 46, "Retired", true});

    game->retireRuntimeSession();

    EXPECT_EQ(SaveStatus::Accepted, accepted.status);
    EXPECT_FALSE(TestGameModule::hasPendingSave(*game));
    ASSERT_TRUE(game->lastSaveResult());
    EXPECT_EQ(SaveStatus::Cancelled, game->lastSaveResult()->status);
    EXPECT_EQ(accepted.requestId, game->lastSaveResult()->requestId);
    EXPECT_TRUE(capturedGroups.empty());
    ASSERT_EQ(1, terminalResults.size());
    ASSERT_EQ(1, console.lines.size());
    EXPECT_THAT(console.lines[0], HasSubstr("cancelled"));
}

TEST_F(SaveFixture, execution_exception_terminalizes_and_next_request_succeeds) {
    int attempts = 0;
    SaveOrchestrationSeams seams;
    seams.captureModule = [&attempts](const Game &, const std::string &group) {
        if (++attempts == 1) {
            throw std::runtime_error("injected execution exception");
        }
        return moduleSnapshot(group);
    };
    seams.captureSaveWide = [](const Game &, SaveMetadataInput) {
        return saveWideSnapshot();
    };
    seams.publish = [](SaveSlotPackageInput input) {
        SaveSlotPublishResult result;
        result.publishedSlot = input.target;
        result.committedWorkingState =
            std::make_shared<const SaveWorkingState>();
        result.durable = true;
        result.message = "published";
        return result;
    };
    seams.terminalResult = [this](const SaveRequest &request,
                                  const SaveResult &result) {
        terminalRequests.push_back(request);
        terminalResults.push_back(result);
    };
    TestGameModule::configureSaveOrchestration(*game, std::move(seams));

    auto first = game->requestManualSave(47, "Throws");
    TestGameModule::processPendingSave(*game);
    ASSERT_EQ(1, terminalResults.size());
    EXPECT_EQ(SaveStatus::InternalExecutionFailure, terminalResults[0].status);
    EXPECT_THAT(
        terminalResults[0].message,
        HasSubstr("injected execution exception"));
    EXPECT_FALSE(TestGameModule::hasPendingSave(*game));
    EXPECT_EQ(SaveEligibilityReason::None, game->saveEligibility());

    auto second = game->requestManualSave(48, "Retry");
    EXPECT_EQ(SaveStatus::Accepted, second.status);
    EXPECT_GT(second.requestId, first.requestId);
    TestGameModule::processPendingSave(*game);
    ASSERT_EQ(2, terminalResults.size());
    EXPECT_EQ(SaveStatus::DurableSuccess, terminalResults[1].status);
}

TEST_F(SaveFixture, deferred_eligibility_failure_is_one_terminal_result) {
    auto accepted = game->requestManualSave(49, "Eligibility");
    TestGameModule::setTransitionInProgress(*game, true);

    TestGameModule::processPendingSave(*game);

    ASSERT_EQ(1, terminalResults.size());
    EXPECT_EQ(accepted.requestId, terminalResults[0].requestId);
    EXPECT_EQ(SaveStatus::NotAllowed, terminalResults[0].status);
    EXPECT_EQ(SaveEligibilityReason::TransitionInProgress,
              terminalResults[0].reason);
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
    committed = TestGameModule::prepareCurrentModuleWorkingState(*game);
    ASSERT_TRUE(committed);
    engine.resourceModule().director().adoptSaveWorkingState(committed);
    TestGameModule::setSnapshotModuleName(*game, "module_b");
    committed = TestGameModule::prepareCurrentModuleWorkingState(*game);
    ASSERT_TRUE(committed);
    engine.resourceModule().director().adoptSaveWorkingState(committed);
    TestGameModule::setSnapshotModuleName(*game, "module_a");
    committed = TestGameModule::prepareCurrentModuleWorkingState(*game);
    ASSERT_TRUE(committed);
    engine.resourceModule().director().adoptSaveWorkingState(committed);

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

TEST_F(SaveFixture, invalid_destination_is_rejected_before_source_exit_or_snapshot) {
    auto sourceModule = game->module();
    auto sourcePlayer = game->party().player();
    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareModuleLoad("module_b", _))
        .WillOnce(Throw(ResourceNotFoundException("destination missing")));
    EXPECT_CALL(director, commitModuleLoad(_)).Times(0);

    EXPECT_FALSE(game->loadModule("module_b"));

    EXPECT_TRUE(capturedGroups.empty());
    EXPECT_EQ(0, workingAdoptions);
    EXPECT_EQ(sourceModule, game->module());
    EXPECT_EQ(sourcePlayer, game->party().player());
    EXPECT_EQ(sourcePlayer, game->getObjectById(sourcePlayer->id()));
    EXPECT_TRUE(game->hasPlayableRuntimeSession());
}

TEST_F(SaveFixture, failed_destination_commit_does_not_adopt_source_working_state) {
    engine.options().graphics.sceneRender = false;
    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, commitModuleLoad(_))
        .WillOnce(Throw(ResourceNotFoundException(
            "prepared module changed before commit")));

    EXPECT_FALSE(game->loadModule("module_b"));

    EXPECT_EQ(0, workingAdoptions);
    EXPECT_EQ(Game::Screen::MainMenu, game->currentScreen());
    EXPECT_FALSE(game->module());
    EXPECT_FALSE(game->hasPlayableRuntimeSession());
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

namespace {

/// Installation holding one structurally complete save slot on disk.
// The load transaction is part of the shared Game lifecycle, so both games
// must observe the same commit boundary.
struct LoadTransactionFixture : TestWithParam<GameID> {
    test::TmpDir root {"reone_e3g_load_transaction"};
    TestEngine engine;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
    std::vector<Portrait> portraitRows;
    SaveSlotDescriptor slot;
    std::shared_ptr<Gff> destinationIfo;
    std::shared_ptr<Gff> destinationAre;
    std::shared_ptr<Gff> destinationGit;
    std::shared_ptr<Layout> emptyLayout {std::make_shared<Layout>()};
    std::vector<std::shared_ptr<scene::CameraSceneNode>> cameraNodes;
    int spawnScriptLookups {0};
    ByteBuffer activeNpcBytes;

    static std::shared_ptr<TwoDA> appearanceTable() {
        TwoDA::Builder builder;
        builder.columns({
            "modeltype", "walkdist", "rundist", "footsteptype",
            "envmap", "race", "racetex"});
        builder.row({"S", "1", "1", "-1", "", "", ""});
        return std::shared_ptr<TwoDA>(builder.build());
    }

    std::unique_ptr<PreparedModuleLoad> preparedDestination(
        const std::string &name = "module_b") {
        return PreparedModuleLoadTestFactory::validated(
            name, destinationIfo, destinationAre, destinationGit);
    }

    void writeRequiredMetadata() {
        auto nfo = Gff::Builder()
                       .field(Gff::Field::newCExoString(
                           "LASTMODULE", "module_b"))
                       .field(Gff::Field::newCExoString(
                           "AREANAME", "module_b"))
                       .build();
        auto nfoBytes = GffWriter(
                            GffFileFormat::v32("NFO "), *nfo)
                            .toBytes();
        std::ofstream nfoFile(
            slot.directory / "savenfo.res", std::ios::binary);
        nfoFile.write(
            nfoBytes.data(), static_cast<std::streamsize>(nfoBytes.size()));

        auto globals = Gff::Builder().build();
        auto globalBytes = GffWriter(
                               GffFileFormat::v32("GVT "), *globals)
                               .toBytes();
        std::ofstream globalFile(
            slot.directory / "globalvars.res", std::ios::binary);
        globalFile.write(
            globalBytes.data(),
            static_cast<std::streamsize>(globalBytes.size()));
    }

    void writeTemplateAutosaveMetadata(
        std::string playerTag = "jolee",
        std::string startWaypoint = "autosave_start",
        uint32_t pauseDay = 7,
        uint32_t pauseTime = 4321) {
        auto params = Gff::Builder()
                          .field(Gff::Field::newCExoString(
                              "STARTWAYPOINT", std::move(startWaypoint)))
                          .field(Gff::Field::newDword(
                              "TIME_PAUSEDAY", pauseDay))
                          .field(Gff::Field::newDword(
                              "TIME_PAUSETIME", pauseTime))
                          .build();
        auto nfo = Gff::Builder()
                       .field(Gff::Field::newCExoString(
                           "LASTMODULE", "module_b"))
                       .field(Gff::Field::newCExoString(
                           "AREANAME", "module_b"))
                       .field(Gff::Field::newByte("PCAUTOSAVE", 1))
                       .field(Gff::Field::newStruct(
                           "AUTOSAVEPARAMS", params))
                       .build();
        auto nfoBytes = GffWriter(
                            GffFileFormat::v32("NFO "), *nfo)
                            .toBytes();
        std::ofstream nfoFile(
            slot.directory / "savenfo.res", std::ios::binary);
        nfoFile.write(
            nfoBytes.data(), static_cast<std::streamsize>(nfoBytes.size()));

        auto globals = Gff::Builder().build();
        auto globalBytes = GffWriter(
                               GffFileFormat::v32("GVT "), *globals)
                               .toBytes();
        std::ofstream globalFile(
            slot.directory / "globalvars.res", std::ios::binary);
        globalFile.write(
            globalBytes.data(),
            static_cast<std::streamsize>(globalBytes.size()));

        auto savedPlayer = Gff::Builder()
                               .field(Gff::Field::newCExoString(
                                   "Tag", std::move(playerTag)))
                               .field(Gff::Field::newDword(
                                   "Appearance_Type", 0))
                               .field(Gff::Field::newWord(
                                   "SoundSetFile", 0xffff))
                               .field(Gff::Field::newByte(
                                   "BodyBag", 0xff))
                               .field(Gff::Field::newByte(
                                   "PerceptionRange", 0xff))
                               .build();
        auto pifo = Gff::Builder()
                        .field(Gff::Field::newList(
                            "Mod_PlayerList", {savedPlayer}))
                        .build();
        auto pifoBytes = GffWriter(
                             GffFileFormat::v32("IFO "), *pifo)
                             .toBytes();
        std::ofstream pifoFile(
            slot.directory / "pifo.ifo", std::ios::binary);
        pifoFile.write(
            pifoBytes.data(), static_cast<std::streamsize>(pifoBytes.size()));

        auto member = Gff::Builder()
                          .field(Gff::Field::newInt(
                              "PT_MEMBER_ID", kNpcPlayer))
                          .field(Gff::Field::newByte(
                              "PT_IS_LEADER", 1))
                          .build();
        auto party = Gff::Builder()
                         .field(Gff::Field::newInt(
                             "PT_CONTROLLED_NP", -1))
                         .field(Gff::Field::newByte(
                             "PT_NUM_MEMBERS", 1))
                         .field(Gff::Field::newList(
                             "PT_MEMBERS", {member}))
                         .build();
        auto partyBytes = GffWriter(
                              GffFileFormat::v32("PT  "), *party)
                              .toBytes();
        std::ofstream partyFile(
            slot.directory / "partytable.res", std::ios::binary);
        partyFile.write(
            partyBytes.data(), static_cast<std::streamsize>(partyBytes.size()));
    }

    void writeActiveNpcWithSavedAction() {
        auto playerMember = Gff::Builder()
                                .field(Gff::Field::newInt(
                                    "PT_MEMBER_ID", kNpcPlayer))
                                .field(Gff::Field::newByte(
                                    "PT_IS_LEADER", 1))
                                .build();
        auto npcMember = Gff::Builder()
                             .field(Gff::Field::newInt("PT_MEMBER_ID", 0))
                             .field(Gff::Field::newByte("PT_IS_LEADER", 0))
                             .build();
        auto available = Gff::Builder()
                             .field(Gff::Field::newByte("PT_NPC_AVAIL", 1))
                             .field(Gff::Field::newByte("PT_NPC_SELECT", 1))
                             .build();
        auto party = Gff::Builder()
                         .field(Gff::Field::newInt("PT_CONTROLLED_NP", -1))
                         .field(Gff::Field::newByte("PT_NUM_MEMBERS", 2))
                         .field(Gff::Field::newList(
                             "PT_MEMBERS", {playerMember, npcMember}))
                         .field(Gff::Field::newList(
                             "PT_AVAIL_NPCS", {available}))
                         .build();
        auto partyBytes = GffWriter(
                              GffFileFormat::v32("PT  "), *party)
                              .toBytes();
        std::ofstream partyFile(
            slot.directory / "partytable.res", std::ios::binary);
        partyFile.write(
            partyBytes.data(), static_cast<std::streamsize>(partyBytes.size()));
        partyFile.close();

        auto seconds = Gff::Builder()
                           .type(1)
                           .field(Gff::Field::newDword("Type", 2))
                           .field(Gff::Field::newFloat("Value", 5.0f))
                           .build();
        auto wait = Gff::Builder()
                        .type(0)
                        .field(Gff::Field::newDword("ActionId", 30))
                        .field(Gff::Field::newWord("GroupActionId", 1))
                        .field(Gff::Field::newWord("NumParams", 1))
                        .field(Gff::Field::newList("Paramaters", {seconds}))
                        .build();
        auto npc = Gff::Builder()
                       .field(Gff::Field::newCExoString("Tag", "disk_npc"))
                       .field(Gff::Field::newDword("Appearance_Type", 0))
                       .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                       .field(Gff::Field::newByte("BodyBag", 0xff))
                       .field(Gff::Field::newByte("PerceptionRange", 0xff))
                       .field(Gff::Field::newList("ActionList", {wait}))
                       .build();
        activeNpcBytes = GffWriter(
                             GffFileFormat::v32("UTC "), *npc)
                             .toBytes();
        test::writeErf(
            slot.archive,
            ErfWriter::FileType::ERF,
            {{"availnpc0", ResType::Utc,
              std::string(activeNpcBytes.begin(), activeNpcBytes.end())}});
    }

    void SetUp() override {
        engine.init();
        engine.options().graphics.sceneRender = false;
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sceneGraph));
        ON_CALL(sceneGraph, newCamera())
            .WillByDefault(Invoke([this]() {
                auto node = std::make_shared<scene::CameraSceneNode>(
                    sceneGraph,
                    engine.services().graphics,
                    engine.services().audio,
                    engine.services().resource);
                cameraNodes.push_back(node);
                return node;
            }));
        EXPECT_CALL(engine.resourceModule().layouts(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(emptyLayout));
        EXPECT_CALL(engine.resourceModule().strings(), getText(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::string {}));
        EXPECT_CALL(engine.resourceModule().twoDas(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(nullptr));
        EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(appearanceTable()));
        EXPECT_CALL(engine.resourceModule().models(), get(_))
            .Times(AnyNumber());
        EXPECT_CALL(engine.resourceModule().scripts(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](const std::string &resRef)
                                       -> std::shared_ptr<script::ScriptProgram> {
                if (resRef == "spawn_probe") {
                    ++spawnScriptLookups;
                }
                return nullptr;
            }));
        portraitRows.push_back({"po_live_player", 0, -1, -1, true, 0});
        EXPECT_CALL(engine.gameModule().portraits(), portraits())
            .Times(AnyNumber())
            .WillRepeatedly(ReturnRef(portraitRows));

        game = std::make_unique<Game>(
            GetParam(), root.path, engine.options(), engine.services(), console);
        TestGameModule::initSnapshotLocalServices(*game);
        area = game->newArea();
        player = game->newCreature();
        TestGameModule::configureModuleSnapshot(
            *game, area, player, "module_a", "module_a");

        // The engine has to be somewhere recognisable for "the screen did not
        // change" to mean anything.
        TestGameModule::setCurrentScreen(
            *game, static_cast<int>(Game::Screen::InGame));

        auto directory = root.path / "saves" / "000002 - Game1";
        std::filesystem::create_directories(directory);
        test::writeErf(directory / "SAVEGAME.sav", ErfWriter::FileType::ERF, {});
        slot = SaveSlotDescriptor {directory, directory / "SAVEGAME.sav"};

        auto playerRecord = Gff::Builder()
                                .field(Gff::Field::newDword("ObjectId", 100))
                                .build();
        auto areaRecord = Gff::Builder()
                              .field(Gff::Field::newResRef(
                                  "Area_Name", "module_b"))
                              .field(Gff::Field::newDword("ObjectId", 101))
                              .build();
        destinationIfo = Gff::Builder()
                             .field(Gff::Field::newResRef(
                                 "Mod_Entry_Area", "module_b"))
                             .field(Gff::Field::newByte("Mod_IsSaveGame", 1))
                             .field(Gff::Field::newDword(
                                 "Mod_NextObjId0", 2))
                             .field(Gff::Field::newDword64(
                                 "Mod_Effect_NxtId", 1))
                             .field(Gff::Field::newList(
                                 "Mod_PlayerList", {playerRecord}))
                             .field(Gff::Field::newList(
                                 "Mod_Area_list", {areaRecord}))
                             .build();
        destinationAre = Gff::Builder().build();
        destinationGit = Gff::Builder().build();

        auto &director = engine.resourceModule().director();
        EXPECT_CALL(director, prepareModuleLoad(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](
                                       const std::string &name,
                                       std::shared_ptr<const SaveWorkingState>) {
                return preparedDestination(name);
            }));
        EXPECT_CALL(director, commitModuleLoad(_))
            .Times(AnyNumber());
    }
};

} // namespace

TEST_P(LoadTransactionFixture, aPreCommitFailureLeavesTheRunningSessionAuthoritative) {
    // given a load whose candidate cannot even be opened
    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Throw(ResourceNotFoundException("Save directory not found")));

    // nothing may be committed or retired on the way out
    EXPECT_CALL(director, commitGameLoad(_)).Times(0);
    EXPECT_CALL(director, onNewGame()).Times(0);
    EXPECT_CALL(director, onModuleLoad(_)).Times(0);

    auto generation = TestGameModule::runtimeSessionGeneration(*game);
    auto *modulePtr = game->module().get();
    auto *playerPtr = game->party().player().get();
    auto screen = game->currentScreen();

    // when
    EXPECT_THROW(game->loadGame(slot), ResourceNotFoundException);

    // then the session the player was in is still the one that is running
    EXPECT_EQ(generation, TestGameModule::runtimeSessionGeneration(*game));
    EXPECT_EQ(modulePtr, game->module().get());
    EXPECT_EQ(playerPtr, game->party().player().get());
    EXPECT_EQ(screen, game->currentScreen());
    EXPECT_NE(Game::Screen::None, game->currentScreen());
}

TEST_P(LoadTransactionFixture, aMissingRequiredSaveRecordFailsBeforeTheCommitBoundary) {
    // given a slot whose archive opens but which carries no savenfo.res.
    // Validating it only after the reset is what used to strand the engine.
    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([this](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(0);
    EXPECT_CALL(director, onNewGame()).Times(0);

    auto generation = TestGameModule::runtimeSessionGeneration(*game);
    auto *modulePtr = game->module().get();

    // when
    EXPECT_THROW(game->loadGame(slot), ResourceNotFoundException);

    // then
    EXPECT_EQ(generation, TestGameModule::runtimeSessionGeneration(*game));
    EXPECT_EQ(modulePtr, game->module().get());
}

TEST_P(LoadTransactionFixture, invalidDestinationStructureLeavesTheRunningWorldUntouched) {
    writeRequiredMetadata();
    destinationGit.reset();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(0);
    EXPECT_CALL(director, onNewGame()).Times(0);

    auto generation = TestGameModule::runtimeSessionGeneration(*game);
    auto moduleBefore = game->module();
    auto playerBefore = game->party().player();
    auto screenBefore = game->currentScreen();

    EXPECT_THROW(game->loadGame(slot), ResourceNotFoundException);

    EXPECT_EQ(generation, TestGameModule::runtimeSessionGeneration(*game));
    EXPECT_EQ(moduleBefore, game->module());
    EXPECT_EQ(playerBefore, game->party().player());
    EXPECT_EQ(playerBefore, game->getObjectById(playerBefore->id()));
    EXPECT_EQ(screenBefore, game->currentScreen());
}

TEST_P(LoadTransactionFixture, duplicateSavedIdentityFailsBeforeSessionPublication) {
    writeRequiredMetadata();
    auto duplicatePlayer = Gff::Builder()
                               .field(Gff::Field::newDword("ObjectId", 101))
                               .build();
    auto duplicateArea = Gff::Builder()
                             .field(Gff::Field::newResRef(
                                 "Area_Name", "module_b"))
                             .field(Gff::Field::newDword("ObjectId", 101))
                             .build();
    destinationIfo = Gff::Builder()
                         .field(Gff::Field::newResRef(
                             "Mod_Entry_Area", "module_b"))
                         .field(Gff::Field::newByte("Mod_IsSaveGame", 1))
                         .field(Gff::Field::newList(
                             "Mod_PlayerList", {duplicatePlayer}))
                         .field(Gff::Field::newList(
                             "Mod_Area_list", {duplicateArea}))
                         .build();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(0);
    EXPECT_CALL(director, onNewGame()).Times(0);

    auto generation = TestGameModule::runtimeSessionGeneration(*game);
    auto moduleBefore = game->module();
    auto playerBefore = game->party().player();

    EXPECT_THROW(game->loadGame(slot), ValidationException);

    EXPECT_EQ(generation, TestGameModule::runtimeSessionGeneration(*game));
    EXPECT_EQ(moduleBefore, game->module());
    EXPECT_EQ(playerBefore, game->party().player());
}

TEST_P(LoadTransactionFixture, invalidPartyCandidateDoesNotMutatePublishedParty) {
    writeRequiredMetadata();
    auto partyTable = Gff::Builder()
                          .field(Gff::Field::newInt(
                              "PT_CONTROLLED_NP", 999))
                          .build();
    auto partyBytes = GffWriter(
                          GffFileFormat::v32("PT  "), *partyTable)
                          .toBytes();
    std::ofstream partyFile(
        slot.directory / "partytable.res", std::ios::binary);
    partyFile.write(
        partyBytes.data(),
        static_cast<std::streamsize>(partyBytes.size()));
    partyFile.close();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(0);
    EXPECT_CALL(director, onNewGame()).Times(0);

    auto generation = TestGameModule::runtimeSessionGeneration(*game);
    auto playerBefore = game->party().player();

    EXPECT_THROW(game->loadGame(slot), ValidationException);

    EXPECT_EQ(generation, TestGameModule::runtimeSessionGeneration(*game));
    EXPECT_EQ(playerBefore, game->party().player());
    EXPECT_EQ(playerBefore, game->getObjectById(playerBefore->id()));
}

TEST_P(LoadTransactionFixture, preparationConsumesTheDiscoveredDescriptorVerbatim) {
    // given the slot discovered by indexing. Reducing it to a name here would
    // let the loader resolve a different directory than the list offered.
    auto &director = engine.resourceModule().director();
    std::filesystem::path seen;
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([&](const SaveSlotDescriptor &descriptor) {
            seen = descriptor.directory;
            throw ResourceNotFoundException("stop after capturing identity");
            return std::unique_ptr<SaveSessionState>();
        }));
    EXPECT_CALL(director, onGameLoad(An<std::string_view>())).Times(0);

    // when
    EXPECT_THROW(game->loadGame(slot), ResourceNotFoundException);

    // then
    EXPECT_EQ(slot.directory, seen);
}

TEST_P(LoadTransactionFixture, aFailedLoadDoesNotPreventALaterOne) {
    // given one rejected candidate followed by another attempt
    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Throw(ResourceNotFoundException("Save directory not found")))
        .WillOnce(Throw(ResourceNotFoundException("Save directory not found")));

    EXPECT_THROW(game->loadGame(slot), ResourceNotFoundException);

    // when the player tries again
    // then the second attempt still reaches preparation rather than finding a
    // half-dismantled engine
    EXPECT_THROW(game->loadGame(slot), ResourceNotFoundException);
    EXPECT_EQ("module_a", game->module()->name());
}

TEST_P(LoadTransactionFixture, aPostCommitFailureLandsOnADeliberateScreen) {
    // given a candidate good enough to commit, which then fails while the
    // destination module is being mounted. The previous session is already
    // gone at that point and cannot be brought back, so the only question is
    // whether the engine ends up somewhere intentional.
    auto nfo = Gff::Builder()
                   .field(Gff::Field::newCExoString("LASTMODULE", "module_b"))
                   .field(Gff::Field::newCExoString("AREANAME", "module_b"))
                   .build();
    auto nfoBytes = GffWriter(GffFileFormat::v32("NFO "), *nfo).toBytes();
    std::ofstream nfoFile(slot.directory / "savenfo.res", std::ios::binary);
    nfoFile.write(nfoBytes.data(), static_cast<std::streamsize>(nfoBytes.size()));
    nfoFile.close();

    auto globals = Gff::Builder().build();
    auto globalBytes = GffWriter(GffFileFormat::v32("GVT "), *globals).toBytes();
    std::ofstream globalFile(slot.directory / "globalvars.res", std::ios::binary);
    globalFile.write(globalBytes.data(), static_cast<std::streamsize>(globalBytes.size()));
    globalFile.close();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(1);
    EXPECT_CALL(director, commitModuleLoad(_))
        .WillOnce(Throw(ResourceNotFoundException("module changed before commit")));

    // when the failure lands after the commit boundary
    EXPECT_FALSE(game->loadGame(slot));

    // then nothing of either session is left half-built, and the engine is not
    // sitting on the blank screen an abandoned session renders as
    EXPECT_EQ(Game::Screen::MainMenu, game->currentScreen());
    EXPECT_NE(Game::Screen::None, game->currentScreen());
    EXPECT_FALSE(game->module());
    EXPECT_FALSE(game->hasPlayableRuntimeSession());
}

TEST_P(LoadTransactionFixture, saveResourceCommitFailureAlsoLandsOnADeliberateScreen) {
    writeRequiredMetadata();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_))
        .WillOnce(Throw(ResourceNotFoundException(
            "candidate save changed before commit")));
    EXPECT_CALL(director, commitModuleLoad(_)).Times(0);

    EXPECT_FALSE(game->loadGame(slot));

    EXPECT_EQ(Game::Screen::MainMenu, game->currentScreen());
    EXPECT_FALSE(game->module());
    EXPECT_FALSE(game->hasPlayableRuntimeSession());
}

TEST_P(LoadTransactionFixture, installedModuleFallbackRemainsValidAtPreflight) {
    writeTemplateAutosaveMetadata();
    destinationIfo = Gff::Builder()
                         .field(Gff::Field::newResRef(
                             "Mod_Entry_Area", "module_b"))
                         .build();
    destinationGit = Gff::Builder()
                         .field(Gff::Field::newByte("UseTemplates", 1))
                         .build();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(1);
    EXPECT_CALL(director, commitModuleLoad(_))
        .WillOnce(Throw(ResourceNotFoundException(
            "stop after proving optional module snapshot acceptance")));

    auto generation = TestGameModule::runtimeSessionGeneration(*game);

    EXPECT_FALSE(game->loadGame(slot));

    EXPECT_NE(generation, TestGameModule::runtimeSessionGeneration(*game));
    EXPECT_EQ(Game::Screen::MainMenu, game->currentScreen());
}

TEST_P(LoadTransactionFixture, templateAutosavePreparationSeparatesSessionAndWorldIdentity) {
    writeTemplateAutosaveMetadata("jolee", "autosave_start", 7, 4321);
    destinationIfo = Gff::Builder()
                         .field(Gff::Field::newResRef(
                             "Mod_Entry_Area", "module_b"))
                         .field(Gff::Field::newByte("Mod_IsSaveGame", 1))
                         .field(Gff::Field::newDword(
                             "Mod_MinPerHour", 5))
                         .build();
    destinationGit = Gff::Builder()
                         .field(Gff::Field::newByte("UseTemplates", 1))
                         .build();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(0);
    const auto generation = TestGameModule::runtimeSessionGeneration(*game);
    const auto registrySize = TestGameModule::objectRegistrySize(*game);

    int context = -1;
    std::string playerTag;
    bool playerHasObjectId = true;
    std::string startWaypoint;
    uint32_t pauseDay = 0;
    uint32_t pauseTime = 0;
    TestGameModule::inspectPreparedSaveLoad(
        *game,
        slot,
        context,
        playerTag,
        playerHasObjectId,
        startWaypoint,
        pauseDay,
        pauseTime);

    EXPECT_EQ(static_cast<int>(ModuleLoadContext::InitialTemplateRestore),
              context);
    EXPECT_EQ("jolee", playerTag);
    EXPECT_FALSE(playerHasObjectId);
    EXPECT_EQ("autosave_start", startWaypoint);
    EXPECT_EQ(7u, pauseDay);
    EXPECT_EQ(4321u, pauseTime);
    EXPECT_EQ(generation, TestGameModule::runtimeSessionGeneration(*game));
    EXPECT_EQ(registrySize, TestGameModule::objectRegistrySize(*game));
}

TEST_P(LoadTransactionFixture, retailShapedTemplateAutosaveRestoresAPlayableFreshWorld) {
    writeTemplateAutosaveMetadata("jolee", "autosave_start", 7, 4321);
    destinationIfo = Gff::Builder()
                         .field(Gff::Field::newResRef(
                             "Mod_Entry_Area", "module_b"))
                         .field(Gff::Field::newFloat("Mod_Entry_X", 11.0f))
                         .field(Gff::Field::newFloat("Mod_Entry_Y", 22.0f))
                         .field(Gff::Field::newDword(
                             "Mod_MinPerHour", 2))
                         .build();
    auto spawned = Gff::Builder()
                       .field(Gff::Field::newCExoString("Tag", "fresh_spawn"))
                       .field(Gff::Field::newResRef(
                           "ScriptSpawn", "spawn_probe"))
                       .field(Gff::Field::newDword(
                           "Appearance_Type", 0))
                       .field(Gff::Field::newWord(
                           "SoundSetFile", 0xffff))
                       .field(Gff::Field::newByte("BodyBag", 0xff))
                       .field(Gff::Field::newByte(
                           "PerceptionRange", 0xff))
                       .build();
    auto startWaypoint = Gff::Builder()
                             .field(Gff::Field::newCExoString(
                                 "Tag", "autosave_start"))
                             .field(Gff::Field::newFloat(
                                 "XPosition", 33.0f))
                             .field(Gff::Field::newFloat(
                                 "YPosition", 44.0f))
                             .field(Gff::Field::newFloat(
                                 "XOrientation", 0.0f))
                             .field(Gff::Field::newFloat(
                                 "YOrientation", 1.0f))
                             .build();
    destinationGit = Gff::Builder()
                         .field(Gff::Field::newByte("UseTemplates", 1))
                         .field(Gff::Field::newList(
                             "Creature List", {spawned}))
                         .field(Gff::Field::newList(
                             "WaypointList", {startWaypoint}))
                         .build();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(1);

    ASSERT_TRUE(game->loadGame(slot));

    EXPECT_TRUE(game->hasPlayableRuntimeSession());
    EXPECT_EQ(Game::Screen::InGame, game->currentScreen());
    ASSERT_TRUE(game->module());
    EXPECT_EQ("module_b", game->module()->name());
    ASSERT_TRUE(game->party().player());
    EXPECT_EQ("jolee", game->party().player()->tag());
    EXPECT_FALSE(game->party().player()->serializedObjectIdentity());
    EXPECT_EQ(game->party().player(),
              game->module()->area()->getObjectByTag("jolee"));
    EXPECT_EQ(7u, game->worldTimeDay());
    EXPECT_EQ(4321u, game->worldTimeOfDay());
    EXPECT_FLOAT_EQ(33.0f, game->party().player()->position().x);
    EXPECT_FLOAT_EQ(44.0f, game->party().player()->position().y);
    EXPECT_EQ(1, spawnScriptLookups);
    auto fresh = game->module()->area()->getObjectByTag("fresh_spawn");
    ASSERT_TRUE(fresh);
    EXPECT_FALSE(fresh->serializedObjectIdentity());
}

TEST_P(LoadTransactionFixture, diskRestoreClearsSpawnedDetachedNpcActions) {
    writeTemplateAutosaveMetadata("player", "autosave_start", 7, 4321);
    writeActiveNpcWithSavedAction();
    destinationIfo = Gff::Builder()
                         .field(Gff::Field::newResRef(
                             "Mod_Entry_Area", "module_b"))
                         .build();
    destinationGit = Gff::Builder()
                         .field(Gff::Field::newByte("UseTemplates", 1))
                         .build();

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([](const SaveSlotDescriptor &descriptor) {
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, commitGameLoad(_)).Times(1);
    EXPECT_CALL(director, findSaveWorking(_))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](const ResourceId &id)
                                   -> std::optional<Resource> {
            if (id != ResourceId("availnpc0", ResType::Utc)) {
                return std::nullopt;
            }
            return Resource {activeNpcBytes};
        }));

    ASSERT_TRUE(game->loadGame(slot));

    auto npc = game->party().getMemberByNPC(0);
    ASSERT_TRUE(npc);
    EXPECT_EQ("disk_npc", npc->tag());
    EXPECT_TRUE(npc->actions().empty());
}

TEST_P(LoadTransactionFixture, aCommittedLoadReplacesTheRunningSessionInOrder) {
    // given a candidate that gets past validation
    auto nfo = Gff::Builder()
                   .field(Gff::Field::newCExoString("LASTMODULE", "module_b"))
                   .field(Gff::Field::newCExoString("AREANAME", "module_b"))
                   .build();
    auto nfoBytes = GffWriter(GffFileFormat::v32("NFO "), *nfo).toBytes();
    std::ofstream nfoFile(slot.directory / "savenfo.res", std::ios::binary);
    nfoFile.write(nfoBytes.data(), static_cast<std::streamsize>(nfoBytes.size()));
    nfoFile.close();

    auto globals = Gff::Builder().build();
    auto globalBytes = GffWriter(GffFileFormat::v32("GVT "), *globals).toBytes();
    std::ofstream globalFile(slot.directory / "globalvars.res", std::ios::binary);
    globalFile.write(globalBytes.data(), static_cast<std::streamsize>(globalBytes.size()));
    globalFile.close();

    auto *modulePtr = game->module().get();
    auto generation = TestGameModule::runtimeSessionGeneration(*game);

    // The candidate is built while the old session is still authoritative, the
    // old session is retired next, and only then is the candidate published.
    // Recorded as call order rather than a strict sequence: retiring to the
    // safe terminal state resets the game a second time, which a saturating
    // InSequence expectation would reject.
    int step = 0;
    int prepared = 0;
    int modulePrepared = 0;
    int retired = 0;
    int committed = 0;

    auto &director = engine.resourceModule().director();
    EXPECT_CALL(director, prepareGameLoad(_))
        .WillOnce(Invoke([&](const SaveSlotDescriptor &descriptor) {
            prepared = ++step;
            return std::make_unique<SaveSessionState>(descriptor);
        }));
    EXPECT_CALL(director, prepareModuleLoad("module_b", _))
        .WillOnce(Invoke([&](
                               const std::string &name,
                               std::shared_ptr<const SaveWorkingState>) {
            modulePrepared = ++step;
            return preparedDestination(name);
        }));
    EXPECT_CALL(director, onNewGame())
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([&]() {
            if (retired == 0) {
                retired = ++step;
            }
        }));
    EXPECT_CALL(director, commitGameLoad(_))
        .WillOnce(Invoke([&](std::unique_ptr<SaveSessionState>) {
            committed = ++step;
        }));
    EXPECT_CALL(director, commitModuleLoad(_))
        .WillOnce(Throw(ResourceNotFoundException(
            "stop after observing session publication")));
    // when the load reaches the commit boundary
    EXPECT_FALSE(game->loadGame(slot));

    // then the session that was running has genuinely been replaced rather
    // than merely disturbed
    EXPECT_NE(generation, TestGameModule::runtimeSessionGeneration(*game));
    EXPECT_NE(modulePtr, game->module().get());

    // validation precedes retirement, retirement precedes publication
    ASSERT_GT(prepared, 0);
    ASSERT_GT(modulePrepared, 0);
    ASSERT_GT(retired, 0);
    ASSERT_GT(committed, 0);
    EXPECT_LT(prepared, retired);
    EXPECT_LT(modulePrepared, retired);
    EXPECT_LT(retired, committed);
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    LoadTransactionFixture,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });
