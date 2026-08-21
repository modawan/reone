/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/gui/saveload.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;

void TestGameModule::setSaveLoadPendingRequest(
    SaveLoad &saveLoad, uint64_t requestId) {
    saveLoad._pendingRequestId = requestId;
}

bool TestGameModule::consumeSaveLoadResult(
    SaveLoad &saveLoad, const std::optional<SaveResult> &result) {
    return saveLoad.consumeTerminalResult(result);
}

void TestGameModule::dismissSaveLoad(SaveLoad &saveLoad) {
    saveLoad.dismissTransientState();
}

bool TestGameModule::hasSaveLoadPendingRequest(const SaveLoad &saveLoad) {
    return saveLoad._pendingRequestId.has_value();
}

bool TestGameModule::hasSaveLoadTransientState(const SaveLoad &saveLoad) {
    return saveLoad._selectedSaveSlot.has_value() || saveLoad._saveNameVisible;
}

TEST(SaveLoadFailureRecovery,
     terminal_failure_dismiss_update_reopen_does_not_reuse_stale_result) {
    auto &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    SaveLoad saveLoad(game, engine.services());

    TestGameModule::setSaveLoadPendingRequest(saveLoad, 41);
    SaveResult failure;
    failure.status = SaveStatus::SerializationFailure;
    failure.requestId = 41;
    failure.slot = 1001;
    failure.displayName = "FINAL_K2_SAVE";

    EXPECT_TRUE(TestGameModule::consumeSaveLoadResult(saveLoad, failure));
    EXPECT_FALSE(TestGameModule::hasSaveLoadPendingRequest(saveLoad));
    TestGameModule::dismissSaveLoad(saveLoad);
    EXPECT_FALSE(TestGameModule::hasSaveLoadTransientState(saveLoad));

    // A later frame and a reopened persistent controller must not consume the
    // previous terminal result as the result of the next request.
    EXPECT_FALSE(TestGameModule::consumeSaveLoadResult(saveLoad, failure));
    TestGameModule::dismissSaveLoad(saveLoad);
    TestGameModule::setSaveLoadPendingRequest(saveLoad, 42);
    EXPECT_FALSE(TestGameModule::consumeSaveLoadResult(saveLoad, failure));
    EXPECT_TRUE(TestGameModule::hasSaveLoadPendingRequest(saveLoad));

    SaveResult accepted;
    accepted.status = SaveStatus::Accepted;
    accepted.requestId = 42;
    EXPECT_FALSE(TestGameModule::consumeSaveLoadResult(saveLoad, accepted));
    EXPECT_TRUE(TestGameModule::hasSaveLoadPendingRequest(saveLoad));

    SaveResult success;
    success.status = SaveStatus::DurableSuccess;
    success.requestId = 42;
    success.durable = true;
    EXPECT_TRUE(TestGameModule::consumeSaveLoadResult(saveLoad, success));
    EXPECT_FALSE(TestGameModule::hasSaveLoadPendingRequest(saveLoad));
}
