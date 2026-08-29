/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action.h"
#include "reone/game/effect.h"
#include "reone/game/effect/assuredhit.h"
#include "reone/game/effect/beam.h"
#include "reone/game/effect/modifyattacks.h"
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/gui/hud.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/store.h"
#include "reone/game/object/trigger.h"
#include "reone/game/room.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/scene/collision.h"
#include "reone/scene/node/model.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/system/exception/validation.h"

using namespace reone;
using namespace reone::game;
using namespace testing;

namespace {

class CountingAction : public reone::game::Action {
public:
    CountingAction(Game &game, ServicesView &services, int &executions) :
        reone::game::Action(game, services, ActionType::Invalid),
        _executions(executions) {
    }

    void execute(std::shared_ptr<reone::game::Action> self, Object &actor, float dt) override {
        ++_executions;
        complete();
    }

private:
    int &_executions;
};

class SessionConversation : public Conversation {
public:
    SessionConversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

    void bindOwner(std::shared_ptr<Object> owner) {
        _owner = std::move(owner);
        _dialog = std::make_shared<resource::Dialog>();
        _owner->setIsInConversation(true);
    }

protected:
    void setReplyLines(std::vector<std::string> lines) override {
    }

    void setMessage(std::string message) override {
    }
};

void configureRuntimeMocks(
    TestEngine &engine,
    NiceMock<scene::MockSceneGraph> &sceneGraph) {

    ON_CALL(engine.sceneModule().graphs(), get(_))
        .WillByDefault(ReturnRef(sceneGraph));
}

std::shared_ptr<resource::TwoDA> runtimeBaseItems() {
    resource::TwoDA::Builder builder;
    builder.columns({"equipableslots", "itemclass", "ammunitiontype"});
    builder.row({"2", "i_test", "0"});
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

std::shared_ptr<resource::TwoDA> runtimePlaceables() {
    resource::TwoDA::Builder builder;
    builder.columns({"modelname"});
    builder.row({""});
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

std::shared_ptr<resource::TwoDA> runtimeAppearances() {
    resource::TwoDA::Builder builder;
    builder.columns(
        {"modeltype", "walkdist", "rundist", "perspace", "creperspace",
         "sizecategory", "footsteptype", "envmap", "race"});
    builder.row({"S", "1", "1", "0.6", "0.6", "3", "-1", "", ""});
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

std::shared_ptr<resource::Gff> runtimeItem(
    std::string tag,
    std::optional<uint32_t> savedId = std::nullopt,
    uint32_t structType = 0) {
    resource::Gff::Builder builder;
    builder.type(structType)
        .field(resource::Gff::Field::newCExoString("Tag", std::move(tag)))
        .field(resource::Gff::Field::newInt("BaseItem", 0))
        .field(resource::Gff::Field::newWord("StackSize", 1));
    if (savedId) {
        builder.field(resource::Gff::Field::newDword("ObjectId", *savedId));
    }
    return builder.build();
}

} // namespace

void reone::game::TestGameModule::cacheActiveModule(Game &game, std::string name) {
    game._loadedModules.emplace(std::move(name), game._module);
}

size_t reone::game::TestGameModule::objectRegistrySize(const Game &game) {
    return game._objectById.size();
}

size_t reone::game::TestGameModule::loadedModuleCount(const Game &game) {
    return game._loadedModules.size();
}

uint32_t reone::game::TestGameModule::nextObjectId(const Game &game) {
    return game._nextObjectId;
}

uint64_t reone::game::TestGameModule::runtimeSessionGeneration(const Game &game) {
    return game._runtimeSessionGeneration;
}

uint64_t reone::game::TestGameModule::savedGraphGeneration(const Game &game) {
    return game._savedGraphGeneration;
}

void reone::game::TestGameModule::setAreaRuntimePath(
    Creature &creature, Pathfinder &pathfinder) {
    pathfinder.paths.emplace_back();
    pathfinder.paths.back().active = true;
    creature._path = Path {static_cast<int32_t>(pathfinder.paths.size() - 1)};
}

bool reone::game::TestGameModule::hasAreaRuntimePath(const Creature &creature) {
    return creature._path.has_value();
}

size_t reone::game::TestGameModule::seenObjectCount(const Creature &creature) {
    return creature._perception.seen.size();
}

size_t reone::game::TestGameModule::heardObjectCount(const Creature &creature) {
    return creature._perception.heard.size();
}

void reone::game::TestGameModule::setAreaRuntimeSceneNode(
    Object &object, std::shared_ptr<scene::SceneNode> sceneNode) {
    object._sceneNode = std::move(sceneNode);
}

void reone::game::TestGameModule::registerSavedModuleReferenceTarget(
    Game &game,
    const std::shared_ptr<Module> &module,
    const SerializedIdentityContext &identityContext) {
    game.registerSavedModuleReferenceTarget(module, identityContext);
}

void reone::game::TestGameModule::markSpawnScriptFired(Creature &creature) {
    creature._spawnScriptFired = true;
}

void reone::game::TestGameModule::bindConversation(Game &game, Conversation &conversation) {
    game._conversation = &conversation;
}

bool reone::game::TestGameModule::hasConversation(const Game &game) {
    return game._conversation != nullptr;
}

void reone::game::TestGameModule::bindHUDSelection(
    Game &game,
    std::shared_ptr<Object> object) {

    game._hud = std::make_unique<HUD>(game, game._services);
    game._hud->_select._selectedObject = object;
    game._hud->_select._hilightedObject = std::move(object);
}

bool reone::game::TestGameModule::hasHUDSelection(const Game &game) {
    if (!game._hud) {
        return false;
    }
    return static_cast<bool>(game._hud->_select._selectedObject) ||
           static_cast<bool>(game._hud->_select._hilightedObject);
}

TEST(RuntimeSession, retirement_removes_all_gameplay_ownership_and_restarts_ids) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    TestGameModule::setActiveModule(game, true);
    TestGameModule::cacheActiveModule(game, "session_a");
    auto oldModule = game.module();
    auto oldArea = game.newArea();
    auto oldPlayer = game.newCreature();
    game.party().addMember(kNpcPlayer, oldPlayer);
    game.party().setPlayer(oldPlayer);
    game.openInGame();

    const auto moduleId = oldModule->id();
    const auto areaId = oldArea->id();
    const auto playerId = oldPlayer->id();
    ASSERT_TRUE(game.hasPlayableRuntimeSession());
    ASSERT_EQ(3, TestGameModule::objectRegistrySize(game));
    ASSERT_EQ(1, TestGameModule::loadedModuleCount(game));

    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(1);
    game.retireRuntimeSession();

    EXPECT_FALSE(game.hasPlayableRuntimeSession());
    EXPECT_EQ(Game::Screen::None, game.currentScreen());
    EXPECT_FALSE(game.module());
    EXPECT_FALSE(game.party().player());
    EXPECT_TRUE(game.party().members().empty());
    EXPECT_EQ(0, TestGameModule::objectRegistrySize(game));
    EXPECT_EQ(0, TestGameModule::loadedModuleCount(game));
    EXPECT_FALSE(game.getObjectById(moduleId));
    EXPECT_FALSE(game.getObjectById(areaId));
    EXPECT_FALSE(game.getObjectById(playerId));
    EXPECT_EQ(2, TestGameModule::nextObjectId(game));

    auto replacement = game.newCreature();
    EXPECT_EQ(2, replacement->id());
    EXPECT_NE(oldModule.get(), game.getObjectById(replacement->id()).get());
}

TEST(RuntimeSession, retirement_unpublishes_conversation_and_gui_object_references) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto participant = game.newCreature();
    SessionConversation conversation(game, engine.services());
    conversation.bindOwner(participant);
    TestGameModule::bindConversation(game, conversation);
    TestGameModule::bindHUDSelection(game, participant);

    ASSERT_TRUE(participant->isInConversation());
    ASSERT_TRUE(TestGameModule::hasConversation(game));
    ASSERT_TRUE(TestGameModule::hasHUDSelection(game));

    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(1);
    game.retireRuntimeSession();

    EXPECT_FALSE(participant->isInConversation());
    EXPECT_FALSE(TestGameModule::hasConversation(game));
    EXPECT_FALSE(TestGameModule::hasHUDSelection(game));
    EXPECT_FALSE(game.getObjectById(participant->id()));
}

TEST(RuntimeSession, retirement_preserves_save_wide_logical_and_resource_state) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    game.setGlobalBoolean("BOOL", true);
    game.setGlobalNumber("NUMBER", 42);
    game.setGlobalString("STRING", "committed");
    game.setCustomToken(31, "persist");
    Party::PersistedState partyState;
    partyState.pcName = "B Player";
    partyState.selectedPlanet = 7;
    game.party().setPersistedState(partyState);
    game.party().giveGold(123);
    game.party().setXP(456);
    game.journal().restoreEntry("plot_b", 20, 3, 4);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    EXPECT_CALL(engine.resourceModule().director(),
                onGameLoad(An<std::string_view>()))
        .Times(0);
    EXPECT_CALL(engine.resourceModule().director(),
                onGameLoad(An<const resource::SaveSlotDescriptor &>()))
        .Times(0);
    EXPECT_CALL(engine.resourceModule().director(), onModuleLoad(_)).Times(0);
    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(1);
    game.retireRuntimeSession();

    EXPECT_TRUE(game.getGlobalBoolean("BOOL"));
    EXPECT_EQ(42, game.getGlobalNumber("NUMBER"));
    EXPECT_EQ("committed", game.getGlobalString("STRING"));
    EXPECT_EQ("persist", game.substituteCustomTokens("<CUSTOM31>"));
    EXPECT_EQ("B Player", game.party().persistedState().pcName);
    EXPECT_EQ(7, game.party().persistedState().selectedPlanet);
    EXPECT_EQ(123, game.party().gold());
    EXPECT_EQ(456, game.party().xp());
    EXPECT_EQ(20, game.journal().getEntryState("plot_b"));
    EXPECT_FALSE(game.party().player());
}

TEST(RuntimeSession, retirement_is_idempotent_and_cycles_do_not_grow_ownership) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(6);
    EXPECT_CALL(sceneGraph, clear()).Times(6);
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto first = game.newCreature();
        game.newArea();
        game.newModule();
        ASSERT_EQ(2, first->id());
        ASSERT_EQ(3, TestGameModule::objectRegistrySize(game));

        game.retireRuntimeSession();
        EXPECT_EQ(0, TestGameModule::objectRegistrySize(game));
        EXPECT_EQ(2, TestGameModule::nextObjectId(game));

        game.retireRuntimeSession();
        EXPECT_EQ(0, TestGameModule::objectRegistrySize(game));
        EXPECT_EQ(2, TestGameModule::nextObjectId(game));
    }
}

TEST(RuntimeSession, retired_actions_cannot_execute_through_gameplay_update) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    int executions = 0;
    auto actor = game.newCreature();
    auto action = game.newAction<CountingAction>(executions);
    actor->addAction(action);

    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(1);
    game.retireRuntimeSession();
    game.update(10.0f);

    EXPECT_EQ(0, executions);
    EXPECT_FALSE(game.getObjectById(actor->id()));
}

TEST(RuntimeSession, publication_is_explicit_and_retirement_returns_to_non_playable) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    TestGameModule::setActiveModule(game, true);
    EXPECT_FALSE(game.hasPlayableRuntimeSession());
    game.openInGame();
    EXPECT_TRUE(game.hasPlayableRuntimeSession());

    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(1);
    game.retireRuntimeSession();
    EXPECT_FALSE(game.hasPlayableRuntimeSession());
    EXPECT_FALSE(game.module());
}

TEST(RuntimeSession, scheduling_an_ordinary_module_transition_preserves_session_state) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    TestGameModule::setActiveModule(game, true);
    auto object = game.newCreature();
    game.setGlobalNumber("SESSION", 9);
    game.setCustomToken(31, "transition");
    game.openInGame();

    game.scheduleModuleTransition("module_b", "entry_b");

    EXPECT_TRUE(game.hasPlayableRuntimeSession());
    EXPECT_EQ(object, game.getObjectById(object->id()));
    EXPECT_EQ(9, game.getGlobalNumber("SESSION"));
    EXPECT_EQ("transition", game.substituteCustomTokens("<CUSTOM31>"));
    EXPECT_TRUE(game.module());
}

TEST(RuntimeSession, full_game_reset_still_clears_logical_state) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    game.setGlobalBoolean("BOOL", true);
    game.setCustomToken(31, "old");
    Party::PersistedState partyState;
    partyState.pcName = "old";
    game.party().setPersistedState(partyState);
    game.journal().restoreEntry("old_plot", 30, 1, 2);
    game.newCreature();

    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(1);
    game.resetGame();

    EXPECT_FALSE(game.getGlobalBoolean("BOOL"));
    EXPECT_EQ("<CUSTOM31>", game.substituteCustomTokens("<CUSTOM31>"));
    EXPECT_TRUE(game.party().persistedState().pcName.empty());
    EXPECT_EQ(0, game.journal().getEntryState("old_plot"));
    EXPECT_EQ(0, TestGameModule::objectRegistrySize(game));
}

TEST(RuntimeSession, saved_session_can_publish_repeated_ordinary_module_transitions_without_accumulation) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    TestGameModule::setActiveModule(game, true);
    TestGameModule::cacheActiveModule(game, "module_a");
    auto oldModule = game.module();
    auto oldArea = game.newArea();
    auto oldWorldObject = game.newCreature();

    auto player = game.newCreature();
    auto inventoryItem = game.newItem();
    player->addItem(inventoryItem);
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setActualPlayer(player);

    auto available = game.newCreature();
    game.party().addAvailableMember(0, available);
    game.setGlobalNumber("SESSION", 9);
    game.openInGame();

    auto sessionGeneration = TestGameModule::runtimeSessionGeneration(game);
    auto graphGeneration = TestGameModule::savedGraphGeneration(game);
    auto oldModuleId = oldModule->id();
    auto oldAreaId = oldArea->id();
    auto oldWorldObjectId = oldWorldObject->id();
    ASSERT_EQ(6, TestGameModule::objectRegistrySize(game));
    ASSERT_EQ(1, TestGameModule::loadedModuleCount(game));

    game.retireActiveModuleRuntime();

    EXPECT_FALSE(game.hasPlayableRuntimeSession());
    EXPECT_FALSE(game.module());
    EXPECT_EQ(0, TestGameModule::loadedModuleCount(game));
    EXPECT_EQ(3, TestGameModule::objectRegistrySize(game));
    EXPECT_FALSE(game.getObjectById(oldModuleId));
    EXPECT_FALSE(game.getObjectById(oldAreaId));
    EXPECT_FALSE(game.getObjectById(oldWorldObjectId));
    EXPECT_EQ(player, game.getObjectById(player->id()));
    EXPECT_EQ(inventoryItem, game.getObjectById(inventoryItem->id()));
    EXPECT_EQ(available, game.getObjectById(available->id()));
    EXPECT_EQ(9, game.getGlobalNumber("SESSION"));
    EXPECT_EQ(sessionGeneration, TestGameModule::runtimeSessionGeneration(game));
    EXPECT_EQ(
        graphGeneration + 1,
        TestGameModule::savedGraphGeneration(game));

    TestGameModule::setActiveModule(game, true);
    TestGameModule::cacheActiveModule(game, "module_b");
    auto targetModule = game.module();
    auto targetArea = game.newArea();
    auto targetWorldObject = game.newCreature();
    ASSERT_EQ(6, TestGameModule::objectRegistrySize(game));
    game.openInGame();

    EXPECT_TRUE(game.hasPlayableRuntimeSession());
    EXPECT_EQ(targetModule, game.module());
    EXPECT_EQ(targetModule, game.getObjectById(targetModule->id()));
    EXPECT_EQ(targetArea, game.getObjectById(targetArea->id()));
    EXPECT_EQ(targetWorldObject, game.getObjectById(targetWorldObject->id()));
    EXPECT_EQ(sessionGeneration, TestGameModule::runtimeSessionGeneration(game));

    game.retireActiveModuleRuntime();

    EXPECT_FALSE(game.hasPlayableRuntimeSession());
    EXPECT_EQ(3, TestGameModule::objectRegistrySize(game));
    EXPECT_EQ(0, TestGameModule::loadedModuleCount(game));
    EXPECT_EQ(player, game.getObjectById(player->id()));
    EXPECT_EQ(inventoryItem, game.getObjectById(inventoryItem->id()));
    EXPECT_EQ(available, game.getObjectById(available->id()));
    EXPECT_FALSE(game.getObjectById(targetModule->id()));
    EXPECT_FALSE(game.getObjectById(targetArea->id()));
    EXPECT_FALSE(game.getObjectById(targetWorldObject->id()));
    EXPECT_EQ(sessionGeneration, TestGameModule::runtimeSessionGeneration(game));
    EXPECT_EQ(
        graphGeneration + 2,
        TestGameModule::savedGraphGeneration(game));
}

TEST(ModuleLoadContext, separates_saved_world_provenance_from_session_placement) {
    auto fresh = resolveModuleLoadContext(false, false);
    auto initialRestore = resolveModuleLoadContext(true, true);
    auto savedTransition = resolveModuleLoadContext(false, true);

    EXPECT_EQ(ModuleLoadContext::FreshModule, fresh);
    EXPECT_FALSE(restoresSavedWorld(fresh));
    EXPECT_FALSE(restoresSavedSession(fresh));

    EXPECT_EQ(ModuleLoadContext::InitialSaveRestore, initialRestore);
    EXPECT_TRUE(restoresSavedWorld(initialRestore));
    EXPECT_TRUE(restoresSavedSession(initialRestore));

    EXPECT_EQ(ModuleLoadContext::SavedModuleTransition, savedTransition);
    EXPECT_TRUE(restoresSavedWorld(savedTransition));
    EXPECT_FALSE(restoresSavedSession(savedTransition));
}

TEST(RuntimeSession, ordinary_transition_repositions_live_party_and_rebinds_its_room) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    Room sourceRoom("source", glm::vec3(0.0f), nullptr, nullptr, nullptr);
    Room destinationRoom("destination", glm::vec3(0.0f), nullptr, nullptr, nullptr);
    ON_CALL(sceneGraph, testElevation(_, _))
        .WillByDefault(Invoke([&destinationRoom](
                                  const glm::vec3 &position,
                                  scene::Collision &collision) {
            collision.intersection = position;
            collision.user = &destinationRoom;
            return true;
        }));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto sourceArea = game.newArea();
    auto player = game.newCreature();
    player->setPosition(glm::vec3(91.83f, 146.54f, 3.75f));
    player->setFacing(1.25f);
    player->setCurrentHitPoints(17);
    auto effect = std::make_shared<Effect>(EffectType::Invalid);
    player->applyEffect(effect, DurationType::Permanent);
    int actionExecutions = 0;
    auto action = game.newAction<CountingAction>(actionExecutions);
    player->addAction(action);
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setActualPlayer(player);
    auto companion = game.newCreature();
    companion->setPosition(glm::vec3(90.0f, 145.0f, 3.75f));
    game.party().addAvailableMember(0, companion);
    game.party().addMember(0, companion);

    sourceArea->add(player);
    sourceArea->add(companion);
    player->setRoom(&sourceRoom);
    companion->setRoom(&sourceRoom);
    player->startStuntMode();
    ASSERT_TRUE(player->isStuntMode());
    ASSERT_EQ(&sourceRoom, player->room());
    ASSERT_EQ(&sourceRoom, companion->room());
    ASSERT_TRUE(sourceRoom.tenants().count(player.get()));
    ASSERT_TRUE(sourceRoom.tenants().count(companion.get()));

    sourceArea->unloadParty();

    EXPECT_EQ(nullptr, player->room());
    EXPECT_EQ(nullptr, companion->room());
    EXPECT_FALSE(player->isStuntMode());
    EXPECT_FALSE(sourceRoom.tenants().count(player.get()));
    EXPECT_FALSE(sourceRoom.tenants().count(companion.get()));
    EXPECT_EQ(player, game.party().player());
    ASSERT_EQ(2, game.party().getSize());
    EXPECT_EQ(companion, game.party().getMember(1));
    EXPECT_EQ(17, player->currentHitPoints());
    ASSERT_EQ(1, player->effects().size());
    EXPECT_EQ(effect, player->effects().front().effect);
    EXPECT_TRUE(player->actions().empty());
    EXPECT_TRUE(action->isCancelled());
    EXPECT_EQ(0, actionExecutions);

    auto destinationArea = game.newArea();
    glm::vec3 entry(9.085618f, -42.946671f, 0.0f);
    destinationArea->loadParty(entry, 0.5f, false);

    EXPECT_FALSE(player->isStuntMode());
    EXPECT_EQ(entry, player->position());
    EXPECT_FLOAT_EQ(0.5f, player->getFacing());
    EXPECT_EQ(&destinationRoom, player->room());
    EXPECT_TRUE(destinationRoom.tenants().count(player.get()));
    EXPECT_EQ(&destinationRoom, companion->room());
    EXPECT_EQ(player, game.party().player());
    EXPECT_TRUE(destinationRoom.tenants().count(companion.get()));
    EXPECT_EQ(player, game.getObjectById(player->id()));
    EXPECT_EQ(17, player->currentHitPoints());
    EXPECT_EQ(1, player->effects().size());
    EXPECT_TRUE(player->actions().empty());
    EXPECT_EQ(0, actionExecutions);
    auto &destinationCreatures = destinationArea->getObjectsByType(ObjectType::Creature);
    EXPECT_NE(destinationCreatures.end(),
              std::find(destinationCreatures.begin(), destinationCreatures.end(), player));
    EXPECT_NE(destinationCreatures.end(),
              std::find(destinationCreatures.begin(), destinationCreatures.end(), companion));
}

TEST(RuntimeSession, initial_save_restore_preserves_archived_party_placement) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    Room savedRoom("saved", glm::vec3(0.0f), nullptr, nullptr, nullptr);
    ON_CALL(sceneGraph, testElevation(_, _))
        .WillByDefault(Invoke([&savedRoom](
                                  const glm::vec3 &position,
                                  scene::Collision &collision) {
            collision.intersection = position;
            collision.user = &savedRoom;
            return true;
        }));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto area = game.newArea();
    auto player = game.newCreature();
    glm::vec3 archivedPosition(11.3286257f, -40.3899155f, 0.0f);
    player->setPosition(archivedPosition);
    player->setFacing(0.75f);
    player->setCurrentHitPoints(23);
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    area->loadParty(glm::vec3(-6.82759f, -26.73170f, 0.0f), 1.5f, true);

    EXPECT_EQ(archivedPosition, player->position());
    EXPECT_FLOAT_EQ(0.75f, player->getFacing());
    EXPECT_EQ(&savedRoom, player->room());
    EXPECT_TRUE(savedRoom.tenants().count(player.get()));
    EXPECT_EQ(23, player->currentHitPoints());
}

namespace {

// Script execution counts keyed by resref. Static so the stub installed on a
// scripts mock stays valid for the lifetime of the test that installed it.
std::map<std::string, int> &spawnRunCounts() {
    static std::map<std::string, int> counts;
    return counts;
}

// Start counting dispatches of the named scripts. Each resolves to no program,
// which is enough to observe that the runner was asked to run it.
void countSpawnRuns(TestEngine &engine, const std::vector<std::string> &resRefs) {
    for (const auto &resRef : resRefs) {
        spawnRunCounts()[resRef] = 0;
        EXPECT_CALL(engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](const std::string &key) {
                ++spawnRunCounts()[key];
                return std::shared_ptr<script::ScriptProgram>();
            }));
    }
}

int spawnRuns(const std::string &resRef) {
    return spawnRunCounts()[resRef];
}

// A live party crossing module boundaries. Areas come and go around it; the
// party creatures are session-owned and outlive every one of them.
struct PartyTransferHarness {
    TestEngine engine;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    StubConsole console;
    Room room {"transfer", glm::vec3(0.0f), nullptr, nullptr, nullptr};
    std::unique_ptr<Game> game;
    std::shared_ptr<Creature> player;
    std::shared_ptr<Creature> companion;
    std::shared_ptr<Area> area;

    explicit PartyTransferHarness(resource::GameID gameId) {
        engine.init();
        configureRuntimeMocks(engine, sceneGraph);
        ON_CALL(sceneGraph, testElevation(_, _))
            .WillByDefault(Invoke([this](
                                      const glm::vec3 &position,
                                      scene::Collision &collision) {
                collision.intersection = position;
                collision.user = &room;
                return true;
            }));
        game = std::make_unique<Game>(
            gameId, "", engine.options(), engine.services(), console);
        game->initLocalServices();
        countSpawnRuns(engine, {"pc_spawn", "npc_spawn", "extra_spawn"});

        player = game->newCreature();
        player->setOnSpawn("pc_spawn");
        game->party().addMember(kNpcPlayer, player);
        game->party().setPlayer(player);
        game->party().setActualPlayer(player);

        companion = game->newCreature();
        companion->setOnSpawn("npc_spawn");
        game->party().addAvailableMember(0, companion);
        game->party().addMember(0, companion);
    }

    // One ordinary module transition: the source area retires, a destination
    // area is mounted, and the same party creatures are positioned into it.
    std::shared_ptr<Area> transitionTo(const glm::vec3 &entry, float facing) {
        if (area) {
            area->unloadParty();
        }
        auto previous = std::move(area);
        area = game->newArea();
        area->loadParty(entry, facing, false);
        return previous;
    }
};

} // namespace

TEST(PartyTransferSpawn, newly_created_area_creature_runs_its_spawn_script_exactly_once) {
    for (auto gameId : {resource::GameID::KotOR, resource::GameID::TSL}) {
        PartyTransferHarness harness(gameId);
        auto stranger = harness.game->newCreature();
        stranger->setOnSpawn("extra_spawn");
        harness.area = harness.game->newArea();
        harness.area->add(stranger);

        harness.area->runSpawnScripts();
        EXPECT_EQ(1, spawnRuns("extra_spawn"));

        // A second module-load style sweep over the same live creature is not
        // a second creation.
        harness.area->runSpawnScripts();
        EXPECT_EQ(1, spawnRuns("extra_spawn"));
    }
}

TEST(PartyTransferSpawn, ordinary_transition_keeps_retained_party_members_spawned) {
    for (auto gameId : {resource::GameID::KotOR, resource::GameID::TSL}) {
        PartyTransferHarness harness(gameId);

        auto sourceArea = harness.transitionTo(glm::vec3(3.0f, 4.0f, 0.0f), 0.25f);
        EXPECT_EQ(nullptr, sourceArea);
        EXPECT_EQ(1, spawnRuns("pc_spawn"));
        EXPECT_EQ(1, spawnRuns("npc_spawn"));
        EXPECT_TRUE(harness.player->spawnScriptFired());
        EXPECT_TRUE(harness.companion->spawnScriptFired());

        auto playerId = harness.player->id();
        auto companionId = harness.companion->id();
        auto *playerObject = harness.player.get();
        auto *companionObject = harness.companion.get();

        auto retired = harness.transitionTo(glm::vec3(-9.0f, 12.5f, 0.0f), 1.5f);

        // Same session-owned creatures, positioned into the destination area.
        ASSERT_TRUE(retired);
        EXPECT_EQ(playerObject, harness.game->party().player().get());
        EXPECT_EQ(companionObject, harness.game->party().getMember(1).get());
        EXPECT_EQ(playerId, harness.player->id());
        EXPECT_EQ(companionId, harness.companion->id());
        EXPECT_EQ(harness.player, harness.game->getObjectById(playerId));
        EXPECT_EQ(harness.companion, harness.game->getObjectById(companionId));

        // Attachment to a new area is not a new creation.
        EXPECT_EQ(1, spawnRuns("pc_spawn"));
        EXPECT_EQ(1, spawnRuns("npc_spawn"));

        // The retired area no longer owns them; the destination one does.
        const auto &retiredCreatures = retired->getObjectsByType(ObjectType::Creature);
        EXPECT_EQ(retiredCreatures.end(),
                  std::find(retiredCreatures.begin(), retiredCreatures.end(), harness.player));
        EXPECT_EQ(retiredCreatures.end(),
                  std::find(retiredCreatures.begin(), retiredCreatures.end(), harness.companion));
        const auto &creatures = harness.area->getObjectsByType(ObjectType::Creature);
        EXPECT_NE(creatures.end(),
                  std::find(creatures.begin(), creatures.end(), harness.player));
        EXPECT_NE(creatures.end(),
                  std::find(creatures.begin(), creatures.end(), harness.companion));
        EXPECT_EQ(&harness.room, harness.player->room());
        EXPECT_EQ(&harness.room, harness.companion->room());
        EXPECT_EQ(glm::vec3(-9.0f, 12.5f, 0.0f), harness.player->position());
        EXPECT_FLOAT_EQ(1.5f, harness.player->getFacing());
    }
}

TEST(PartyTransferSpawn, repeated_transitions_never_respawn_the_same_party) {
    for (auto gameId : {resource::GameID::KotOR, resource::GameID::TSL}) {
        PartyTransferHarness harness(gameId);
        auto *companionObject = harness.companion.get();
        auto companionId = harness.companion->id();

        // A -> B -> A -> B.
        harness.transitionTo(glm::vec3(1.0f, 1.0f, 0.0f), 0.0f);
        harness.transitionTo(glm::vec3(2.0f, 2.0f, 0.0f), 0.5f);
        harness.transitionTo(glm::vec3(1.0f, 1.0f, 0.0f), 1.0f);
        harness.transitionTo(glm::vec3(2.0f, 2.0f, 0.0f), 1.5f);

        EXPECT_EQ(1, spawnRuns("pc_spawn"));
        EXPECT_EQ(1, spawnRuns("npc_spawn"));
        EXPECT_EQ(companionObject, harness.game->party().getMember(1).get());
        EXPECT_EQ(companionId, harness.companion->id());
        EXPECT_EQ(harness.companion, harness.game->getObjectById(companionId));
    }
}

TEST(PartyTransferSpawn, transferred_party_member_keeps_durable_not_area_execution_state) {
    for (auto gameId : {resource::GameID::KotOR, resource::GameID::TSL}) {
        PartyTransferHarness harness(gameId);
        harness.transitionTo(glm::vec3(0.0f, 0.0f, 0.0f), 0.0f);

        harness.companion->setCurrentHitPoints(19);
        harness.companion->setLocalBoolean(3, true);
        harness.companion->setLocalNumber(5, 42);
        auto effect = std::make_shared<Effect>(EffectType::Invalid);
        harness.companion->applyEffect(effect, DurationType::Permanent);
        int executions = 0;
        auto action = harness.game->newAction<CountingAction>(executions);
        harness.companion->addAction(action);
        auto carried = harness.game->newOwnedItem();
        harness.companion->addItem(carried);
        auto equipped = harness.game->newOwnedItem();
        TestGameModule::setSnapshotEquipment(
            *harness.companion, InventorySlots::rightWeapon, equipped);

        harness.transitionTo(glm::vec3(7.0f, -3.0f, 0.0f), 2.0f);

        EXPECT_EQ(19, harness.companion->currentHitPoints());
        EXPECT_TRUE(harness.companion->getLocalBoolean(3));
        EXPECT_EQ(42, harness.companion->getLocalNumber(5));
        ASSERT_EQ(1, harness.companion->effects().size());
        EXPECT_EQ(effect, harness.companion->effects().front().effect);
        EXPECT_TRUE(harness.companion->actions().empty());
        EXPECT_TRUE(action->isCancelled());
        EXPECT_EQ(0, executions);
        ASSERT_EQ(1, harness.companion->items().size());
        EXPECT_EQ(carried, harness.companion->items().front());
        EXPECT_EQ(equipped,
                  harness.companion->getEquippedItem(InventorySlots::rightWeapon));
        EXPECT_EQ(1, spawnRuns("npc_spawn"));
    }
}

TEST(AreaRuntimeRetirement, canonical_boundary_retires_every_area_owned_attachment) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);

    auto area = game.newArea();
    auto retained = game.newCreature();
    auto outgoing = game.newCreature();
    auto trigger = game.newTrigger();
    trigger->deserialize(
        *resource::Gff::Builder().build(),
        SerializedIdentityContext::templateResource());

    Party::PersistedState state;
    state.controlledNpc = 0;
    state.npcAvailable[0] = true;
    game.party().setPersistedState(state);
    game.party().addAvailableMember(0, retained);
    game.party().addMember(0, retained);
    game.party().setPlayer(retained);
    game.party().setActualPlayer(game.newCreature());

    auto root = std::make_shared<graphics::ModelNode>(
        0, "retained", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto model = std::make_unique<graphics::Model>(
        "retained", 0, root,
        std::vector<std::shared_ptr<graphics::Animation>>(), "", 1.0f);
    auto sceneNode = std::make_shared<scene::ModelSceneNode>(
        *model, scene::ModelUsage::Creature, sceneGraph,
        engine.graphicsModule().services(),
        engine.audioModule().services(),
        engine.resourceModule().services());
    TestGameModule::setAreaRuntimeSceneNode(*retained, sceneNode);

    area->add(outgoing);
    area->add(trigger);
    area->add(retained);
    Room room("source", glm::vec3(0.0f), nullptr, nullptr, nullptr);
    retained->setRoom(&room);
    trigger->addTenant(retained);
    TestGameModule::setAreaRuntimePath(*retained, area->pathfinder());

    retained->beginCombatAttack(outgoing, FeatType::Invalid);
    retained->setAttemptedAttackTarget(outgoing->id());
    retained->setLastHostileActor(outgoing->id());
    retained->setObjectSeen(outgoing, true);
    retained->setObjectHeard(outgoing, true);
    retained->setBlockingDoor(outgoing->id());
    retained->startStuntMode();

    int executions = 0;
    auto action = game.newAction<CountingAction>(executions);
    auto delayed = game.newAction<CountingAction>(executions);
    retained->addAction(action);
    retained->delayAction(delayed, 30.0f);

    retained->setCurrentHitPoints(19);
    retained->setLocalBoolean(3, true);
    retained->setLocalNumber(4, 23);
    retained->setAppearance(42);
    auto carried = game.newOwnedItem();
    retained->addItem(carried);
    auto durableEffect = std::make_shared<Effect>(EffectType::Invalid);
    retained->applyEffect(durableEffect, DurationType::Permanent);
    retained->applyEffect(
        std::make_shared<ModifyAttacksEffect>(1), DurationType::Permanent);
    retained->applyEffect(
        std::make_shared<AssuredHitEffect>(), DurationType::Permanent);
    ASSERT_EQ(1, retained->modifiedAttacks());
    ASSERT_TRUE(retained->hasAssuredHit());

    std::weak_ptr<Object> outgoingEffectTarget = outgoing;
    auto beamEffect = std::make_shared<BeamEffect>(
        0, outgoing, BodyNode::Chest, false);
    retained->applyEffect(beamEffect, DurationType::Permanent);

    EffectInstance areaBoundEffect;
    areaBoundEffect.effect = std::make_shared<Effect>(EffectType::Invalid);
    areaBoundEffect.id = game.allocateEffectId();
    areaBoundEffect.subType = static_cast<uint16_t>(DurationType::Permanent);
    areaBoundEffect.creatorId = outgoing->id();
    areaBoundEffect.objectParameters[0] = outgoing->id();
    ASSERT_TRUE(game.bindEffectCreator(areaBoundEffect));
    ASSERT_TRUE(retained->restoreEffect(std::move(areaBoundEffect)));

    ASSERT_EQ(&room, retained->room());
    ASSERT_TRUE(room.tenants().count(retained.get()));
    ASSERT_TRUE(trigger->isTenant(retained));
    ASSERT_TRUE(TestGameModule::hasAreaRuntimePath(*retained));
    ASSERT_TRUE(retained->getAttackTarget());
    ASSERT_EQ(1u, TestGameModule::seenObjectCount(*retained));
    ASSERT_EQ(1u, TestGameModule::heardObjectCount(*retained));
    ASSERT_EQ(1u, TestGameModule::delayedActionCount(*retained));
    EXPECT_CALL(sceneGraph, removeRoot(A<scene::ModelSceneNode &>()))
        .WillOnce(Invoke([expected = sceneNode.get()](scene::ModelSceneNode &node) {
            EXPECT_EQ(expected, &node);
        }));

    area->retireCreatureRuntime(retained);

    EXPECT_EQ(nullptr, retained->room());
    EXPECT_FALSE(room.tenants().count(retained.get()));
    EXPECT_FALSE(trigger->isTenant(retained));
    EXPECT_FALSE(TestGameModule::hasAreaRuntimePath(*retained));
    ASSERT_EQ(1u, area->pathfinder().paths.size());
    EXPECT_FALSE(area->pathfinder().paths.front().active);
    EXPECT_FALSE(retained->isInCombat());
    EXPECT_FALSE(retained->getAttackTarget());
    EXPECT_EQ(script::kObjectInvalid, retained->getAttemptedAttackTarget());
    EXPECT_EQ(script::kObjectInvalid, retained->getLastHostileActor());
    EXPECT_EQ(0u, TestGameModule::seenObjectCount(*retained));
    EXPECT_EQ(0u, TestGameModule::heardObjectCount(*retained));
    EXPECT_EQ(script::kObjectInvalid, retained->blockingDoorId());
    EXPECT_TRUE(retained->actions().empty());
    EXPECT_TRUE(action->isCancelled());
    EXPECT_EQ(0u, TestGameModule::delayedActionCount(*retained));
    EXPECT_FALSE(retained->isStuntMode());

    EXPECT_EQ(19, retained->currentHitPoints());
    EXPECT_TRUE(retained->getLocalBoolean(3));
    EXPECT_EQ(23, retained->getLocalNumber(4));
    EXPECT_EQ(42, retained->appearance());
    ASSERT_EQ(1u, retained->items().size());
    EXPECT_EQ(carried, retained->items().front());
    ASSERT_EQ(5u, retained->effects().size());
    EXPECT_EQ(durableEffect, retained->effects().front().effect);
    EXPECT_EQ(1, retained->modifiedAttacks());
    EXPECT_TRUE(retained->hasAssuredHit());
    EXPECT_FALSE(retained->effects().back().boundCreator());
    EXPECT_EQ(kSavedEffectInvalidObjectId, retained->effects().back().creatorId);
    EXPECT_FALSE(retained->effects().back().boundObjectParameter(0));
    EXPECT_EQ(
        kSavedEffectInvalidObjectId,
        retained->effects().back().objectParameters[0]);
    EXPECT_EQ(retained, game.party().rosterCreature({RosterKind::Npc, 0}));

    const auto &creatures = area->getObjectsByType(ObjectType::Creature);
    EXPECT_EQ(creatures.end(), std::find(creatures.begin(), creatures.end(), retained));
    EXPECT_NE(creatures.end(), std::find(creatures.begin(), creatures.end(), outgoing));

    area->destroyObject(*outgoing);
    area->update(0.0f);
    EXPECT_FALSE(game.getObjectById(outgoing->id()));
    outgoing.reset();
    beamEffect.reset();
    EXPECT_TRUE(outgoingEffectTarget.expired());
}

TEST(RuntimeObjectIntegrity, creature_inventory_and_equipment_replace_as_one_graph) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeAppearances()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(
        static_cast<MockPortraits &>(engine.services().game.portraits),
        getTextureByAppearance(_))
        .Times(AnyNumber());
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto creature = game.newCreature();
    auto initial = resource::Gff::Builder()
                       .field(resource::Gff::Field::newList(
                           "ItemList", {runtimeItem("old_inventory", 810)}))
                       .field(resource::Gff::Field::newList(
                           "Equip_ItemList",
                           {runtimeItem("old_equipment", 811, 1u << InventorySlots::body)}))
                       .build();
    creature->deserialize(
        *initial, SerializedIdentityContext::moduleGraph("replacement"));
    auto oldInventory = creature->items().front();
    auto oldEquipment = creature->getEquippedItem(InventorySlots::body);
    ASSERT_TRUE(oldEquipment);
    const size_t registrySize = TestGameModule::objectRegistrySize(game);

    auto saved = resource::Gff::Builder()
                     .field(resource::Gff::Field::newList(
                         "ItemList", {runtimeItem("new_inventory", 810)}))
                     .field(resource::Gff::Field::newList(
                         "Equip_ItemList",
                         {runtimeItem("new_equipment", 811, 1u << InventorySlots::body)}))
                     .build();
    creature->deserialize(
        *saved, SerializedIdentityContext::moduleGraph("replacement"));

    EXPECT_FALSE(game.getObjectById(oldInventory->id()));
    EXPECT_FALSE(game.getObjectById(oldEquipment->id()));
    ASSERT_EQ(1u, creature->items().size());
    ASSERT_TRUE(creature->getEquippedItem(InventorySlots::body));
    EXPECT_EQ("new_inventory", creature->items().front()->tag());
    EXPECT_EQ("new_equipment",
              creature->getEquippedItem(InventorySlots::body)->tag());
    EXPECT_EQ(creature->items().front(), game.getObjectBySavedId(810));
    EXPECT_EQ(creature->getEquippedItem(InventorySlots::body),
              game.getObjectBySavedId(811));
    EXPECT_EQ(registrySize, TestGameModule::objectRegistrySize(game));
}

TEST(RuntimeObjectIntegrity, savewide_party_inventory_replacement_retires_old_items) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setActualPlayer(player);
    auto oldItem = game.newItem();
    player->addItem(oldItem);
    auto inventory = resource::Gff::Builder()
                         .field(resource::Gff::Field::newList(
                             "ItemList", {runtimeItem("shared_new")}))
                         .build();

    TestGameModule::deserializeInventory(game, *inventory);

    EXPECT_FALSE(game.getObjectById(oldItem->id()));
    ASSERT_EQ(1u, player->items().size());
    EXPECT_EQ(player->items().front(),
              game.getObjectById(player->items().front()->id()));
    ASSERT_TRUE(player->items().front()->saveRecordProvenance());
    EXPECT_EQ(SaveRecordOriginKind::PartyInventoryItem,
              player->items().front()->saveRecordProvenance()->origin.kind);
}

TEST(RuntimeObjectIntegrity, failed_graph_replacement_preserves_old_graph_and_cursor) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto creature = game.newCreature();
    auto oldEquipment = game.newItem();
    oldEquipment->deserialize(
        *runtimeItem("old_equipment"),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(creature->equip(InventorySlots::body, oldEquipment));
    const size_t registrySize = TestGameModule::objectRegistrySize(game);
    const uint32_t nextId = TestGameModule::nextObjectId(game);

    auto saved = resource::Gff::Builder()
                     .field(resource::Gff::Field::newList(
                         "Equip_ItemList",
                         {runtimeItem("candidate_a", 820, 1u << InventorySlots::body),
                          runtimeItem("candidate_b", 821, 1u << InventorySlots::body)}))
                     .build();
    EXPECT_THROW(
        creature->deserialize(
            *saved, SerializedIdentityContext::moduleGraph("failure")),
        ValidationException);

    EXPECT_EQ(oldEquipment, creature->getEquippedItem(InventorySlots::body));
    EXPECT_EQ(oldEquipment, game.getObjectById(oldEquipment->id()));
    EXPECT_FALSE(game.getObjectBySavedId(820));
    EXPECT_FALSE(game.getObjectBySavedId(821));
    EXPECT_EQ(registrySize, TestGameModule::objectRegistrySize(game));
    EXPECT_EQ(nextId, TestGameModule::nextObjectId(game));
}

TEST(RuntimeObjectIntegrity, store_and_placeable_replace_owned_items_immediately) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("placeables"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimePlaceables()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto store = game.newStore();
    auto oldStoreItem = game.newItem();
    store->addItem(oldStoreItem);
    auto storeSaved = resource::Gff::Builder()
                          .field(resource::Gff::Field::newList(
                              "ItemList", {runtimeItem("store_new")}))
                          .build();
    store->deserialize(
        *storeSaved, SerializedIdentityContext::detachedRecord("store"));
    EXPECT_FALSE(game.getObjectById(oldStoreItem->id()));
    ASSERT_EQ(1u, store->items().size());
    EXPECT_EQ(store->items().front(),
              game.getObjectById(store->items().front()->id()));

    auto placeable = game.newPlaceable();
    auto oldPlaceableItem = game.newItem();
    placeable->addItem(oldPlaceableItem);
    auto placeableSaved = resource::Gff::Builder()
                              .field(resource::Gff::Field::newDword("Appearance", 0))
                              .field(resource::Gff::Field::newList(
                                  "ItemList", {runtimeItem("placeable_new")}))
                              .build();
    placeable->deserialize(
        *placeableSaved,
        SerializedIdentityContext::detachedRecord("placeable"));
    EXPECT_FALSE(game.getObjectById(oldPlaceableItem->id()));
    ASSERT_EQ(1u, placeable->items().size());
    EXPECT_EQ(placeable->items().front(),
              game.getObjectById(placeable->items().front()->id()));
}

TEST(RuntimeObjectIntegrity, area_destruction_ends_registry_and_saved_visibility) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto saved = resource::Gff::Builder()
                     .field(resource::Gff::Field::newDword("ObjectId", 830))
                     .build();
    auto object = game.newCreature(
        *saved, SerializedIdentityContext::moduleGraph("destroy"));
    area->add(object);
    const uint32_t runtimeId = object->id();

    area->destroyObject(*object);
    area->update(0.0f);

    EXPECT_FALSE(game.getObjectById(runtimeId));
    EXPECT_FALSE(game.getObjectBySavedId(830));
    EXPECT_FALSE(game.isRuntimeObjectLive(*object));
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Creature).empty());
    game.destroyRuntimeObjectGraph(object);
    EXPECT_FALSE(game.getObjectById(runtimeId));
}

TEST(RuntimeObjectIntegrity, stale_finalization_cannot_erase_a_newer_id_owner) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto stale = game.newItem();
    game.destroyRuntimeObjectGraph(stale);
    auto replacement = game.newItem();
    TestGameModule::setSnapshotObjectId(*stale, replacement->id());

    game.destroyRuntimeObjectGraph(stale);

    EXPECT_EQ(replacement, game.getObjectById(replacement->id()));
    EXPECT_TRUE(game.isRuntimeObjectLive(*replacement));
}

TEST(RuntimeObjectIntegrity, area_retirement_preserves_session_object_liveness) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto retained = game.newCreature();
    game.party().addMember(kNpcPlayer, retained);
    game.party().setPlayer(retained);
    game.party().setActualPlayer(retained);
    area->add(retained);

    area->retireCreatureRuntime(retained);

    EXPECT_EQ(retained, game.getObjectById(retained->id()));
    EXPECT_TRUE(game.isRuntimeObjectLive(*retained));
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Creature).empty());
}

TEST(AreaRuntimeRetirement, unload_party_includes_inactive_roster_and_puppets) {
    for (auto gameId : {resource::GameID::KotOR, resource::GameID::TSL}) {
        TestEngine engine;
        engine.init();
        NiceMock<scene::MockSceneGraph> sceneGraph;
        configureRuntimeMocks(engine, sceneGraph);
        StubConsole console;
        Game game(gameId, "", engine.options(), engine.services(), console);
        auto area = game.newArea();

        auto pc = game.newCreature();
        game.party().addMember(kNpcPlayer, pc);
        game.party().setPlayer(pc);
        game.party().setActualPlayer(pc);
        area->add(pc);

        auto inactive = game.newCreature();
        game.party().addAvailableMember(0, inactive);
        area->add(inactive);

        std::shared_ptr<Creature> puppet;
        if (gameId == resource::GameID::TSL) {
            puppet = game.newCreature();
            game.party().addAvailablePuppet(0, puppet);
            area->add(puppet);
        }

        area->unloadParty();

        const auto &creatures = area->getObjectsByType(ObjectType::Creature);
        EXPECT_EQ(creatures.end(), std::find(creatures.begin(), creatures.end(), pc));
        EXPECT_EQ(creatures.end(), std::find(creatures.begin(), creatures.end(), inactive));
        if (puppet) {
            EXPECT_EQ(creatures.end(), std::find(creatures.begin(), creatures.end(), puppet));
            EXPECT_EQ(puppet, game.party().rosterCreature({RosterKind::Puppet, 0}));
        }
        EXPECT_EQ(inactive, game.party().rosterCreature({RosterKind::Npc, 0}));
    }
}

TEST(PartyTransferSpawn, initial_save_restore_places_the_party_without_spawning_it) {
    for (auto gameId : {resource::GameID::KotOR, resource::GameID::TSL}) {
        PartyTransferHarness harness(gameId);
        glm::vec3 archived(11.3286257f, -40.3899155f, 0.0f);
        harness.player->setPosition(archived);
        harness.player->setFacing(0.75f);
        harness.companion->setPosition(glm::vec3(9.0f, -41.0f, 0.0f));

        harness.area = harness.game->newArea();
        harness.area->loadParty(glm::vec3(-6.8f, -26.7f, 0.0f), 1.5f, true);

        EXPECT_EQ(archived, harness.player->position());
        EXPECT_FLOAT_EQ(0.75f, harness.player->getFacing());
        EXPECT_EQ(&harness.room, harness.player->room());
        EXPECT_EQ(0, spawnRuns("pc_spawn"));
        EXPECT_EQ(0, spawnRuns("npc_spawn"));
        EXPECT_FALSE(harness.player->spawnScriptFired());

        // The restored session then plays on. A save records the party as
        // already spawned, so the first ordinary transition out of the restored
        // module is still a transfer rather than a creation.
        TestGameModule::markSpawnScriptFired(*harness.player);
        TestGameModule::markSpawnScriptFired(*harness.companion);
        harness.transitionTo(glm::vec3(4.0f, 4.0f, 0.0f), 0.5f);

        EXPECT_EQ(0, spawnRuns("pc_spawn"));
        EXPECT_EQ(0, spawnRuns("npc_spawn"));
    }
}

TEST(PartyTransferSpawn, session_replacement_does_not_leak_the_old_party) {
    for (auto gameId : {resource::GameID::KotOR, resource::GameID::TSL}) {
        PartyTransferHarness harness(gameId);
        harness.transitionTo(glm::vec3(0.0f, 0.0f, 0.0f), 0.0f);
        ASSERT_EQ(1, spawnRuns("npc_spawn"));
        auto retiredCompanion = harness.companion;
        auto retiredId = retiredCompanion->id();

        EXPECT_CALL(harness.engine.audioModule().mixer(), stopAll()).Times(1);
        EXPECT_CALL(harness.sceneGraph, clear()).Times(AnyNumber());
        harness.game->retireRuntimeSession();

        EXPECT_FALSE(harness.game->getObjectById(retiredId));
        EXPECT_TRUE(harness.game->party().members().empty());

        // The replacement session builds its own party creatures, and those are
        // genuinely new: they spawn.
        auto player = harness.game->newCreature();
        harness.game->party().addMember(kNpcPlayer, player);
        harness.game->party().setPlayer(player);
        harness.game->party().setActualPlayer(player);
        auto companion = harness.game->newCreature();
        companion->setOnSpawn("npc_spawn");
        harness.game->party().addAvailableMember(0, companion);
        harness.game->party().addMember(0, companion);
        EXPECT_NE(retiredCompanion.get(), companion.get());

        harness.area = harness.game->newArea();
        harness.area->loadParty(glm::vec3(0.0f), 0.0f, false);

        EXPECT_EQ(2, spawnRuns("npc_spawn"));
        EXPECT_TRUE(companion->spawnScriptFired());
        // Ids restart with the session, so the retired companion is gone even
        // where its id has since been handed to a new object.
        EXPECT_NE(retiredCompanion, harness.game->getObjectById(retiredId));
    }
}
