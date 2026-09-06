/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/scene.h"

#include "reone/game/game.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/docommand.h"
#include "reone/game/action/wait.h"
#include "reone/game/object/area.h"
#include "reone/game/types.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/room.h"
#include "reone/game/script/routines.h"
#include "reone/resource/gff.h"
#include "reone/resource/layout.h"
#include "reone/scene/collision.h"
#include "reone/scene/node/camera.h"
#include "reone/script/executioncontext.h"
#include "reone/script/executionstate.h"
#include "reone/script/program.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

constexpr char kEntryArea[] = "area_x";
constexpr char kOnLoadScript[] = "hook_onload";
constexpr char kOnEnterScript[] = "hook_onenter";
constexpr int kDelayedMutationLocal = 42;

std::shared_ptr<ScriptProgram> delayedSelfMutation(
    Routines &routines, float delaySeconds = 0.0f) {
    auto program = std::make_shared<ScriptProgram>(kOnLoadScript);

    // This is the compiled shape of:
    //
    // DelayCommand(0.0, SetLocalBoolean(OBJECT_SELF, 42, TRUE));
    //
    // STORE_STATE captures the continuation beginning after the following
    // jump. The live execution skips that body and passes the captured action
    // to DelayCommand.
    program->add(Instruction::newSTORE_STATE(0, 0));
    program->add(Instruction::newJMP(31));
    program->add(Instruction::newCONSTI(1));
    program->add(Instruction::newCONSTI(kDelayedMutationLocal));
    program->add(Instruction::newCONSTO(kObjectSelf));
    program->add(Instruction::newACTION(
        routines.getIndexByName("SetLocalBoolean"), 3));
    program->add(Instruction(InstructionType::RETN));
    program->add(Instruction::newCONSTF(delaySeconds));
    program->add(Instruction::newACTION(
        routines.getIndexByName("DelayCommand"), 2));
    program->add(Instruction(InstructionType::RETN));
    return program;
}

std::shared_ptr<Gff> savedPermanentEffect() {
    return Gff::Builder()
        .type(0x1111)
        .field(Gff::Field::newDword64("Id", 10))
        .field(Gff::Field::newWord("Type", 68))
        .field(Gff::Field::newWord(
            "SubType", static_cast<uint16_t>(DurationType::Permanent)))
        .field(Gff::Field::newDword(
            "CreatorId", kSavedEffectInvalidObjectId))
        .build();
}

std::shared_ptr<Gff> savedWaitAction() {
    auto seconds = Gff::Builder()
                       .type(1)
                       .field(Gff::Field::newDword("Type", 2))
                       .field(Gff::Field::newFloat("Value", 1.0f))
                       .build();
    return Gff::Builder()
        .type(0)
        .field(Gff::Field::newDword("ActionId", 30))
        .field(Gff::Field::newWord("GroupActionId", 7))
        .field(Gff::Field::newWord("NumParams", 1))
        .field(Gff::Field::newList("Paramaters", {seconds}))
        .build();
}

/**
 * What a module's authored entry hooks observe when the world is built.
 *
 * Mod_OnModLoad and the area's OnEnter are what content uses to set a module
 * up for the party arriving. Reone used to skip both whenever the module came
 * from persisted state, so revisiting a module left that authored work undone.
 * They run on every entry, and each reports whether the world it is being
 * built into came off disk.
 */
struct EntryLifecycleFixture : TestWithParam<GameID> {
    void SetUp() override {
        // These tests are about which scripts a load dispatches, not about
        // what it draws. The engine fixture is shared, so the render setting
        // is put back the way it was found.
        sceneRenderWas = engine.options().graphics.sceneRender;
        engine.options().graphics.sceneRender = false;

        // testEngine() is shared by the whole binary: teach it defaults rather
        // than re-initializing mocks other suites already depend on.
        ON_CALL(engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(sharedSceneGraph()));
        ON_CALL(sharedSceneGraph(), testElevation(_, _))
            .WillByDefault(Invoke([this](
                                      const glm::vec3 &position,
                                      scene::Collision &collision) {
                collision.intersection = position;
                collision.user = &candidateRoom;
                return true;
            }));
        EXPECT_CALL(engine.gameModule().portraits(), portraits())
            .Times(AnyNumber())
            .WillRepeatedly(ReturnRef(portraitRows));
        EXPECT_CALL(engine.resourceModule().strings(), getText(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::string {}));

        game = std::make_unique<Game>(
            GetParam(), "", engine.options(), engine.services(), console);
        routines = std::make_unique<Routines>(
            GetParam(), game.get(), &engine.services());
        routines->init();

        // The load reaches presentation state the game owns, such as the
        // area map, so the game needs its local services standing up.
        TestGameModule::initSnapshotLocalServices(*game);

        // Start with no module loaded, which is what entering the first
        // module of a session actually is. That keeps the source-module
        // snapshot the transition path performs out of these tests, which are
        // about the destination's authored hooks.
        player = game->newCreature();
        game->party().addMember(kNpcPlayer, player);
        game->party().setPlayer(player);
        game->party().setActualPlayer(player);
        TestGameModule::setRuntimeSessionPlayable(*game, true);

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

        // The area builds its cameras while loading, and each needs a real
        // scene node behind it.
        ON_CALL(sharedSceneGraph(), newCamera())
            .WillByDefault(Invoke([this]() {
                auto node = std::make_shared<scene::CameraSceneNode>(
                    sharedSceneGraph(),
                    engine.services().graphics,
                    engine.services().audio,
                    engine.services().resource);
                cameraNodes.push_back(node);
                return node;
            }));

        // The area needs a layout to load at all. An empty one is enough:
        // these tests are about scripts, not rooms.
        EXPECT_CALL(engine.resourceModule().layouts(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](const std::string &)
                                       -> std::shared_ptr<Layout> { return emptyLayout; }));

        // Every script the load reaches is recorded with the answer routine 251
        // would have given at that moment. Returning nothing keeps the run a
        // dispatch observation: ScriptRunner treats a missing program as a
        // script that did nothing.
        EXPECT_CALL(engine.resourceModule().scripts(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](const std::string &resRef)
                                       -> std::shared_ptr<ScriptProgram> {
                dispatched.push_back({resRef, game->isLoadingFromSaveGame()});
                return nullptr;
            }));
    }

    /** Serve the destination module's IFO, ARE and GIT. */
    void serveModule(bool savedModuleSnapshot) {
        auto ifo = Gff::Builder()
                       .field(Gff::Field::newResRef("Mod_Entry_Area", kEntryArea))
                       .field(Gff::Field::newCExoString("Mod_OnModLoad", kOnLoadScript))
                       .field(Gff::Field::newByte("Mod_IsSaveGame", savedModuleSnapshot ? 1 : 0));
        if (savedModuleSnapshot) {
            auto area = Gff::Builder()
                            .field(Gff::Field::newResRef("Area_Name", kEntryArea))
                            .field(Gff::Field::newDword("ObjectId", 1000))
                            .build();
            ifo.field(Gff::Field::newList("Mod_Area_list", {area}));
        }
        moduleIfo = ifo.build();
        areaAre = Gff::Builder()
                      .field(Gff::Field::newCExoString("OnEnter", kOnEnterScript))
                      .build();
        areaGit = Gff::Builder().build();

        auto &director = engine.resourceModule().director();
        EXPECT_CALL(director, prepareModuleLoad("module_b", _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](
                                       const std::string &name,
                                       std::shared_ptr<const SaveWorkingState>) {
                return PreparedModuleLoadTestFactory::validated(
                    name, moduleIfo, areaAre, areaGit);
            }));
        EXPECT_CALL(director, commitModuleLoad(_))
            .Times(AnyNumber());

        auto &gffs = engine.resourceModule().gffs();
        EXPECT_CALL(gffs, get(_, _))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([this](const std::string &key, ResType type)
                                       -> std::shared_ptr<Gff> {
                if (type == ResType::Ifo) {
                    return moduleIfo;
                }
                if (type == ResType::Are) {
                    return areaAre;
                }
                if (type == ResType::Git) {
                    return areaGit;
                }
                return nullptr;
            }));
    }

    /** How many times the load asked for a given authored hook. */
    int dispatchCount(const std::string &resRef) const {
        return static_cast<int>(std::count_if(
            dispatched.begin(), dispatched.end(),
            [&resRef](const auto &entry) { return entry.first == resRef; }));
    }

    /** What routine 251 would have answered inside that hook. */
    std::optional<bool> observedDuring(const std::string &resRef) const {
        for (const auto &entry : dispatched) {
            if (entry.first == resRef) {
                return entry.second;
            }
        }
        return std::nullopt;
    }

    static NiceMock<scene::MockSceneGraph> &sharedSceneGraph() {
        static NiceMock<scene::MockSceneGraph> graph;
        return graph;
    }

    void TearDown() override {
        engine.options().graphics.sceneRender = sceneRenderWas;
    }

    TestEngine &engine {testEngine()};
    bool sceneRenderWas {true};
    StubConsole console;
    std::vector<Portrait> portraitRows;
    std::unique_ptr<Game> game;
    std::unique_ptr<Routines> routines;
    std::shared_ptr<Creature> player;
    Room candidateRoom {"candidate", glm::vec3(0.0f), nullptr, nullptr, nullptr};
    std::vector<std::shared_ptr<scene::CameraSceneNode>> cameraNodes;
    std::shared_ptr<Layout> emptyLayout {std::make_shared<Layout>()};
    std::shared_ptr<Gff> moduleIfo;
    std::shared_ptr<Gff> areaAre;
    std::shared_ptr<Gff> areaGit;
    std::shared_ptr<const SaveWorkingState> committed {
        std::make_shared<const SaveWorkingState>()};
    SaveSlotDescriptor sourceSlot;
    std::vector<std::pair<std::string, bool>> dispatched;
};

} // namespace

size_t reone::game::TestGameModule::delayedActionCount(const Object &object) {
    return object._delayed.size();
}

// Entering a module for the first time runs both authored hooks, and neither
// is looking at a restored world.
TEST_P(EntryLifecycleFixture, a_fresh_entry_runs_both_hooks_outside_a_restore) {
    serveModule(/*savedModuleSnapshot=*/false);

    game->loadModule("module_b");

    EXPECT_EQ(1, dispatchCount(kOnLoadScript));
    EXPECT_EQ(1, dispatchCount(kOnEnterScript));
    EXPECT_THAT(observedDuring(kOnLoadScript), Optional(false));
    EXPECT_THAT(observedDuring(kOnEnterScript), Optional(false));
}

// Coming back to a module reone has persisted state for is still an entry, and
// the authored hooks are what make the module fit for the party arriving again.
TEST_P(EntryLifecycleFixture, revisiting_a_module_with_persisted_state_still_runs_both_hooks) {
    serveModule(/*savedModuleSnapshot=*/true);

    game->loadModule("module_b");

    EXPECT_EQ(1, dispatchCount(kOnLoadScript))
        << "a revisit must still run the module's authored entry hook";
    EXPECT_EQ(1, dispatchCount(kOnEnterScript));
    EXPECT_THAT(observedDuring(kOnLoadScript), Optional(false))
        << "a revisit is not a save being restored";
    EXPECT_THAT(observedDuring(kOnEnterScript), Optional(false));
}

// Restoring from disk runs the same hooks, and both are told what they are
// being built into.
TEST_P(EntryLifecycleFixture, a_disk_restore_runs_both_hooks_and_tells_them_so) {
    serveModule(/*savedModuleSnapshot=*/true);

    game->loadModule("module_b", "", /*initialSaveRestore=*/true);

    EXPECT_EQ(1, dispatchCount(kOnLoadScript));
    EXPECT_EQ(1, dispatchCount(kOnEnterScript));
    EXPECT_THAT(observedDuring(kOnLoadScript), Optional(true));
    EXPECT_THAT(observedDuring(kOnEnterScript), Optional(true))
        << "the area's OnEnter runs inside the load and reads the same answer";
}

// The answer belongs to the load. Once it is over, nothing still claims to be
// restoring.
TEST_P(EntryLifecycleFixture, the_restore_answer_does_not_outlive_the_load) {
    serveModule(/*savedModuleSnapshot=*/true);

    game->loadModule("module_b", "", /*initialSaveRestore=*/true);

    ASSERT_THAT(observedDuring(kOnEnterScript), Optional(true));
    EXPECT_FALSE(game->isLoadingFromSaveGame());
}

// Rebuilding the party and the area's creatures must not make the module look
// entered more than once.
TEST_P(EntryLifecycleFixture, neither_hook_is_dispatched_twice) {
    serveModule(/*savedModuleSnapshot=*/true);

    game->loadModule("module_b", "", /*initialSaveRestore=*/true);

    EXPECT_EQ(1, dispatchCount(kOnLoadScript));
    EXPECT_EQ(1, dispatchCount(kOnEnterScript));
}

// Authored entry work a revisit performs is actually observable afterwards.
// The module's OnLoad collapses the field party, and the party must really be
// collapsed.
TEST_P(EntryLifecycleFixture, authored_entry_work_on_a_revisit_takes_effect) {
    serveModule(/*savedModuleSnapshot=*/true);
    auto companion = game->newCreature();
    game->party().addAvailableMember(0, companion);
    game->party().addMember(0, companion);
    ASSERT_EQ(2, game->party().getSize());

    // Stand in for what the compiled hook does: the point is that the engine
    // dispatches it at all on a revisit, and that what it does survives the
    // rest of the load.
    EXPECT_CALL(engine.resourceModule().scripts(), get(std::string(kOnLoadScript)))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](const std::string &resRef)
                                   -> std::shared_ptr<ScriptProgram> {
            dispatched.push_back({resRef, game->isLoadingFromSaveGame()});
            game->party().removeMember(0);
            return nullptr;
        }));

    game->loadModule("module_b");

    ASSERT_EQ(1, dispatchCount(kOnLoadScript));
    EXPECT_EQ(1, game->party().getSize())
        << "the revisit's authored entry work must not be discarded";
}

// A restored module is still the object running its authored script. Commands
// delayed from that script belong to the module and resume with OBJECT_SELF
// resolving back to the same module object.
TEST_P(EntryLifecycleFixture, a_restored_module_owns_and_executes_its_delayed_onload_command) {
    serveModule(/*savedModuleSnapshot=*/true);
    auto program = delayedSelfMutation(*routines);
    EXPECT_CALL(engine.resourceModule().scripts(), get(std::string(kOnLoadScript)))
        .WillOnce(Invoke([this, program](const std::string &resRef) {
            dispatched.push_back({resRef, game->isLoadingFromSaveGame()});
            return program;
        }));

    ASSERT_TRUE(game->loadModule("module_b"));

    auto restored = game->module();
    ASSERT_TRUE(restored);
    EXPECT_NE(kObjectSelf, restored->id());
    EXPECT_EQ(restored, game->getObjectById(restored->id()));
    EXPECT_EQ(1u, TestGameModule::delayedActionCount(*restored));
    EXPECT_EQ(0u, TestGameModule::delayedActionCount(*restored->area()));
    EXPECT_EQ(0u, TestGameModule::delayedActionCount(*player));
    EXPECT_FALSE(restored->getLocalBoolean(kDelayedMutationLocal));

    restored->update(0.0f);

    EXPECT_TRUE(restored->getLocalBoolean(kDelayedMutationLocal));
    EXPECT_EQ(0u, TestGameModule::delayedActionCount(*restored));
}

TEST_P(EntryLifecycleFixture, restored_runtime_state_is_visible_to_onload_and_stays_cleared) {
    serveModule(/*savedModuleSnapshot=*/true);
    auto savedRuntime = Gff::Builder()
                            .field(Gff::Field::newList(
                                "EffectList", {savedPermanentEffect()}))
                            .field(Gff::Field::newList(
                                "ActionList", {savedWaitAction()}))
                            .build();
    player->deserializeRuntimeState(
        *savedRuntime,
        SerializedIdentityContext::moduleGraph("module_b"));
    ASSERT_TRUE(player->effects().empty());
    ASSERT_TRUE(player->actions().empty());

    EXPECT_CALL(engine.resourceModule().scripts(), get(std::string(kOnLoadScript)))
        .WillOnce(Invoke([this](const std::string &resRef)
                             -> std::shared_ptr<ScriptProgram> {
            dispatched.push_back({resRef, game->isLoadingFromSaveGame()});
            EXPECT_EQ(1u, player->effects().size());
            EXPECT_EQ(1u, player->actions().size());
            player->clearAllEffects();
            player->clearAllActions();
            return nullptr;
        }));

    ASSERT_TRUE(game->loadModule("module_b", "", /*initialSaveRestore=*/true));
    EXPECT_TRUE(player->effects().empty());
    EXPECT_TRUE(player->actions().empty());
}

TEST_P(EntryLifecycleFixture, restored_runtime_state_is_visible_to_onenter) {
    serveModule(/*savedModuleSnapshot=*/true);
    auto savedRuntime = Gff::Builder()
                            .field(Gff::Field::newList(
                                "EffectList", {savedPermanentEffect()}))
                            .field(Gff::Field::newList(
                                "ActionList", {savedWaitAction()}))
                            .build();
    player->deserializeRuntimeState(
        *savedRuntime,
        SerializedIdentityContext::moduleGraph("module_b"));

    EXPECT_CALL(engine.resourceModule().scripts(), get(std::string(kOnEnterScript)))
        .WillOnce(Invoke([this](const std::string &resRef)
                             -> std::shared_ptr<ScriptProgram> {
            dispatched.push_back({resRef, game->isLoadingFromSaveGame()});
            EXPECT_EQ(1u, player->effects().size());
            EXPECT_EQ(1u, player->actions().size());
            return nullptr;
        }));

    ASSERT_TRUE(game->loadModule("module_b", "", /*initialSaveRestore=*/true));
}

// Fresh and restored entry scripts use the same caller contract. This guards
// the already-working side while the restored-world discriminator above pins
// down the regression.
TEST_P(EntryLifecycleFixture, a_fresh_module_owns_and_executes_its_delayed_onload_command) {
    serveModule(/*savedModuleSnapshot=*/false);
    auto program = delayedSelfMutation(*routines);
    EXPECT_CALL(engine.resourceModule().scripts(), get(std::string(kOnLoadScript)))
        .WillOnce(Invoke([this, program](const std::string &resRef) {
            dispatched.push_back({resRef, game->isLoadingFromSaveGame()});
            return program;
        }));

    ASSERT_TRUE(game->loadModule("module_b"));

    auto fresh = game->module();
    ASSERT_TRUE(fresh);
    EXPECT_NE(kObjectSelf, fresh->id());
    EXPECT_EQ(fresh, game->getObjectById(fresh->id()));
    EXPECT_EQ(1u, TestGameModule::delayedActionCount(*fresh));
    EXPECT_FALSE(fresh->getLocalBoolean(kDelayedMutationLocal));

    fresh->update(0.0f);

    EXPECT_TRUE(fresh->getLocalBoolean(kDelayedMutationLocal));
    EXPECT_EQ(0u, TestGameModule::delayedActionCount(*fresh));
}

TEST_P(EntryLifecycleFixture, ordinary_transition_reconstructs_party_action_queue_once) {
    serveModule(/*savedModuleSnapshot=*/false);
    ASSERT_TRUE(game->loadModule("module_b"));

    auto outgoing = game->newAction<WaitAction>(5.0f);
    player->addAction(outgoing);
    ASSERT_EQ(1u, player->actions().size());

    ASSERT_TRUE(game->loadModule("module_b"));

    ASSERT_EQ(1u, player->actions().size());
    EXPECT_TRUE(outgoing->isCancelled());
    EXPECT_NE(outgoing, player->actions().front());
    ASSERT_TRUE(player->actions().front()->originalSavedAction());
    EXPECT_EQ(30u, player->actions().front()->originalSavedAction()->actionId);

    // A second transition snapshots the reconstructed queue rather than
    // appending another copy of the first transition's record.
    ASSERT_TRUE(game->loadModule("module_b"));
    ASSERT_EQ(1u, player->actions().size());
    ASSERT_TRUE(player->actions().front()->originalSavedAction());
    EXPECT_EQ(30u, player->actions().front()->originalSavedAction()->actionId);
}

TEST_P(EntryLifecycleFixture, ordinary_transition_keeps_only_exact_live_action_target) {
    serveModule(/*savedModuleSnapshot=*/false);
    auto companion = game->newCreature();
    ASSERT_TRUE(game->party().addAvailableMember(0, companion));
    ASSERT_TRUE(game->party().addMember(0, companion));
    ASSERT_TRUE(game->loadModule("module_b"));

    auto outgoing = game->newAction<AttackObjectAction>(companion);
    player->addAction(outgoing);

    ASSERT_TRUE(game->loadModule("module_b"));

    ASSERT_EQ(1u, player->actions().size());
    auto restored = std::dynamic_pointer_cast<AttackObjectAction>(
        player->actions().front());
    ASSERT_TRUE(restored);
    EXPECT_EQ(companion, restored->target());
    EXPECT_TRUE(outgoing->isCancelled());
}

TEST_P(EntryLifecycleFixture, ordinary_transition_reconstructs_physical_feat_for_retained_target) {
    serveModule(/*savedModuleSnapshot=*/false);
    auto companion = game->newCreature();
    ASSERT_TRUE(game->party().addAvailableMember(0, companion));
    ASSERT_TRUE(game->party().addMember(0, companion));
    ASSERT_TRUE(game->loadModule("module_b"));

    auto outgoing = game->newAction<UseFeatAction>(
        FeatType::ImprovedPowerAttack, companion);
    player->addAction(outgoing);
    game->combat().addAction(outgoing, *player);
    ASSERT_EQ(1u, game->combat().roundCount());

    ASSERT_TRUE(game->loadModule("module_b"));

    ASSERT_EQ(1u, player->actions().size());
    auto restored = std::dynamic_pointer_cast<UseFeatAction>(
        player->actions().front());
    ASSERT_TRUE(restored);
    EXPECT_NE(restored, outgoing);
    EXPECT_EQ(restored->target(), companion);
    EXPECT_EQ(restored->feat(), FeatType::ImprovedPowerAttack);
    EXPECT_EQ(restored->result(), AttackResultType::Invalid);
    EXPECT_TRUE(restored->runtimeDependenciesLive());
    EXPECT_TRUE(outgoing->isCancelled());
    EXPECT_EQ(0u, game->combat().roundCount());
}

TEST_P(EntryLifecycleFixture, ordinary_transition_drops_outgoing_area_action_target) {
    serveModule(/*savedModuleSnapshot=*/false);
    ASSERT_TRUE(game->loadModule("module_b"));
    auto outgoingTarget = game->newCreature();
    game->module()->area()->add(outgoingTarget);
    auto outgoing = game->newAction<AttackObjectAction>(outgoingTarget);
    player->addAction(outgoing);
    game->combat().addAction(outgoing, *player);
    ASSERT_EQ(1u, game->combat().roundCount());

    ASSERT_TRUE(game->loadModule("module_b"));

    EXPECT_TRUE(outgoing->isCancelled());
    EXPECT_FALSE(game->isRuntimeObjectLive(*outgoingTarget));
    EXPECT_TRUE(player->actions().empty());
    EXPECT_EQ(0u, game->combat().roundCount());
}

TEST_P(EntryLifecycleFixture, ordinary_transition_drops_physical_feat_with_area_only_target) {
    serveModule(/*savedModuleSnapshot=*/false);
    ASSERT_TRUE(game->loadModule("module_b"));
    auto outgoingTarget = game->newCreature();
    game->module()->area()->add(outgoingTarget);
    auto outgoing = game->newAction<UseFeatAction>(
        FeatType::PowerAttack, outgoingTarget);
    player->addAction(outgoing);
    game->combat().addAction(outgoing, *player);
    ASSERT_EQ(1u, game->combat().roundCount());

    ASSERT_TRUE(game->loadModule("module_b"));

    EXPECT_TRUE(outgoing->isCancelled());
    EXPECT_FALSE(game->isRuntimeObjectLive(*outgoingTarget));
    EXPECT_TRUE(player->actions().empty());
    EXPECT_EQ(0u, game->combat().roundCount());
}

TEST_P(EntryLifecycleFixture, controlled_companion_uses_same_transition_continuity) {
    serveModule(/*savedModuleSnapshot=*/false);
    auto controlled = game->newCreature();
    ASSERT_TRUE(game->party().addAvailableMember(0, controlled));
    game->party().setControlledMember(0, controlled);
    ASSERT_EQ(controlled, game->party().player());
    ASSERT_EQ(player, game->party().actualPlayer());
    ASSERT_TRUE(game->loadModule("module_b"));

    auto outgoing = game->newAction<WaitAction>(5.0f);
    controlled->addAction(outgoing);

    ASSERT_TRUE(game->loadModule("module_b"));

    EXPECT_EQ(controlled, game->party().player());
    EXPECT_EQ(player, game->party().actualPlayer());
    ASSERT_EQ(1u, controlled->actions().size());
    EXPECT_TRUE(outgoing->isCancelled());
    ASSERT_TRUE(controlled->actions().front()->originalSavedAction());
    EXPECT_EQ(
        30u,
        controlled->actions().front()->originalSavedAction()->actionId);
}

TEST_P(EntryLifecycleFixture, k2_puppet_uses_same_transition_continuity) {
    if (GetParam() != GameID::TSL) GTEST_SKIP();
    serveModule(/*savedModuleSnapshot=*/false);
    auto puppet = game->newCreature();
    ASSERT_TRUE(game->party().addAvailablePuppet(0, puppet));
    ASSERT_TRUE(game->party().addPuppet(0, puppet));
    ASSERT_TRUE(game->loadModule("module_b"));

    auto outgoing = game->newAction<WaitAction>(5.0f);
    puppet->addAction(outgoing);

    ASSERT_TRUE(game->loadModule("module_b"));

    ASSERT_EQ(1u, puppet->actions().size());
    EXPECT_TRUE(outgoing->isCancelled());
    ASSERT_TRUE(puppet->actions().front()->originalSavedAction());
    EXPECT_EQ(30u, puppet->actions().front()->originalSavedAction()->actionId);
}

TEST_P(EntryLifecycleFixture, ordinary_transition_preserves_party_delay_due_time) {
    serveModule(/*savedModuleSnapshot=*/false);
    ASSERT_TRUE(game->loadModule("module_b"));
    TestGameModule::setSnapshotWorldTime(*game, 3, 1200, 2);

    auto program = std::make_shared<ScriptProgram>("party_delay");
    program->add(Instruction(InstructionType::RETN));
    auto state = std::make_shared<ExecutionState>();
    state->program = std::move(program);
    state->insOffset = 13;
    state->globals = {Variable::ofInt(7)};
    auto context = std::make_shared<ExecutionContext>();
    context->savedState = std::move(state);
    player->delayAction(
        game->newAction<DoCommandAction>(std::move(context)), 2.0f);
    ASSERT_EQ(1u, TestGameModule::delayedActionCount(*player));

    ASSERT_TRUE(game->loadModule("module_b"));

    EXPECT_EQ(0u, TestGameModule::delayedActionCount(*player));
    ASSERT_TRUE(game->module());
    EXPECT_EQ(1u, game->module()->pendingSavedEventCount());

    game->update(1.0f);
    EXPECT_EQ(1u, game->module()->pendingSavedEventCount());
    game->update(1.1f);
    EXPECT_EQ(0u, game->module()->pendingSavedEventCount());
}

TEST_P(EntryLifecycleFixture, failed_destination_retires_attached_session_creatures_before_teardown) {
    serveModule(/*savedModuleSnapshot=*/false);

    std::shared_ptr<Creature> controlled;
    std::shared_ptr<Creature> puppet;
    if (GetParam() == GameID::TSL) {
        controlled = game->newCreature();
        ASSERT_TRUE(game->party().addAvailableMember(0, controlled));
        game->party().setControlledMember(0, controlled);
        ASSERT_EQ(controlled, game->party().player());

        puppet = game->newCreature();
        ASSERT_TRUE(game->party().addAvailablePuppet(0, puppet));
        ASSERT_TRUE(game->party().addPuppet(0, puppet));
    }

    auto expectedLeader = game->party().player();
    std::shared_ptr<Area> failedArea;
    std::shared_ptr<Trigger> failedTrigger;
    EXPECT_CALL(engine.resourceModule().scripts(), get(std::string(kOnEnterScript)))
        .WillOnce(Invoke([this, &failedArea, &failedTrigger, expectedLeader, puppet](
                             const std::string &resRef)
                             -> std::shared_ptr<ScriptProgram> {
            dispatched.push_back({resRef, game->isLoadingFromSaveGame()});
            failedArea = game->module()->area();
            TestGameModule::setAreaRuntimePath(
                *expectedLeader, failedArea->pathfinder());
            failedTrigger = game->newTrigger();
            failedArea->add(failedTrigger);
            failedTrigger->addTenant(expectedLeader);
            if (puppet) {
                TestGameModule::setAreaRuntimePath(
                    *puppet, failedArea->pathfinder());
                failedTrigger->addTenant(puppet);
            }
            throw std::runtime_error("injected Area OnEnter failure");
        }));

    ASSERT_FALSE(game->loadModule("module_b"));
    ASSERT_TRUE(failedArea);
    ASSERT_TRUE(failedTrigger);
    EXPECT_FALSE(game->module());
    EXPECT_EQ(nullptr, expectedLeader->room());
    EXPECT_FALSE(candidateRoom.tenants().count(expectedLeader.get()));
    EXPECT_FALSE(TestGameModule::hasAreaRuntimePath(*expectedLeader));
    EXPECT_FALSE(failedTrigger->isTenant(expectedLeader));
    const auto &creatures = failedArea->getObjectsByType(ObjectType::Creature);
    EXPECT_EQ(creatures.end(), std::find(creatures.begin(), creatures.end(), expectedLeader));
    EXPECT_EQ(Game::Screen::MainMenu, game->currentScreen());
    EXPECT_FALSE(game->party().player());
    EXPECT_FALSE(game->isRuntimeObjectLive(*expectedLeader));
    EXPECT_FALSE(game->getObjectById(expectedLeader->id()));

    if (controlled) {
        EXPECT_FALSE(game->party().rosterCreature({RosterKind::Npc, 0}));
        EXPECT_EQ(nullptr, player->room())
            << "the parked canonical PC was never attached to the candidate";
    }
    if (puppet) {
        EXPECT_EQ(nullptr, puppet->room());
        EXPECT_FALSE(candidateRoom.tenants().count(puppet.get()));
        EXPECT_FALSE(TestGameModule::hasAreaRuntimePath(*puppet));
        EXPECT_FALSE(failedTrigger->isTenant(puppet));
        EXPECT_FALSE(game->party().rosterCreature({RosterKind::Puppet, 0}));
        EXPECT_FALSE(game->isRuntimeObjectLive(*puppet));
        EXPECT_EQ(creatures.end(), std::find(creatures.begin(), creatures.end(), puppet));
    }

    // A later whole-session retirement sees already-detached objects and is
    // intentionally harmless.
    game->retireRuntimeSession();
}

TEST_P(EntryLifecycleFixture, failed_destination_before_party_placement_is_harmless) {
    serveModule(/*savedModuleSnapshot=*/false);

    std::shared_ptr<Area> failedArea;
    EXPECT_CALL(engine.resourceModule().scripts(), get(std::string(kOnLoadScript)))
        .WillOnce(Invoke([this, &failedArea](const std::string &resRef)
                             -> std::shared_ptr<ScriptProgram> {
            dispatched.push_back({resRef, game->isLoadingFromSaveGame()});
            failedArea = game->module()->area();
            throw std::runtime_error("injected Module OnLoad failure");
        }));

    ASSERT_FALSE(game->loadModule("module_b"));
    ASSERT_TRUE(failedArea);
    EXPECT_FALSE(game->module());
    EXPECT_EQ(nullptr, player->room());
    EXPECT_FALSE(candidateRoom.tenants().count(player.get()));
    EXPECT_TRUE(failedArea->getObjectsByType(ObjectType::Creature).empty());
    EXPECT_EQ(Game::Screen::MainMenu, game->currentScreen());
    EXPECT_FALSE(game->party().player());
    EXPECT_FALSE(game->isRuntimeObjectLive(*player));
    EXPECT_FALSE(game->getObjectById(player->id()));
}

// Session restoration, saved-world identity and placement are independent.
TEST(EntryLifecycleContexts, restoration_predicates_are_orthogonal) {
    EXPECT_FALSE(restoresSavedWorld(ModuleLoadContext::FreshModule));
    EXPECT_FALSE(restoresSavedWorld(ModuleLoadContext::InitialTemplateRestore));
    EXPECT_TRUE(restoresSavedWorld(ModuleLoadContext::SavedModuleTransition));
    EXPECT_TRUE(restoresSavedWorld(ModuleLoadContext::InitialSaveRestore));

    EXPECT_FALSE(restoresSavedSession(ModuleLoadContext::FreshModule));
    EXPECT_FALSE(restoresSavedSession(ModuleLoadContext::SavedModuleTransition));
    EXPECT_TRUE(restoresSavedSession(ModuleLoadContext::InitialTemplateRestore));
    EXPECT_TRUE(restoresSavedSession(ModuleLoadContext::InitialSaveRestore));

    EXPECT_FALSE(preservesSavedPlacement(ModuleLoadContext::FreshModule));
    EXPECT_FALSE(preservesSavedPlacement(ModuleLoadContext::InitialTemplateRestore));
    EXPECT_FALSE(preservesSavedPlacement(ModuleLoadContext::SavedModuleTransition));
    EXPECT_TRUE(preservesSavedPlacement(ModuleLoadContext::InitialSaveRestore));
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    EntryLifecycleFixture,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });
