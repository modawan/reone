/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/gui/saveload.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;

namespace {

SavedGame browserSave(
    uint32_t slot,
    uint32_t displayNumber = 0,
    std::optional<uint64_t> timestamp = std::nullopt) {
    SavedGame result;
    result.slot = slot;
    result.displayNumber = displayNumber ? displayNumber : slot;
    result.metadata.timestamp = timestamp;
    return result;
}

std::vector<uint32_t> browserSlots(const std::vector<SavedGame> &saves) {
    std::vector<uint32_t> result;
    std::transform(saves.begin(), saves.end(), std::back_inserter(result),
                   [](const auto &save) { return save.slot; });
    return result;
}

} // namespace

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

TEST(SaveBrowserOrdering, k1_load_keeps_special_and_manual_slots_in_durable_order) {
    auto saves = detail::prepareSaveBrowserEntries(
        {browserSave(19, 0, 100), browserSave(1, 0, 500), browserSave(7, 0, 200),
         browserSave(0, 0, 400), browserSave(3, 0, 300)},
        false, SaveLoadMode::LoadFromMainMenu);

    EXPECT_EQ(browserSlots(saves), (std::vector<uint32_t> {0, 1, 3, 7, 19}));
}

TEST(SaveBrowserOrdering, k1_save_omits_reserved_slots_and_keeps_manuals_oldest_first) {
    auto saves = detail::prepareSaveBrowserEntries(
        {browserSave(19), browserSave(1), browserSave(7), browserSave(0), browserSave(3)},
        false, SaveLoadMode::Save);

    EXPECT_EQ(browserSlots(saves), (std::vector<uint32_t> {3, 7, 19}));
}

TEST(SaveBrowserOrdering, k2_load_orders_all_rows_by_persisted_timestamp_newest_first) {
    auto saves = detail::prepareSaveBrowserEntries(
        {browserSave(19, 0, 500), browserSave(1, 0, 450), browserSave(7, 0, 300),
         browserSave(0, 0, 350), browserSave(3, 0, 100)},
        true, SaveLoadMode::LoadFromInGame);

    EXPECT_EQ(browserSlots(saves), (std::vector<uint32_t> {19, 1, 0, 7, 3}));
}

TEST(SaveBrowserOrdering, k2_save_omits_reserved_slots_and_places_newest_manual_first) {
    auto saves = detail::prepareSaveBrowserEntries(
        {browserSave(19, 0, 500), browserSave(1, 0, 450), browserSave(7, 0, 300),
         browserSave(0, 0, 350), browserSave(3, 0, 100)},
        true, SaveLoadMode::Save);

    EXPECT_EQ(browserSlots(saves), (std::vector<uint32_t> {19, 7, 3}));
}

TEST(SaveBrowserOrdering, presentation_order_does_not_change_durable_or_display_identity) {
    auto older = browserSave(164, 91, 100);
    auto newer = browserSave(998, 12, 200);
    auto saves = detail::prepareSaveBrowserEntries(
        {older, newer}, true, SaveLoadMode::LoadFromMainMenu);

    ASSERT_EQ(saves.size(), 2);
    EXPECT_EQ(saves[0].slot, 998u);
    EXPECT_EQ(saves[0].displayNumber, 12u);
    EXPECT_EQ(saves[1].slot, 164u);
    EXPECT_EQ(saves[1].displayNumber, 91u);
}

TEST(SaveBrowserActivation, single_selection_has_no_controller_action_until_activated) {
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::LoadFromMainMenu, false, false, false),
              detail::SaveBrowserActivation::None);
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::LoadFromMainMenu, false, true, true),
              detail::SaveBrowserActivation::LoadExisting);
}

TEST(SaveBrowserActivation, save_activation_uses_name_and_overwrite_paths) {
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::Save, false, false, false),
              detail::SaveBrowserActivation::SaveNew);
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::Save, false, true, true),
              detail::SaveBrowserActivation::SaveExisting);
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::Save, false, true, false),
              detail::SaveBrowserActivation::None);
}

TEST(SaveBrowserActivation, pending_request_suppresses_repeated_double_click_activation) {
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::Save, true, false, false),
              detail::SaveBrowserActivation::None);
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::Save, true, true, true),
              detail::SaveBrowserActivation::None);
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::LoadFromInGame, true, true, true),
              detail::SaveBrowserActivation::None);
}

TEST(SaveBrowserActivation, load_activation_matches_in_k1_and_k2_controller_modes) {
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::LoadFromMainMenu, false, true, true),
              detail::SaveBrowserActivation::LoadExisting);
    EXPECT_EQ(detail::evaluateSaveBrowserActivation(
                  SaveLoadMode::LoadFromInGame, false, true, true),
              detail::SaveBrowserActivation::LoadExisting);
}
