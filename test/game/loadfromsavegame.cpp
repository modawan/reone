/* Copyright (c) 2026 The reone project contributors */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/savegame.h"
#include "reone/game/script/routines.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

/**
 * GetLoadFromSaveGame reports whether the world being built came from a save
 * on disk. Shipped scripts use it to tell that restoration apart from ordinary
 * entry, and the two games lean opposite ways: K1 callers take the negative
 * form, guarding entry work such as spawns, cutscenes and destruction that the
 * save already holds, while K2 callers predominantly take the positive form to
 * run restore-time fixups. Either way the answer must be true only while a
 * save is being restored, and false for a new game, an ordinary transition or
 * a revisit.
 */
struct LoadFromSaveGameFixture : TestWithParam<GameID> {
    void SetUp() override {
        // testEngine() initializes itself once and is shared by the whole
        // binary: re-initializing it here would tear down mocks other suites
        // have already taught, and this default must never outlive what it
        // points at.
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sharedSceneGraph()));
        EXPECT_CALL(engine.gameModule().portraits(), portraits())
            .Times(AnyNumber())
            .WillRepeatedly(ReturnRef(portraitRows));

        game = std::make_unique<Game>(
            GetParam(), "", engine.options(), engine.services(), console);
        routines = std::make_unique<Routines>(
            GetParam(), game.get(), &engine.services());
        routines->init();

        area = game->newArea();
        player = game->newCreature();
        TestGameModule::configureModuleSnapshot(
            *game, area, player, "module_a", "module_a");

        auto &director = engine.resourceModule().director();
        EXPECT_CALL(director, committedSaveWorkingState())
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this]() { return committed; }));
        EXPECT_CALL(director, saveSlotDescriptor())
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this]() { return sourceSlot; }));
        EXPECT_CALL(director, adoptSaveWorkingState(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](auto state) { committed = std::move(state); }));
        EXPECT_CALL(director, saveNames())
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::set<std::string> {}));
    }

    /** The routine's answer, the way a compiled script asks for it. */
    bool routineSaysLoading() {
        Routine &routine = routines->get(routines->getIndexByName("GetLoadFromSaveGame"));
        ExecutionContext execution;
        execution.routines = routines.get();
        return routine.invoke({}, execution).intValue != 0;
    }

    /**
     * Snapshotting the source module happens inside the load, so it is a live
     * window onto what an authored script would see while the world is built.
     * The snapshot then reports failure, which unwinds the load without
     * needing a renderable scene; the sample has already been taken.
     */
    void sampleDuringLoad() {
        observed.reset();
        SaveOrchestrationSeams seams;
        seams.captureModule = [this](const Game &, const std::string &) {
            observed = routineSaysLoading();
            ModuleSnapshotResult result;
            result.error = ModuleSnapshotError::EncodingFailure;
            result.message = "injected snapshot failure";
            return result;
        };
        TestGameModule::configureSaveOrchestration(*game, std::move(seams));
    }

    static NiceMock<scene::MockSceneGraph> &sharedSceneGraph() {
        static NiceMock<scene::MockSceneGraph> graph;
        return graph;
    }

    TestEngine &engine {testEngine()};
    StubConsole console;
    std::vector<Portrait> portraitRows;
    std::unique_ptr<Game> game;
    std::unique_ptr<Routines> routines;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
    std::shared_ptr<const SaveWorkingState> committed {
        std::make_shared<const SaveWorkingState>()};
    SaveSlotDescriptor sourceSlot;
    std::optional<bool> observed;
};

} // namespace

TEST_P(LoadFromSaveGameFixture, an_ordinary_running_session_is_not_loading) {
    EXPECT_FALSE(routineSaysLoading());
}

// The discriminating case: while a save is being restored, an authored script
// must be told so.
TEST_P(LoadFromSaveGameFixture, restoring_a_save_reports_loading) {
    sampleDuringLoad();

    game->loadModule("module_b", "", /*initialSaveRestore=*/true);

    ASSERT_TRUE(observed) << "the load never reached the sampling point";
    EXPECT_TRUE(*observed);
}

TEST_P(LoadFromSaveGameFixture, an_ordinary_module_transition_is_not_loading) {
    sampleDuringLoad();

    game->loadModule("module_b");

    ASSERT_TRUE(observed) << "the load never reached the sampling point";
    EXPECT_FALSE(*observed)
        << "an ordinary transition must not look like a save restore";
}

// The answer belongs to the load, not to the session that outlives it, and a
// load that dies partway must not leave the session claiming otherwise.
TEST_P(LoadFromSaveGameFixture, the_window_closes_when_the_restore_ends) {
    sampleDuringLoad();

    EXPECT_FALSE(game->loadModule("module_b", "", /*initialSaveRestore=*/true));

    ASSERT_TRUE(observed);
    ASSERT_TRUE(*observed);
    EXPECT_FALSE(routineSaysLoading())
        << "the answer must fall away once the load is over";
}

TEST_P(LoadFromSaveGameFixture, a_later_restore_still_gets_its_window) {
    sampleDuringLoad();
    ASSERT_FALSE(game->loadModule("module_b", "", /*initialSaveRestore=*/true));
    ASSERT_FALSE(routineSaysLoading());

    sampleDuringLoad();
    ASSERT_FALSE(game->loadModule("module_c", "", /*initialSaveRestore=*/true));

    ASSERT_TRUE(observed);
    EXPECT_TRUE(*observed) << "an earlier failure must not consume the window";
    EXPECT_FALSE(routineSaysLoading());
}

TEST_P(LoadFromSaveGameFixture, routine_is_registered_with_the_retail_signature) {
    int index = routines->getIndexByName("GetLoadFromSaveGame");
    EXPECT_EQ(index, 251);
    Routine &routine = routines->get(index);
    EXPECT_EQ(routine.getArgumentCount(), 0);
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    LoadFromSaveGameFixture,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });
