/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <algorithm>
#include <set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action.h"
#include "reone/game/action/castfakespellatobject.h"
#include "reone/game/action/equipitem.h"
#include "reone/game/effect.h"
#include "reone/game/effect/assuredhit.h"
#include "reone/game/effect/beam.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/modifyattacks.h"
#include "reone/game/event.h"
#include "reone/game/equipmentrules.h"
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
#include "reone/game/script/routines.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"
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

class TargetCountingAction : public reone::game::Action {
public:
    TargetCountingAction(
        Game &game,
        ServicesView &services,
        std::shared_ptr<Object> target,
        int &executions) :
        reone::game::Action(game, services, ActionType::Invalid),
        _target(std::move(target)),
        _executions(executions) {
        requireRuntimeObject(_target);
    }

    void execute(
        std::shared_ptr<reone::game::Action>,
        Object &,
        float) override {
        ++_executions;
        complete();
    }

private:
    // Deliberately retain storage to prove strong ownership cannot confer
    // gameplay liveness after semantic destruction.
    std::shared_ptr<Object> _target;
    int &_executions;
};

class SessionConversation : public Conversation {
public:
    SessionConversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

    void bindOwner(std::shared_ptr<Object> owner) {
        _owner = owner;
        _dialog = std::make_shared<resource::Dialog>();
        owner->setIsInConversation(true);
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

std::shared_ptr<resource::TwoDA> runtimeBaseItems(int maxStack = 99) {
    resource::TwoDA::Builder builder;
    builder.columns(
        {"equipableslots", "itemclass", "ammunitiontype", "maxstack"});
    builder.row({"2", "i_test", "0", std::to_string(maxStack)});
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
    uint32_t structType = 0,
    uint16_t stackSize = 1,
    uint8_t charges = 0) {
    resource::Gff::Builder builder;
    builder.type(structType)
        .field(resource::Gff::Field::newCExoString("Tag", std::move(tag)))
        .field(resource::Gff::Field::newInt("BaseItem", 0))
        .field(resource::Gff::Field::newWord("StackSize", stackSize))
        .field(resource::Gff::Field::newByte("Charges", charges));
    if (savedId) {
        builder.field(resource::Gff::Field::newDword("ObjectId", *savedId));
    }
    return builder.build();
}

std::shared_ptr<resource::Gff> runtimeItemProperty(
    ItemProperty type,
    uint16_t subtype = 0,
    uint8_t costTable = 0,
    uint16_t costValue = 0) {
    return resource::Gff::Builder()
        .field(resource::Gff::Field::newWord(
            "PropertyName", static_cast<uint16_t>(type)))
        .field(resource::Gff::Field::newWord("Subtype", subtype))
        .field(resource::Gff::Field::newByte("CostTable", costTable))
        .field(resource::Gff::Field::newWord("CostValue", costValue))
        .field(resource::Gff::Field::newByte("UpgradeType", 0))
        .build();
}

std::shared_ptr<resource::Gff> runtimeEquippedItem(
    std::string tag,
    std::vector<std::shared_ptr<resource::Gff>> properties,
    std::optional<uint32_t> savedId = std::nullopt) {
    resource::Gff::Builder builder;
    builder.type(1u << InventorySlots::body)
        .field(resource::Gff::Field::newCExoString("Tag", std::move(tag)))
        .field(resource::Gff::Field::newInt("BaseItem", 0))
        .field(resource::Gff::Field::newWord("StackSize", 1))
        .field(resource::Gff::Field::newByte("Charges", 0))
        .field(resource::Gff::Field::newList(
            "PropertiesList", std::move(properties)));
    if (savedId) {
        builder.field(resource::Gff::Field::newDword("ObjectId", *savedId));
    }
    return builder.build();
}

std::shared_ptr<resource::TwoDA> oneValueTable(
    std::string column,
    int value) {
    resource::TwoDA::Builder builder;
    builder.columns({std::move(column)});
    builder.row({std::to_string(value)});
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

void configureEquipmentEffectTables(TestEngine &engine) {
    resource::TwoDA::Builder costTables;
    costTables.columns({"name"}).row({"iprp_test"});
    auto costTable = std::shared_ptr<resource::TwoDA>(costTables.build());
    resource::TwoDA::Builder damageTypes;
    damageTypes.columns({"label"}).row({"bludgeoning"});
    auto damageTypeTable =
        std::shared_ptr<resource::TwoDA>(damageTypes.build());
    resource::TwoDA::Builder protection;
    protection.columns({"label"}).row({"normal"});
    auto protectionTable =
        std::shared_ptr<resource::TwoDA>(protection.build());

    EXPECT_CALL(engine.resourceModule().twoDas(), get("iprp_costtable"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(costTable));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("iprp_test"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(oneValueTable("value", 4)));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("iprp_resistcost"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(oneValueTable("amount", 5)));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("iprp_soakcost"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(oneValueTable("amount", 6)));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("iprp_damagetype"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(damageTypeTable));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("iprp_protection"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(protectionTable));
}

void giveItemThroughRoutine(
    TestEngine &engine,
    Game &game,
    const std::shared_ptr<Item> &item,
    const std::shared_ptr<Object> &receiver) {
    Routines routines(game.gameId(), &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;
    execution.routines = &routines;
    routines.get(routines.getIndexByName("GiveItem"))
        .invoke(
            {script::Variable::ofObject(item->id()),
             script::Variable::ofObject(receiver->id())},
            execution);
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

std::shared_ptr<Item> reone::game::TestGameModule::newItemAtRuntimeId(
    Game &game, uint32_t id) {
    return game.newObjectAtId<Item>(id, false, game, game._services);
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
    auto initialTemplate = resolveModuleLoadContext(true, false);
    auto initialRestore = resolveModuleLoadContext(true, true);
    auto savedTransition = resolveModuleLoadContext(false, true);

    EXPECT_EQ(ModuleLoadContext::FreshModule, fresh);
    EXPECT_FALSE(restoresSavedWorld(fresh));
    EXPECT_FALSE(restoresSavedSession(fresh));
    EXPECT_FALSE(preservesSavedPlacement(fresh));

    EXPECT_EQ(ModuleLoadContext::InitialTemplateRestore, initialTemplate);
    EXPECT_FALSE(restoresSavedWorld(initialTemplate));
    EXPECT_TRUE(restoresSavedSession(initialTemplate));
    EXPECT_FALSE(preservesSavedPlacement(initialTemplate));

    EXPECT_EQ(ModuleLoadContext::InitialSaveRestore, initialRestore);
    EXPECT_TRUE(restoresSavedWorld(initialRestore));
    EXPECT_TRUE(restoresSavedSession(initialRestore));
    EXPECT_TRUE(preservesSavedPlacement(initialRestore));

    EXPECT_EQ(ModuleLoadContext::SavedModuleTransition, savedTransition);
    EXPECT_TRUE(restoresSavedWorld(savedTransition));
    EXPECT_FALSE(restoresSavedSession(savedTransition));
    EXPECT_FALSE(preservesSavedPlacement(savedTransition));
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

    sourceArea->retirePartyAreaRuntime();

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
            area->retirePartyAreaRuntime();
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

TEST(PartyTransferSpawn, k2_active_puppet_remains_single_across_transitions) {
    PartyTransferHarness harness(resource::GameID::TSL);
    auto puppet = harness.game->newCreature();
    puppet->setTag("remote");
    ASSERT_TRUE(harness.game->party().addAvailablePuppet(0, puppet));
    ASSERT_TRUE(harness.game->party().addPuppet(0, puppet));
    const auto puppetId = puppet->id();
    auto *puppetObject = puppet.get();

    for (int cycle = 0; cycle < 10; ++cycle) {
        SCOPED_TRACE(cycle);
        harness.transitionTo(
            glm::vec3(static_cast<float>(cycle), 2.0f, 0.0f),
            static_cast<float>(cycle) * 0.1f);

        EXPECT_TRUE(harness.game->party().isPuppet(0));
        EXPECT_EQ(
            puppet,
            harness.game->party().rosterCreature({RosterKind::Puppet, 0}));
        EXPECT_EQ(puppetObject, puppet.get());
        EXPECT_EQ(puppetId, puppet->id());
        EXPECT_EQ(puppet, harness.game->getObjectById(puppetId));

        const auto &creatures =
            harness.area->getObjectsByType(ObjectType::Creature);
        EXPECT_EQ(1, std::count(creatures.begin(), creatures.end(), puppet));
        EXPECT_EQ(
            1,
            std::count_if(
                creatures.begin(), creatures.end(), [](const auto &creature) {
                    return creature && creature->tag() == "remote";
                }));
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

    area->retireCreatureAreaRuntime(retained);

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

TEST(RuntimeObjectIntegrity, savewide_party_inventory_coalesces_records_without_object_ids) {
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
    auto inventory = resource::Gff::Builder()
                         .field(resource::Gff::Field::newList(
                             "ItemList",
                             {runtimeItem("computer_spike", std::nullopt, 0, 2),
                              runtimeItem("computer_spike", std::nullopt, 0, 3)}))
                         .build();

    TestGameModule::deserializeInventory(game, *inventory);

    ASSERT_EQ(1u, player->items().size());
    EXPECT_EQ(5, player->items().front()->stackSize());
    EXPECT_EQ(player->id(), player->items().front()->owner());
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
        *runtimeEquippedItem(
            "old_equipment",
            {runtimeItemProperty(
                ItemProperty::BonusFeat,
                static_cast<uint16_t>(FeatType::Toughness))}),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(creature->equip(InventorySlots::body, oldEquipment));
    ASSERT_TRUE(creature->hasEffectiveFeat(FeatType::Toughness));
    ASSERT_EQ(1u, creature->effects().size());
    const EffectId oldEffectId = creature->effects().front().id;
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
    ASSERT_TRUE(creature->hasEffectiveFeat(FeatType::Toughness));
    ASSERT_EQ(1u, creature->effects().size());
    EXPECT_EQ(oldEffectId, creature->effects().front().id);
    EXPECT_EQ(oldEquipment, creature->effects().front().boundCreator());
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

TEST(RuntimeObjectIntegrity, staged_template_container_coalesces_item_records) {
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
    auto placeable = game.newPlaceable();
    const size_t registrySize = TestGameModule::objectRegistrySize(game);
    auto record = resource::Gff::Builder()
                      .field(resource::Gff::Field::newDword("Appearance", 0))
                      .field(resource::Gff::Field::newList(
                          "ItemList",
                          {runtimeItem("computer_spike"),
                           runtimeItem("computer_spike"),
                           runtimeItem("computer_spike"),
                           runtimeItem("computer_spike"),
                           runtimeItem("computer_spike")}))
                      .build();

    placeable->deserialize(
        *record,
        SerializedIdentityContext::templateResource("endar_footlocker"));

    ASSERT_EQ(1u, placeable->items().size());
    EXPECT_EQ(5, placeable->items().front()->stackSize());
    EXPECT_EQ(placeable->id(), placeable->items().front()->owner());
    EXPECT_TRUE(placeable->items().front()->isRuntimeLive());
    EXPECT_EQ(registrySize + 1, TestGameModule::objectRegistrySize(game));
}

TEST(RuntimeObjectIntegrity, staged_creature_and_detached_inventory_coalesce) {
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
    auto record = resource::Gff::Builder()
                      .field(resource::Gff::Field::newList(
                          "ItemList",
                          {runtimeItem("repair_part", std::nullopt, 0, 2),
                           runtimeItem("repair_part", std::nullopt, 0, 3)}))
                      .build();

    creature->deserialize(
        *record,
        SerializedIdentityContext::detachedRecord("availnpc0.utc"));

    ASSERT_EQ(1u, creature->items().size());
    EXPECT_EQ(5, creature->items().front()->stackSize());
    ASSERT_TRUE(creature->items().front()->saveRecordProvenance());
    EXPECT_FALSE(creature->items().front()->serializedObjectIdentity());
}

TEST(RuntimeObjectIntegrity, staged_stack_coalescing_obeys_retail_maximum) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems(10)));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("placeables"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimePlaceables()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto load = [&](std::vector<std::shared_ptr<resource::Gff>> items) {
        auto placeable = game.newPlaceable();
        auto record = resource::Gff::Builder()
                          .field(resource::Gff::Field::newDword("Appearance", 0))
                          .field(resource::Gff::Field::newList(
                              "ItemList", std::move(items)))
                          .build();
        placeable->deserialize(
            *record,
            SerializedIdentityContext::templateResource("max_stack"));
        return placeable;
    };

    auto below = load(
        {runtimeItem("parts", std::nullopt, 0, 4),
         runtimeItem("parts", std::nullopt, 0, 5)});
    ASSERT_EQ(1u, below->items().size());
    EXPECT_EQ(9, below->items()[0]->stackSize());

    auto exact = load(
        {runtimeItem("parts", std::nullopt, 0, 4),
         runtimeItem("parts", std::nullopt, 0, 6)});
    ASSERT_EQ(1u, exact->items().size());
    EXPECT_EQ(10, exact->items()[0]->stackSize());

    auto above = load(
        {runtimeItem("parts", std::nullopt, 0, 6),
         runtimeItem("parts", std::nullopt, 0, 6),
         runtimeItem("parts", std::nullopt, 0, 6)});
    ASSERT_EQ(2u, above->items().size());
    EXPECT_EQ(10, above->items()[0]->stackSize());
    EXPECT_EQ(8, above->items()[1]->stackSize());

    auto several = load(
        {runtimeItem("parts", std::nullopt, 0, 6),
         runtimeItem("parts", std::nullopt, 0, 6),
         runtimeItem("parts", std::nullopt, 0, 6),
         runtimeItem("parts", std::nullopt, 0, 6),
         runtimeItem("parts", std::nullopt, 0, 6),
         runtimeItem("parts", std::nullopt, 0, 1)});
    ASSERT_EQ(4u, several->items().size());
    EXPECT_EQ(10, several->items()[0]->stackSize());
    EXPECT_EQ(10, several->items()[1]->stackSize());
    EXPECT_EQ(10, several->items()[2]->stackSize());
    EXPECT_EQ(1, several->items()[3]->stackSize());
}

TEST(RuntimeObjectIntegrity, staged_authoritative_item_identities_remain_distinct) {
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
    auto placeable = game.newPlaceable();
    auto record = resource::Gff::Builder()
                      .field(resource::Gff::Field::newDword("Appearance", 0))
                      .field(resource::Gff::Field::newList(
                          "ItemList",
                          {runtimeItem("saved_stack", 870),
                           runtimeItem("saved_stack", 871)}))
                      .build();

    placeable->deserialize(
        *record, SerializedIdentityContext::moduleGraph("saved_inventory"));

    ASSERT_EQ(2u, placeable->items().size());
    EXPECT_EQ(placeable->items()[0], game.getObjectBySavedId(870));
    EXPECT_EQ(placeable->items()[1], game.getObjectBySavedId(871));
    EXPECT_NE(placeable->items()[0], placeable->items()[1]);

    auto detached = game.newPlaceable();
    auto detachedRecord = resource::Gff::Builder()
                              .field(resource::Gff::Field::newDword("Appearance", 0))
                              .field(resource::Gff::Field::newList(
                                  "ItemList",
                                  {runtimeItem("detached_stack", 880),
                                   runtimeItem("detached_stack", 881)}))
                              .build();
    const auto detachedContext =
        SerializedIdentityContext::detachedRecord("inventory.res");

    detached->deserialize(*detachedRecord, detachedContext);

    ASSERT_EQ(2u, detached->items().size());
    EXPECT_EQ(
        detached->items()[0]->serializedObjectIdentity(),
        std::optional<SerializedObjectIdentity>({detachedContext, 880}));
    EXPECT_EQ(
        detached->items()[1]->serializedObjectIdentity(),
        std::optional<SerializedObjectIdentity>({detachedContext, 881}));
    EXPECT_FALSE(game.getObjectBySavedId(880));
    EXPECT_FALSE(game.getObjectBySavedId(881));
}

TEST(RuntimeObjectIntegrity, staged_stack_compatibility_preserves_distinct_state_and_equipment) {
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
    auto record = resource::Gff::Builder()
                      .field(resource::Gff::Field::newList(
                          "ItemList",
                          {runtimeItem("same_tag", std::nullopt, 0, 1, 0),
                           runtimeItem("same_tag", std::nullopt, 0, 1, 1)}))
                      .field(resource::Gff::Field::newList(
                          "Equip_ItemList",
                          {runtimeItem(
                              "same_tag",
                              std::nullopt,
                              1u << InventorySlots::body)}))
                      .build();

    creature->deserialize(
        *record, SerializedIdentityContext::templateResource("creature_items"));

    ASSERT_EQ(2u, creature->items().size());
    ASSERT_TRUE(creature->getEquippedItem(InventorySlots::body));
    EXPECT_TRUE(creature->getEquippedItem(InventorySlots::body)->isEquipped());
    EXPECT_EQ(creature->id(), creature->getEquippedItem(InventorySlots::body)->owner());
}

TEST(RuntimeObjectIntegrity, area_destruction_ends_registry_and_saved_visibility) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeAppearances()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(
        static_cast<MockPortraits &>(engine.services().game.portraits),
        getTextureByAppearance(_))
        .Times(AnyNumber());
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
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto retained = game.newCreature();
    game.party().addMember(kNpcPlayer, retained);
    game.party().setPlayer(retained);
    game.party().setActualPlayer(retained);
    auto equipment = game.newItem();
    equipment->deserialize(
        *runtimeEquippedItem(
            "retained_equipment",
            {runtimeItemProperty(
                ItemProperty::BonusFeat,
                static_cast<uint16_t>(FeatType::Toughness))}),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(retained->equip(InventorySlots::body, equipment));
    area->add(retained);

    area->retireCreatureAreaRuntime(retained);

    EXPECT_EQ(retained, game.getObjectById(retained->id()));
    EXPECT_TRUE(game.isRuntimeObjectLive(*retained));
    EXPECT_TRUE(game.isRuntimeObjectLive(*equipment));
    EXPECT_TRUE(retained->hasEffectiveFeat(FeatType::Toughness));
    ASSERT_EQ(1u, retained->effects().size());
    EXPECT_EQ(equipment, retained->effects().front().boundCreator());
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Creature).empty());
}

TEST(RuntimeObjectLiveness, destroyed_strong_action_target_cannot_execute) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto actor = game.newCreature();
    auto target = game.newCreature();
    int executions = 0;
    auto action = game.newAction<TargetCountingAction>(target, executions);
    actor->addAction(action);

    game.destroyRuntimeObjectGraph(target);
    ASSERT_TRUE(target);
    EXPECT_FALSE(target->isRuntimeLive());
    actor->update(0.0f);

    EXPECT_EQ(0, executions);
    EXPECT_TRUE(action->isCompleted());
}

TEST(RuntimeObjectLiveness, moved_action_target_still_tracks_exact_liveness) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    auto action = game.newAction<CastFakeSpellAtObjectAction>(
        SpellType::AffectMind, target, ProjectilePathType::Default);

    ASSERT_TRUE(action->runtimeDependenciesLive());
    game.destroyRuntimeObjectGraph(target);

    EXPECT_FALSE(action->runtimeDependenciesLive());
}

TEST(RuntimeObjectLiveness, stale_reference_does_not_alias_reused_session_id) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto first = game.newItem();
    RuntimeObjectRef<Item> stale(first);
    const uint32_t reusedId = first->id();
    const uint64_t incarnation = first->runtimeIncarnation();

    game.retireRuntimeSession();
    auto second = game.newItem();

    ASSERT_EQ(reusedId, second->id());
    EXPECT_NE(incarnation, second->runtimeIncarnation());
    EXPECT_FALSE(stale.resolve());
    EXPECT_EQ(second, game.getObjectById(reusedId));
}

TEST(RuntimeObjectLiveness, published_runtime_id_is_not_reused_within_session) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto observer = game.newCreature();
    auto hostile = game.newCreature();
    const uint32_t hostileId = hostile->id();
    observer->setLastHostileActor(hostileId);

    ASSERT_EQ(hostileId, observer->getLastHostileActor());
    game.destroyRuntimeObjectGraph(hostile);

    EXPECT_EQ(script::kObjectInvalid, observer->getLastHostileActor());
    EXPECT_THROW(
        TestGameModule::newItemAtRuntimeId(game, hostileId),
        ValidationException);

    auto ifo = resource::Gff::Builder()
                   .field(resource::Gff::Field::newDword(
                       "Mod_NextObjId0", hostileId))
                   .build();
    game.prepareSavedRuntimeNamespace(
        *ifo, SerializedIdentityContext::moduleGraph("reuse_guard"));
    auto replacement = game.newItem();

    EXPECT_NE(hostileId, replacement->id());
    EXPECT_EQ(replacement->id(), game.getObjectById(replacement->id())->id());
}

TEST(RuntimeObjectLiveness, last_damager_query_uses_exact_live_runtime_binding) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto victim = game.newCreature();
    auto damager = game.newCreature();
    auto unrelated = game.newCreature();
    victim->setCurrentHitPoints(10);
    auto damage = game.newEffect<DamageEffect>(
        1, DamageType::Universal, DamagePower::Normal);
    damage->setSaveFacingCreator(damager);
    victim->applyEffect(damage, DurationType::Instant);
    ASSERT_EQ(damager->id(), victim->getLastDamager());

    Routines routines(resource::GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;
    execution.routines = &routines;
    execution.args.emplace_back(
        script::ArgKind::Caller,
        script::Variable::ofObject(victim->id()));
    execution.args.emplace_back(
        script::ArgKind::LastDamager,
        script::Variable::ofObject(unrelated->id()));

    EXPECT_EQ(
        damager->id(),
        routines.get(346).invoke({}, execution).objectId);

    game.destroyRuntimeObjectGraph(damager);

    EXPECT_EQ(script::kObjectInvalid, victim->getLastDamager());
    EXPECT_EQ(
        script::kObjectInvalid,
        routines.get(346).invoke({}, execution).objectId);
}

TEST(RuntimeObjectLiveness, saved_last_damager_binds_once_to_restored_incarnation) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    const auto context =
        SerializedIdentityContext::moduleGraph("last_damager");
    auto victim = game.newCreature();
    auto damager = game.newCreature();
    auto record = resource::Gff::Builder()
                      .field(resource::Gff::Field::newDword(
                          "LastDamager", 851u))
                      .build();
    victim->deserializeRuntimeState(*record, context);
    game.registerSavedObjectIdentity(851u, damager, context);

    game.resolveSavedObjectReferences();

    EXPECT_EQ(damager->id(), victim->getLastDamager());
    game.destroyRuntimeObjectGraph(damager);
    EXPECT_EQ(script::kObjectInvalid, victim->getLastDamager());
}

TEST(RuntimeObjectLiveness, effect_and_vm_event_references_fail_closed) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();

    EffectInstance effect;
    effect.creatorId = target->id();
    effect.objectParameters[0] = target->id();
    ASSERT_TRUE(game.bindEffectCreator(effect));
    ASSERT_EQ(target, effect.boundCreator());
    ASSERT_EQ(target, effect.boundObjectParameter(0));
    Event event(1, {}, {}, {}, {target});

    game.destroyRuntimeObjectGraph(target);

    EXPECT_FALSE(effect.boundCreator());
    EXPECT_FALSE(effect.boundObjectParameter(0));
    EXPECT_EQ(
        event.objects(),
        (std::vector<uint32_t> {script::kObjectInvalid}));
}

TEST(RuntimeObjectLiveness, flat_saved_reference_fails_closed_after_target_death) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    const auto context =
        SerializedIdentityContext::moduleGraph("flat_reference");
    auto sourceRecord = resource::Gff::Builder()
                            .field(resource::Gff::Field::newDword("ObjectId", 850))
                            .field(resource::Gff::Field::newInt("BaseItem", 0))
                            .field(resource::Gff::Field::newDword("CreatorId", 851))
                            .build();
    auto targetRecord = resource::Gff::Builder()
                            .field(resource::Gff::Field::newDword("ObjectId", 851))
                            .field(resource::Gff::Field::newInt("BaseItem", 0))
                            .build();
    auto source = game.newItem(*sourceRecord, context);
    auto target = game.newItem(*targetRecord, context);
    game.resolveSavedObjectReferences();
    ASSERT_EQ(target, source->savedReference("CreatorId"));

    game.destroyRuntimeObjectGraph(target);

    EXPECT_FALSE(source->savedReference("CreatorId"));
}

TEST(RuntimeObjectIntegrity, gff_factory_failure_never_publishes_candidate) {
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
    const size_t registrySize = TestGameModule::objectRegistrySize(game);
    const uint32_t nextId = TestGameModule::nextObjectId(game);
    auto invalid = resource::Gff::Builder()
                       .field(resource::Gff::Field::newDword("ObjectId", 840))
                       .field(resource::Gff::Field::newList(
                           "Equip_ItemList",
                           {runtimeItem("candidate_a", 841, 1u << InventorySlots::body),
                            runtimeItem("candidate_b", 842, 1u << InventorySlots::body)}))
                       .build();

    EXPECT_THROW(
        game.newCreature(
            *invalid,
            SerializedIdentityContext::moduleGraph("factory_failure")),
        ValidationException);

    EXPECT_EQ(registrySize, TestGameModule::objectRegistrySize(game));
    EXPECT_EQ(nextId, TestGameModule::nextObjectId(game));
    EXPECT_FALSE(game.getObjectBySavedId(840));
    EXPECT_FALSE(game.getObjectBySavedId(841));
    EXPECT_FALSE(game.getObjectBySavedId(842));
}

TEST(RuntimeObjectOwnership, stack_merge_finalizes_consumed_item) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto retained = game.newItem();
    retained->deserialize(
        *runtimeItem("stack"),
        SerializedIdentityContext::templateResource());
    auto consumed = game.newItem();
    consumed->deserialize(
        *runtimeItem("stack"),
        SerializedIdentityContext::templateResource());
    owner->addItem(retained);
    const size_t registrySize = TestGameModule::objectRegistrySize(game);

    owner->addItem(consumed);

    ASSERT_EQ(1u, owner->items().size());
    EXPECT_EQ(retained, owner->items().front());
    EXPECT_EQ(2, retained->stackSize());
    EXPECT_FALSE(consumed->isRuntimeLive());
    EXPECT_FALSE(game.getObjectById(consumed->id()));
    EXPECT_EQ(registrySize - 1, TestGameModule::objectRegistrySize(game));
}

TEST(RuntimeObjectOwnership, runtime_stack_merge_uses_the_retail_maximum) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems(10)));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto retained = game.newItem();
    retained->deserialize(
        *runtimeItem("stack", std::nullopt, 0, 8),
        SerializedIdentityContext::templateResource());
    auto remainder = game.newItem();
    remainder->deserialize(
        *runtimeItem("stack", std::nullopt, 0, 5),
        SerializedIdentityContext::templateResource());
    owner->addItem(retained);

    owner->addItem(remainder);

    ASSERT_EQ(2u, owner->items().size());
    EXPECT_EQ(10, retained->stackSize());
    EXPECT_EQ(3, remainder->stackSize());
    EXPECT_EQ(owner->id(), retained->owner());
    EXPECT_EQ(owner->id(), remainder->owner());
    EXPECT_TRUE(retained->isRuntimeLive());
    EXPECT_TRUE(remainder->isRuntimeLive());
}

TEST(RuntimeObjectOwnership, equipment_replacement_disposes_previous_item) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto first = game.newItem();
    first->deserialize(
        *runtimeItem("first"),
        SerializedIdentityContext::templateResource());
    auto replacement = game.newItem();
    replacement->deserialize(
        *runtimeItem("replacement"),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(owner->equip(InventorySlots::body, first));

    ASSERT_TRUE(owner->replaceEquipment(
        InventorySlots::body, replacement, *owner));

    EXPECT_EQ(replacement, owner->getEquippedItem(InventorySlots::body));
    EXPECT_EQ(owner->id(), replacement->owner());
    ASSERT_EQ(1u, owner->items().size());
    EXPECT_EQ(first, owner->items().front());
    EXPECT_EQ(owner->id(), first->owner());
    EXPECT_FALSE(first->isEquipped());
    EXPECT_TRUE(first->isRuntimeLive());
}

TEST(RuntimeObjectOwnership, unequip_moves_item_to_explicit_receiver) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto item = game.newItem();
    item->deserialize(
        *runtimeItem("unequip"),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(owner->equip(InventorySlots::body, item));
    EXPECT_THROW(owner->addItem(item), ValidationException);

    ASSERT_TRUE(owner->moveEquippedItemTo(item, *owner));

    EXPECT_FALSE(item->isEquipped());
    EXPECT_EQ(owner->id(), item->owner());
    EXPECT_FALSE(owner->getEquippedItem(InventorySlots::body));
    ASSERT_EQ(1u, owner->items().size());
    EXPECT_EQ(item, owner->items().front());
}

TEST(RuntimeObjectOwnership, transfers_equipped_item_between_creatures) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto source = game.newCreature();
    auto receiver = game.newCreature();
    auto item = game.newItem();
    item->deserialize(
        *runtimeItem("transfer"),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(source->equip(InventorySlots::body, item));

    ASSERT_TRUE(transferItemTo(game, item, *receiver));

    EXPECT_FALSE(source->getEquippedItem(InventorySlots::body));
    EXPECT_TRUE(source->items().empty());
    ASSERT_EQ(1u, receiver->items().size());
    EXPECT_EQ(item, receiver->items().front());
    EXPECT_EQ(receiver->id(), item->owner());
    EXPECT_FALSE(item->isEquipped());
    EXPECT_TRUE(item->isRuntimeLive());
}

TEST(RuntimeObjectOwnership, give_item_moves_created_world_item_into_inventory) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    EXPECT_CALL(
        engine.resourceModule().gffs(),
        get("g_i_medeqpmnt04", resource::ResType::Uti))
        .WillOnce(Return(runtimeItem("g_i_medeqpmnt04")));
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto pc = game.newCreature();
    ASSERT_TRUE(game.party().addMember(kNpcPlayer, pc));
    game.party().setPlayer(pc);
    game.party().setActualPlayer(pc);

    auto created = area->createObject(
        ObjectType::Item,
        "g_i_medeqpmnt04",
        game.newLocation(glm::vec3(1.0f, 2.0f, 3.0f), 0.0f));
    auto item = std::dynamic_pointer_cast<Item>(created);
    ASSERT_TRUE(item);
    item->setStackSize(3);
    ASSERT_NE(
        area->objects().end(),
        std::find(area->objects().begin(), area->objects().end(), item));

    giveItemThroughRoutine(engine, game, item, pc);

    ASSERT_EQ(1u, pc->items().size());
    EXPECT_EQ(item, pc->items().front());
    EXPECT_EQ(3, item->stackSize());
    EXPECT_EQ(pc->id(), item->owner());
    EXPECT_TRUE(item->isRuntimeLive());
    EXPECT_EQ(item, game.getObjectById(item->id()));
    EXPECT_EQ(
        area->objects().end(),
        std::find(area->objects().begin(), area->objects().end(), item));
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Item).empty());
    EXPECT_FALSE(area->getObjectByTag("g_i_medeqpmnt04"));
    EXPECT_EQ(nullptr, item->room());
}

TEST(RuntimeObjectOwnership, area_item_stack_merge_retires_only_incoming_item) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto receiver = game.newCreature();
    auto existing = game.newItem();
    existing->deserialize(
        *runtimeItem("world_stack"),
        SerializedIdentityContext::templateResource());
    existing->setStackSize(2);
    receiver->addItem(existing);
    auto incoming = game.newItem();
    incoming->deserialize(
        *runtimeItem("world_stack"),
        SerializedIdentityContext::templateResource());
    incoming->setStackSize(3);
    area->add(incoming);
    const uint32_t incomingId = incoming->id();

    ASSERT_TRUE(transferItemTo(game, incoming, *receiver));

    ASSERT_EQ(1u, receiver->items().size());
    EXPECT_EQ(existing, receiver->items().front());
    EXPECT_EQ(5, existing->stackSize());
    EXPECT_EQ(receiver->id(), existing->owner());
    EXPECT_TRUE(existing->isRuntimeLive());
    EXPECT_FALSE(incoming->isRuntimeLive());
    EXPECT_FALSE(game.getObjectById(incomingId));
    EXPECT_EQ(
        area->objects().end(),
        std::find(area->objects().begin(), area->objects().end(), incoming));
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Item).empty());
    EXPECT_FALSE(area->getObjectByTag("world_stack"));
}

TEST(RuntimeObjectOwnership, git_shaped_world_item_releases_every_area_edge) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto trigger = game.newTrigger();
    trigger->deserialize(
        *resource::Gff::Builder().build(),
        SerializedIdentityContext::templateResource());
    area->add(trigger);
    auto item = game.newItem(
        *runtimeItem("git_world"),
        SerializedIdentityContext::templateResource("test-area"));

    auto root = std::make_shared<graphics::ModelNode>(
        0, "world_item", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto model = std::make_unique<graphics::Model>(
        "world_item", 0, root,
        std::vector<std::shared_ptr<graphics::Animation>>(), "", 1.0f);
    auto sceneNode = std::make_shared<scene::ModelSceneNode>(
        *model, scene::ModelUsage::Placeable, sceneGraph,
        engine.graphicsModule().services(),
        engine.audioModule().services(),
        engine.resourceModule().services());
    TestGameModule::setAreaRuntimeSceneNode(*item, sceneNode);
    EXPECT_CALL(sceneGraph, addRoot(sceneNode)).Times(1);
    EXPECT_CALL(
        sceneGraph,
        removeRoot(A<scene::ModelSceneNode &>()))
        .WillOnce([&sceneNode](scene::ModelSceneNode &removed) {
            EXPECT_EQ(sceneNode.get(), &removed);
        });
    area->add(item);
    Room room("world_room", glm::vec3(0.0f), nullptr, nullptr, nullptr);
    item->setRoom(&room);
    trigger->addTenant(item);
    auto receiver = game.newCreature();

    ASSERT_TRUE(transferItemTo(game, item, *receiver));

    ASSERT_EQ(1u, receiver->items().size());
    EXPECT_EQ(item, receiver->items().front());
    EXPECT_EQ(receiver->id(), item->owner());
    EXPECT_TRUE(item->isRuntimeLive());
    EXPECT_EQ(nullptr, item->room());
    EXPECT_FALSE(trigger->isTenant(item));
    EXPECT_EQ(
        area->objects().end(),
        std::find(area->objects().begin(), area->objects().end(), item));
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Item).empty());
    EXPECT_FALSE(area->getObjectByTag("git_world"));
}

TEST(RuntimeObjectOwnership, ownerless_nonresident_item_is_not_transferable) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto item = game.newItem();
    item->deserialize(
        *runtimeItem("unowned"),
        SerializedIdentityContext::templateResource());
    auto receiver = game.newCreature();

    EXPECT_FALSE(transferItemTo(game, item, *receiver));

    EXPECT_TRUE(receiver->items().empty());
    EXPECT_EQ(0u, item->owner());
    EXPECT_TRUE(item->isRuntimeLive());
    EXPECT_EQ(item, game.getObjectById(item->id()));
}

TEST(RuntimeObjectOwnership, equip_action_releases_area_item_before_equipping) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto actor = game.newCreature();
    area->add(actor);
    auto trigger = game.newTrigger();
    trigger->deserialize(
        *resource::Gff::Builder().build(),
        SerializedIdentityContext::templateResource());
    area->add(trigger);
    auto item = game.newItem(
        *runtimeEquippedItem(
            "world_equip",
            {runtimeItemProperty(
                ItemProperty::BonusFeat,
                static_cast<uint16_t>(FeatType::Toughness))}),
        SerializedIdentityContext::templateResource("test-area"));
    const auto runtimeId = item->id();
    const auto incarnation = item->runtimeIncarnation();

    auto root = std::make_shared<graphics::ModelNode>(
        0, "world_equip", glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto model = std::make_unique<graphics::Model>(
        "world_equip", 0, root,
        std::vector<std::shared_ptr<graphics::Animation>>(), "", 1.0f);
    auto sceneNode = std::make_shared<scene::ModelSceneNode>(
        *model, scene::ModelUsage::Placeable, sceneGraph,
        engine.graphicsModule().services(),
        engine.audioModule().services(),
        engine.resourceModule().services());
    TestGameModule::setAreaRuntimeSceneNode(*item, sceneNode);
    EXPECT_CALL(sceneGraph, addRoot(sceneNode)).Times(1);
    EXPECT_CALL(sceneGraph, removeRoot(A<scene::ModelSceneNode &>()))
        .WillOnce([&sceneNode](scene::ModelSceneNode &removed) {
            EXPECT_EQ(sceneNode.get(), &removed);
        });
    area->add(item);
    Room room("world_room", glm::vec3(0.0f), nullptr, nullptr, nullptr);
    item->setRoom(&room);
    trigger->addTenant(item);
    auto action = game.newAction<EquipItemAction>(
        item, InventorySlots::body, true);

    action->execute(action, *actor, 0.0f);

    EXPECT_EQ(item, actor->getEquippedItem(InventorySlots::body));
    EXPECT_EQ(actor->id(), item->owner());
    EXPECT_TRUE(item->isEquipped());
    EXPECT_EQ(nullptr, item->room());
    EXPECT_FALSE(trigger->isTenant(item));
    EXPECT_EQ(
        area->objects().end(),
        std::find(area->objects().begin(), area->objects().end(), item));
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Item).empty());
    EXPECT_FALSE(area->getObjectByTag("world_equip"));
    EXPECT_TRUE(item->isRuntimeLive());
    EXPECT_EQ(runtimeId, item->id());
    EXPECT_EQ(incarnation, item->runtimeIncarnation());
    EXPECT_EQ(item, game.getObjectById(runtimeId));
    EXPECT_TRUE(actor->hasEffectiveFeat(FeatType::Toughness));
    ASSERT_EQ(1u, actor->effects().size());
    EXPECT_EQ(item, actor->effects().front().boundCreator());
}

TEST(RuntimeObjectOwnership, equip_action_replaces_slot_with_area_item) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto actor = game.newCreature();
    area->add(actor);
    auto displaced = game.newItem();
    displaced->deserialize(
        *runtimeItem("displaced"),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(actor->equip(InventorySlots::body, displaced));
    auto incoming = game.newItem();
    incoming->deserialize(
        *runtimeItem("incoming"),
        SerializedIdentityContext::templateResource());
    area->add(incoming);
    auto action = game.newAction<EquipItemAction>(
        incoming, InventorySlots::body, true);

    action->execute(action, *actor, 0.0f);

    EXPECT_EQ(incoming, actor->getEquippedItem(InventorySlots::body));
    EXPECT_EQ(actor->id(), incoming->owner());
    EXPECT_TRUE(incoming->isEquipped());
    EXPECT_EQ(
        area->objects().end(),
        std::find(area->objects().begin(), area->objects().end(), incoming));
    EXPECT_TRUE(area->getObjectsByType(ObjectType::Item).empty());
    ASSERT_EQ(1u, actor->items().size());
    EXPECT_EQ(displaced, actor->items().front());
    EXPECT_EQ(actor->id(), displaced->owner());
    EXPECT_FALSE(displaced->isEquipped());
    EXPECT_TRUE(displaced->isRuntimeLive());
}

TEST(RuntimeObjectOwnership, rejected_area_item_equip_preserves_area_ownership) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto actor = game.newCreature();
    area->add(actor);
    auto item = game.newItem();
    item->deserialize(
        *runtimeItem("wrong_slot"),
        SerializedIdentityContext::templateResource());
    area->add(item);
    auto action = game.newAction<EquipItemAction>(
        item, InventorySlots::rightWeapon, true);

    action->execute(action, *actor, 0.0f);

    EXPECT_FALSE(actor->getEquippedItem(InventorySlots::rightWeapon));
    EXPECT_EQ(0u, item->owner());
    EXPECT_FALSE(item->isEquipped());
    EXPECT_NE(
        area->objects().end(),
        std::find(area->objects().begin(), area->objects().end(), item));
    ASSERT_EQ(1u, area->getObjectsByType(ObjectType::Item).size());
    EXPECT_EQ(item, area->getObjectsByType(ObjectType::Item).front());
    EXPECT_EQ(item, area->getObjectByTag("wrong_slot"));
}

TEST(RuntimeObjectOwnership, equip_action_rejects_ownerless_nonresident_item) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto actor = game.newCreature();
    auto item = game.newItem();
    item->deserialize(
        *runtimeItem("unowned_equip"),
        SerializedIdentityContext::templateResource());
    auto action = game.newAction<EquipItemAction>(
        item, InventorySlots::body, true);

    action->execute(action, *actor, 0.0f);

    EXPECT_FALSE(actor->getEquippedItem(InventorySlots::body));
    EXPECT_EQ(0u, item->owner());
    EXPECT_FALSE(item->isEquipped());
    EXPECT_TRUE(item->isRuntimeLive());
    EXPECT_EQ(item, game.getObjectById(item->id()));
}

TEST(RuntimeObjectOwnership, direct_nested_ownership_rejects_area_item) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto actor = game.newCreature();
    auto item = game.newItem();
    item->deserialize(
        *runtimeItem("world_direct"),
        SerializedIdentityContext::templateResource());
    area->add(item);

    EXPECT_FALSE(actor->equip(InventorySlots::body, item));
    EXPECT_THROW(actor->addItem(item), ValidationException);

    EXPECT_FALSE(actor->getEquippedItem(InventorySlots::body));
    EXPECT_TRUE(actor->items().empty());
    EXPECT_EQ(0u, item->owner());
    EXPECT_FALSE(item->isEquipped());
    EXPECT_NE(
        area->objects().end(),
        std::find(area->objects().begin(), area->objects().end(), item));
    ASSERT_EQ(1u, area->getObjectsByType(ObjectType::Item).size());
    EXPECT_EQ(item, area->getObjectsByType(ObjectType::Item).front());
}

TEST(RuntimeObjectOwnership, equip_action_moves_equipped_item_between_creatures) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto source = game.newCreature();
    auto receiver = game.newCreature();
    auto item = game.newItem();
    item->deserialize(
        *runtimeItem("action_transfer"),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(source->equip(InventorySlots::body, item));
    auto action = game.newAction<EquipItemAction>(
        item, InventorySlots::body, true);

    action->execute(action, *receiver, 0.0f);

    EXPECT_FALSE(source->getEquippedItem(InventorySlots::body));
    EXPECT_EQ(item, receiver->getEquippedItem(InventorySlots::body));
    EXPECT_EQ(receiver->id(), item->owner());
    EXPECT_TRUE(item->isEquipped());
}

TEST(RuntimeObjectOwnership, rejected_replacement_preserves_original_graph) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto original = game.newItem();
    original->deserialize(
        *runtimeItem("original"),
        SerializedIdentityContext::templateResource());
    auto rejected = game.newItem();
    rejected->deserialize(
        *runtimeItem("rejected"),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(owner->equip(InventorySlots::body, original));
    owner->addItem(rejected);

    EXPECT_FALSE(owner->replaceEquipment(
        InventorySlots::body, rejected, *owner));

    EXPECT_EQ(original, owner->getEquippedItem(InventorySlots::body));
    EXPECT_TRUE(original->isEquipped());
    EXPECT_EQ(owner->id(), original->owner());
    ASSERT_EQ(1u, owner->items().size());
    EXPECT_EQ(rejected, owner->items().front());
    EXPECT_EQ(owner->id(), rejected->owner());
}

TEST(EquipmentCombatEffects, effective_stats_follow_exact_equipment_ownership) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    configureEquipmentEffectTables(engine);
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    owner->attributes().setAbilityScore(Ability::Strength, 12);
    auto item = game.newItem();
    item->deserialize(
        *runtimeEquippedItem(
            "combat_belt",
            {runtimeItemProperty(
                 ItemProperty::AbilityBonus,
                 static_cast<uint16_t>(Ability::Strength)),
             runtimeItemProperty(
                 ItemProperty::BonusFeat,
                 static_cast<uint16_t>(FeatType::Toughness)),
             runtimeItemProperty(
                 ItemProperty::Immunity,
                 static_cast<uint16_t>(ImmunityType::Stun)),
             runtimeItemProperty(ItemProperty::DamageResistance),
             runtimeItemProperty(ItemProperty::DamageReduction)}),
        SerializedIdentityContext::templateResource());

    EXPECT_EQ(12, owner->attributes().getAbilityScore(Ability::Strength));
    EXPECT_EQ(12, owner->getEffectiveAbilityScore(Ability::Strength));
    EXPECT_FALSE(owner->attributes().hasFeat(FeatType::Toughness));
    EXPECT_FALSE(owner->hasEffectiveFeat(FeatType::Toughness));
    ASSERT_TRUE(owner->equip(InventorySlots::body, item));

    EXPECT_EQ(12, owner->attributes().getAbilityScore(Ability::Strength));
    EXPECT_EQ(16, owner->getEffectiveAbilityScore(Ability::Strength));
    EXPECT_FALSE(owner->attributes().hasFeat(FeatType::Toughness));
    EXPECT_TRUE(owner->hasEffectiveFeat(FeatType::Toughness));
    EXPECT_TRUE(owner->hasEffect(EffectType::Immunity));
    EXPECT_TRUE(owner->hasEffect(EffectType::DamageResistance));
    EXPECT_TRUE(owner->hasEffect(EffectType::DamageReduction));
    ASSERT_EQ(5u, owner->effects().size());
    std::set<EffectId> ids;
    for (const EffectInstance &effect : owner->effects()) {
        EXPECT_EQ(DurationType::Equipped, effect.durationType());
        EXPECT_EQ(item, effect.boundCreator());
        EXPECT_TRUE(ids.insert(effect.id).second);
    }
    EXPECT_TRUE(owner->saveEffectSnapshot().empty());

    auto receiver = game.newCreature();
    ASSERT_TRUE(owner->moveEquippedItemTo(item, *receiver));
    EXPECT_FALSE(owner->getEquippedItem(InventorySlots::body));
    EXPECT_EQ(12, owner->getEffectiveAbilityScore(Ability::Strength));
    EXPECT_FALSE(owner->hasEffectiveFeat(FeatType::Toughness));
    EXPECT_FALSE(owner->hasEffect(EffectType::Immunity));
    EXPECT_TRUE(owner->effects().empty());
    ASSERT_EQ(1u, receiver->items().size());
    EXPECT_EQ(item, receiver->items().front());
    EXPECT_FALSE(item->isEquipped());
}

TEST(EquipmentCombatEffects, replacement_rebinds_to_the_exact_item_incarnation) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto receiver = game.newCreature();
    auto itemA = game.newItem();
    auto itemB = game.newItem();
    auto rejected = game.newItem();
    const auto property = [] {
        return std::vector<std::shared_ptr<resource::Gff>> {
            runtimeItemProperty(
                ItemProperty::BonusFeat,
                static_cast<uint16_t>(FeatType::Toughness))};
    };
    itemA->deserialize(
        *runtimeEquippedItem("same_template", property()),
        SerializedIdentityContext::templateResource());
    itemB->deserialize(
        *runtimeEquippedItem("same_template", property()),
        SerializedIdentityContext::templateResource());
    rejected->deserialize(
        *runtimeEquippedItem("same_template", property()),
        SerializedIdentityContext::templateResource());

    ASSERT_TRUE(owner->equip(InventorySlots::body, itemA));
    ASSERT_EQ(1u, owner->effects().size());
    const EffectId firstEffectId = owner->effects().front().id;
    EXPECT_EQ(itemA, owner->effects().front().boundCreator());

    ASSERT_TRUE(owner->replaceEquipment(
        InventorySlots::body, itemB, *receiver));
    ASSERT_EQ(1u, owner->effects().size());
    EXPECT_NE(firstEffectId, owner->effects().front().id);
    EXPECT_EQ(itemB, owner->effects().front().boundCreator());
    ASSERT_EQ(1u, receiver->items().size());
    EXPECT_EQ(itemA, receiver->items().front());
    EXPECT_TRUE(owner->hasEffectiveFeat(FeatType::Toughness));

    owner->addItem(rejected);
    EXPECT_FALSE(owner->replaceEquipment(
        InventorySlots::body, rejected, *receiver));
    ASSERT_EQ(1u, owner->effects().size());
    EXPECT_EQ(itemB, owner->effects().front().boundCreator());
    EXPECT_EQ(itemB, owner->getEquippedItem(InventorySlots::body));
}

TEST(EquipmentCombatEffects, retired_exact_source_fails_closed) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto item = game.newItem();
    item->deserialize(
        *runtimeEquippedItem(
            "retired_source",
            {runtimeItemProperty(
                ItemProperty::BonusFeat,
                static_cast<uint16_t>(FeatType::Toughness))}),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(owner->equip(InventorySlots::body, item));
    ASSERT_TRUE(owner->hasEffectiveFeat(FeatType::Toughness));

    game.destroyRuntimeObjectGraph(item);

    EXPECT_FALSE(item->isRuntimeLive());
    EXPECT_FALSE(owner->hasEffectiveFeat(FeatType::Toughness));
    EXPECT_FALSE(owner->effects().front().boundCreator());
    owner->update(0.0f);
    EXPECT_TRUE(owner->effects().empty());
}

TEST(EquipmentCombatEffects, staged_template_and_saved_graphs_rebuild_once) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeAppearances()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto bonusFeat = [] {
        return runtimeItemProperty(
            ItemProperty::BonusFeat,
            static_cast<uint16_t>(FeatType::Toughness));
    };

    auto templateCreature = game.newCreature();
    auto templateRecord = resource::Gff::Builder()
                              .field(resource::Gff::Field::newList(
                                  "Equip_ItemList",
                                  {runtimeEquippedItem(
                                      "template_equipment", {bonusFeat()})}))
                              .build();
    templateCreature->deserialize(
        *templateRecord,
        SerializedIdentityContext::templateResource("template_creature"));
    ASSERT_TRUE(templateCreature->hasEffectiveFeat(FeatType::Toughness));
    ASSERT_EQ(1u, templateCreature->effects().size());
    auto templateItem =
        templateCreature->getEquippedItem(InventorySlots::body);
    ASSERT_TRUE(templateItem);
    EXPECT_EQ(templateItem, templateCreature->effects().front().boundCreator());
    EXPECT_TRUE(templateCreature->saveEffectSnapshot().empty());

    auto savedCreature = game.newCreature();
    auto savedRecord = resource::Gff::Builder()
                           .field(resource::Gff::Field::newList(
                               "Equip_ItemList",
                               {runtimeEquippedItem(
                                   "saved_equipment", {bonusFeat()}, 4701)}))
                           .build();
    savedCreature->deserialize(
        *savedRecord,
        SerializedIdentityContext::moduleGraph("saved_creature"));
    ASSERT_TRUE(savedCreature->hasEffectiveFeat(FeatType::Toughness));
    ASSERT_EQ(1u, savedCreature->effects().size());
    auto savedItem = savedCreature->getEquippedItem(InventorySlots::body);
    ASSERT_TRUE(savedItem);
    EXPECT_EQ(savedItem, savedCreature->effects().front().boundCreator());
    EXPECT_TRUE(savedCreature->saveEffectSnapshot().empty());
    EXPECT_EQ(savedItem, game.getObjectBySavedId(4701));
}

TEST(RuntimeObjectOwnership, displaced_equipment_merges_and_is_finalized) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto displaced = game.newItem();
    displaced->deserialize(
        *runtimeItem("stack"),
        SerializedIdentityContext::templateResource());
    auto retained = game.newItem();
    retained->deserialize(
        *runtimeItem("stack"),
        SerializedIdentityContext::templateResource());
    auto replacement = game.newItem();
    replacement->deserialize(
        *runtimeItem("replacement"),
        SerializedIdentityContext::templateResource());
    ASSERT_TRUE(owner->equip(InventorySlots::body, displaced));
    owner->addItem(retained);

    ASSERT_TRUE(owner->replaceEquipment(
        InventorySlots::body, replacement, *owner));

    EXPECT_EQ(replacement, owner->getEquippedItem(InventorySlots::body));
    ASSERT_EQ(1u, owner->items().size());
    EXPECT_EQ(retained, owner->items().front());
    EXPECT_EQ(2, retained->stackSize());
    EXPECT_FALSE(displaced->isRuntimeLive());
    EXPECT_FALSE(game.getObjectById(displaced->id()));
    EXPECT_EQ(0u, displaced->owner());
}

TEST(RuntimeObjectOwnership, repeated_equip_and_unequip_preserves_one_owner_edge) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(runtimeBaseItems()));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    auto item = game.newItem();
    item->deserialize(
        *runtimeItem("cycle"),
        SerializedIdentityContext::templateResource());
    owner->addItem(item);
    const size_t registrySize = TestGameModule::objectRegistrySize(game);

    for (int i = 0; i < 5; ++i) {
        auto candidate = takeEquipmentCandidate(game, *owner, item);
        ASSERT_EQ(item, candidate);
        ASSERT_TRUE(owner->equip(InventorySlots::body, candidate));
        ASSERT_TRUE(owner->moveEquippedItemTo(candidate, *owner));
    }

    EXPECT_EQ(registrySize, TestGameModule::objectRegistrySize(game));
    ASSERT_EQ(1u, owner->items().size());
    EXPECT_EQ(item, owner->items().front());
    EXPECT_EQ(owner->id(), item->owner());
    EXPECT_FALSE(item->isEquipped());
}

TEST(RuntimeObjectOwnership, failed_blueprint_equip_does_not_leak_item) {
    TestEngine engine;
    engine.init();
    EXPECT_CALL(
        engine.resourceModule().gffs(),
        get("not_equipment", resource::ResType::Uti))
        .WillOnce(Return(nullptr));
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto owner = game.newCreature();
    const size_t registrySize = TestGameModule::objectRegistrySize(game);

    EXPECT_FALSE(owner->equip("not_equipment"));

    EXPECT_EQ(registrySize, TestGameModule::objectRegistrySize(game));
}

TEST(RuntimeObjectOwnership, template_and_instance_item_graph_overlay_is_exact) {
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
    auto templated = resource::Gff::Builder()
                         .field(resource::Gff::Field::newList(
                             "ItemList", {runtimeItem("template_item")}))
                         .build();
    creature->deserialize(
        *templated, SerializedIdentityContext::templateResource("template"));
    auto templateItem = creature->items().front();

    auto noOverride = resource::Gff::Builder()
                          .field(resource::Gff::Field::newDword("ObjectId", 860))
                          .build();
    creature->deserialize(
        *noOverride, SerializedIdentityContext::moduleGraph("overlay"));
    ASSERT_EQ(1u, creature->items().size());
    EXPECT_EQ(templateItem, creature->items().front());

    auto overrideRecord = resource::Gff::Builder()
                              .field(resource::Gff::Field::newDword("ObjectId", 860))
                              .field(resource::Gff::Field::newList(
                                  "ItemList", {runtimeItem("instance_item", 861)}))
                              .build();
    creature->deserialize(
        *overrideRecord, SerializedIdentityContext::moduleGraph("overlay"));

    ASSERT_EQ(1u, creature->items().size());
    EXPECT_EQ("instance_item", creature->items().front()->tag());
    EXPECT_FALSE(templateItem->isRuntimeLive());
    EXPECT_FALSE(game.getObjectById(templateItem->id()));
}

TEST(RuntimeObjectPresentation, preview_objects_never_enter_gameplay_registry) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto gameplayItem = game.newItem();
    gameplayItem->setOwner(77);
    const size_t registrySize = TestGameModule::objectRegistrySize(game);
    for (int i = 0; i < 5; ++i) {
        auto preview = game.newPresentationCreature("preview");
        auto previewItem = game.newPresentationItem();
        previewItem->clone(*gameplayItem);

        EXPECT_TRUE(preview->isPresentationOnly());
        EXPECT_TRUE(previewItem->isPresentationOnly());
        EXPECT_FALSE(preview->isRuntimeLive());
        EXPECT_FALSE(game.getObjectById(preview->id()));
        EXPECT_FALSE(game.getObjectById(previewItem->id()));
    }
    EXPECT_EQ(registrySize, TestGameModule::objectRegistrySize(game));
    EXPECT_EQ(77u, gameplayItem->owner());
}

TEST(AreaRuntimeRetirement, candidate_cleanup_handles_partial_party_placement_idempotently) {
    TestEngine engine;
    engine.init();
    NiceMock<scene::MockSceneGraph> sceneGraph;
    configureRuntimeMocks(engine, sceneGraph);
    Room room("partial", glm::vec3(0.0f), nullptr, nullptr, nullptr);
    ON_CALL(sceneGraph, testElevation(_, _))
        .WillByDefault(Invoke([&room](
                                  const glm::vec3 &position,
                                  scene::Collision &collision) {
            collision.intersection = position;
            collision.user = &room;
            return true;
        }));
    StubConsole console;
    Game game(resource::GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);

    auto pc = game.newCreature();
    game.party().addMember(kNpcPlayer, pc);
    game.party().setPlayer(pc);
    game.party().setActualPlayer(pc);
    auto companion = game.newCreature();
    ASSERT_TRUE(game.party().addAvailableMember(0, companion));
    ASSERT_TRUE(game.party().addMember(0, companion));
    auto puppet = game.newCreature();
    ASSERT_TRUE(game.party().addAvailablePuppet(0, puppet));
    ASSERT_TRUE(game.party().addPuppet(0, puppet));

    auto trigger = game.newTrigger();
    area->add(trigger);
    area->add(pc);
    area->add(puppet);
    trigger->addTenant(pc);
    trigger->addTenant(puppet);
    TestGameModule::setAreaRuntimePath(*pc, area->pathfinder());
    TestGameModule::setAreaRuntimePath(*puppet, area->pathfinder());
    ASSERT_EQ(&room, pc->room());
    ASSERT_EQ(&room, puppet->room());
    ASSERT_EQ(nullptr, companion->room());

    game.retireActiveModuleRuntime();

    EXPECT_FALSE(game.module());
    EXPECT_EQ(nullptr, pc->room());
    EXPECT_EQ(nullptr, puppet->room());
    EXPECT_EQ(nullptr, companion->room());
    EXPECT_FALSE(room.tenants().count(pc.get()));
    EXPECT_FALSE(room.tenants().count(puppet.get()));
    EXPECT_FALSE(trigger->isTenant(pc));
    EXPECT_FALSE(trigger->isTenant(puppet));
    EXPECT_FALSE(TestGameModule::hasAreaRuntimePath(*pc));
    EXPECT_FALSE(TestGameModule::hasAreaRuntimePath(*puppet));
    EXPECT_EQ(pc, game.party().actualPlayer());
    EXPECT_EQ(companion, game.party().rosterCreature({RosterKind::Npc, 0}));
    EXPECT_EQ(puppet, game.party().rosterCreature({RosterKind::Puppet, 0}));
    EXPECT_EQ(pc, game.getObjectById(pc->id()));
    EXPECT_EQ(companion, game.getObjectById(companion->id()));
    EXPECT_EQ(puppet, game.getObjectById(puppet->id()));

    // Re-entering both cleanup boundaries after partial retirement is safe.
    game.retireActiveModuleRuntime();
    EXPECT_CALL(engine.audioModule().mixer(), stopAll()).Times(1);
    EXPECT_CALL(sceneGraph, clear()).Times(AnyNumber());
    game.retireRuntimeSession();
}

TEST(AreaRuntimeRetirement, retire_party_includes_inactive_roster_and_puppets) {
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

        area->retirePartyAreaRuntime();

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
