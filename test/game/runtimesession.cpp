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
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/gui/hud.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/room.h"
#include "reone/scene/collision.h"

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

    EXPECT_CALL(engine.resourceModule().director(), onGameLoad(_)).Times(0);
    EXPECT_CALL(engine.resourceModule().director(), onModuleLoad(_)).Times(0);
    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(1);
    game.retireRuntimeSession();

    EXPECT_TRUE(game.getGlobalBoolean("BOOL"));
    EXPECT_EQ(42, game.getGlobalNumber("NUMBER"));
    EXPECT_EQ("committed", game.getGlobalString("STRING"));
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
    game.openInGame();

    game.scheduleModuleTransition("module_b", "entry_b");

    EXPECT_TRUE(game.hasPlayableRuntimeSession());
    EXPECT_EQ(object, game.getObjectById(object->id()));
    EXPECT_EQ(9, game.getGlobalNumber("SESSION"));
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
    Party::PersistedState partyState;
    partyState.pcName = "old";
    game.party().setPersistedState(partyState);
    game.journal().restoreEntry("old_plot", 30, 1, 2);
    game.newCreature();

    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(1);
    game.resetGame();

    EXPECT_FALSE(game.getGlobalBoolean("BOOL"));
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

    sourceArea->add(player);
    player->setRoom(&sourceRoom);
    ASSERT_EQ(&sourceRoom, player->room());
    ASSERT_TRUE(sourceRoom.tenants().count(player.get()));

    sourceArea->unloadParty();

    EXPECT_EQ(nullptr, player->room());
    EXPECT_FALSE(sourceRoom.tenants().count(player.get()));
    EXPECT_EQ(player, game.party().player());
    EXPECT_EQ(17, player->currentHitPoints());
    ASSERT_EQ(1, player->effects().size());
    EXPECT_EQ(effect, player->effects().front().effect);
    ASSERT_EQ(1, player->actions().size());
    EXPECT_EQ(action, player->actions().front());
    EXPECT_EQ(0, actionExecutions);

    auto destinationArea = game.newArea();
    glm::vec3 entry(9.085618f, -42.946671f, 0.0f);
    destinationArea->loadParty(entry, 0.5f, false);

    EXPECT_EQ(entry, player->position());
    EXPECT_FLOAT_EQ(0.5f, player->getFacing());
    EXPECT_EQ(&destinationRoom, player->room());
    EXPECT_TRUE(destinationRoom.tenants().count(player.get()));
    EXPECT_EQ(player, game.party().player());
    EXPECT_EQ(player, game.getObjectById(player->id()));
    EXPECT_EQ(17, player->currentHitPoints());
    EXPECT_EQ(1, player->effects().size());
    EXPECT_EQ(1, player->actions().size());
    EXPECT_EQ(0, actionExecutions);
    auto &destinationCreatures = destinationArea->getObjectsByType(ObjectType::Creature);
    EXPECT_NE(destinationCreatures.end(),
              std::find(destinationCreatures.begin(), destinationCreatures.end(), player));
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
