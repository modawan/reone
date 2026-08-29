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

#include <map>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/action.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/docommand.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/game.h"
#include "reone/game/savedruntime.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/executionstate.h"
#include "reone/script/program.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

SerializedIdentityContext savedRuntimeIdentityContext() {
    return SerializedIdentityContext::moduleGraph("test-module");
}

std::shared_ptr<Gff> valueParameter(uint32_t type, Gff::Field value) {
    return Gff::Builder()
        .type(type)
        .field(Gff::Field::newDword("Type", type))
        .field(std::move(value))
        .build();
}

std::shared_ptr<Gff> savedLocation() {
    return Gff::Builder()
        .type(18)
        .field(Gff::Field::newFloat("PositionX", 1.0f))
        .field(Gff::Field::newFloat("PositionY", 2.0f))
        .field(Gff::Field::newFloat("PositionZ", 3.0f))
        .field(Gff::Field::newFloat("OrientationX", 0.0f))
        .field(Gff::Field::newFloat("OrientationY", 1.0f))
        .field(Gff::Field::newFloat("OrientationZ", 0.0f))
        .build();
}

std::shared_ptr<Gff> savedSituation() {
    auto integer = Gff::Builder()
                       .type(0)
                       .field(Gff::Field::newChar("Type", 3))
                       .field(Gff::Field::newInt("Value", -7))
                       .build();
    auto object = Gff::Builder()
                      .type(1)
                      .field(Gff::Field::newChar("Type", 6))
                      .field(Gff::Field::newDword("Value", 2))
                      .build();
    auto location = Gff::Builder()
                        .type(2)
                        .field(Gff::Field::newChar("Type", 18))
                        .field(Gff::Field::newStruct("GameDefinedStrct", savedLocation()))
                        .build();
    auto unsupported = Gff::Builder()
                           .type(3)
                           .field(Gff::Field::newChar("Type", 23))
                           .field(Gff::Field::newStruct(
                               "GameDefinedStrct",
                               Gff::Builder().type(23).field(Gff::Field::newInt("Meaning", 99)).build()))
                           .build();
    auto stack = Gff::Builder()
                     .type(0)
                     .field(Gff::Field::newInt("BasePointer", 1))
                     .field(Gff::Field::newInt("StackPointer", 4))
                     .field(Gff::Field::newInt("TotalSize", 8))
                     .field(Gff::Field::newList("Stack", {integer, object, location, unsupported}))
                     .build();
    return Gff::Builder()
        .type(0)
        .field(Gff::Field::newInt("CodeSize", 4))
        .field(Gff::Field::newVoid("Code", ByteBuffer {1, 2, 3, 4}))
        .field(Gff::Field::newDword("CRC", 0x1234))
        .field(Gff::Field::newInt("InstructionPtr", 42))
        .field(Gff::Field::newInt("SecondaryPtr", 3))
        .field(Gff::Field::newCExoString("Name", "saved_script"))
        .field(Gff::Field::newInt("StackSize", 4))
        .field(Gff::Field::newStruct("Stack", stack))
        .field(Gff::Field::newInt("FutureField", 77))
        .build();
}

std::shared_ptr<Gff> action(
    uint32_t id,
    uint16_t group,
    std::vector<std::shared_ptr<Gff>> parameters = {}) {
    Gff::Builder builder;
    builder.type(0)
        .field(Gff::Field::newDword("ActionId", id))
        .field(Gff::Field::newWord("GroupActionId", group))
        .field(Gff::Field::newWord("NumParams", static_cast<uint16_t>(parameters.size())));
    if (!parameters.empty()) {
        builder.field(Gff::Field::newList("Paramaters", std::move(parameters)));
    }
    return builder.build();
}

std::shared_ptr<Gff> basicAttackAction(
    uint32_t target,
    uint16_t group = 7) {
    return action(12, group, {
        valueParameter(1, Gff::Field::newInt("Value", 0)),
        valueParameter(3, Gff::Field::newDword("Value", target)),
        valueParameter(1, Gff::Field::newInt("Value", 1)),
        valueParameter(1, Gff::Field::newInt("Value", 10009)),
        valueParameter(1, Gff::Field::newInt("Value", 1500)),
        valueParameter(1, Gff::Field::newInt("Value", 1)),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
        valueParameter(1, Gff::Field::newInt("Value", 4)),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
    });
}

std::shared_ptr<Gff> moveToPointAction(
    uint32_t area,
    glm::vec3 destination = glm::vec3(12.0f, 34.0f, 5.0f),
    int32_t flags = 1,
    float timeout = 0.0f,
    int32_t day = 0,
    int32_t time = 0,
    uint16_t group = 9) {
    return action(1, group, {
        valueParameter(2, Gff::Field::newFloat("Value", destination.x)),
        valueParameter(2, Gff::Field::newFloat("Value", destination.y)),
        valueParameter(2, Gff::Field::newFloat("Value", destination.z)),
        valueParameter(3, Gff::Field::newDword("Value", area)),
        valueParameter(3, Gff::Field::newDword("Value", kSavedRuntimeInvalidObjectId)),
        valueParameter(1, Gff::Field::newInt("Value", flags)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.0f)),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
        valueParameter(2, Gff::Field::newFloat("Value", timeout)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.0f)),
        valueParameter(1, Gff::Field::newInt("Value", day)),
        valueParameter(1, Gff::Field::newInt("Value", time)),
    });
}

std::shared_ptr<Gff> event(
    uint32_t eventId,
    uint32_t day,
    uint32_t time,
    std::shared_ptr<Gff> data = nullptr) {
    Gff::Builder builder;
    builder.type(0)
        .field(Gff::Field::newDword("Day", day))
        .field(Gff::Field::newDword("Time", time))
        .field(Gff::Field::newDword("ObjectId", 2))
        .field(Gff::Field::newDword("CallerId", 3))
        .field(Gff::Field::newDword("EventId", eventId));
    if (data) {
        builder.field(Gff::Field::newStruct("EventData", std::move(data)));
    }
    return builder.build();
}

std::shared_ptr<Gff> minimalEffect() {
    return Gff::Builder()
        .type(0x1111)
        .field(Gff::Field::newDword64("Id", 10))
        .field(Gff::Field::newWord("Type", 68))
        .field(Gff::Field::newWord("SubType", 1))
        .field(Gff::Field::newFloat("Duration", 2.0f))
        .field(Gff::Field::newDword(
            "CreatorId", kSavedEffectInvalidObjectId))
        .build();
}

void registerModuleIdentity(
    Game &game,
    uint32_t savedId,
    const std::shared_ptr<Object> &object) {
    game.registerSavedObjectIdentity(
        savedId,
        object,
        SerializedIdentityContext::moduleGraph("test-module"));
}

TEST(SavedAction, should_preserve_retail_fields_parameter_types_and_order) {
    std::vector<std::shared_ptr<Gff>> parameters {
        valueParameter(1, Gff::Field::newInt("Value", -11)),
        valueParameter(2, Gff::Field::newFloat("Value", 2.5f)),
        valueParameter(3, Gff::Field::newDword("Value", kSavedRuntimeInvalidObjectId)),
        valueParameter(4, Gff::Field::newCExoString("Value", "hello")),
        valueParameter(5, Gff::Field::newStruct("Value", savedSituation())),
        valueParameter(99, Gff::Field::newInt("Value", 123)),
    };
    auto root = Gff::Builder()
                    .field(Gff::Field::newList(
                        "ActionList",
                        {action(30, 0), action(37, 0xffff, std::move(parameters)), action(1, 0xfffe)}))
                    .build();

    SavedActionQueue queue = SavedActionQueue::fromGff(
        *root, savedRuntimeIdentityContext());

    ASSERT_EQ(queue.actions.size(), 3);
    EXPECT_EQ(queue.actions[0].actionId, 30);
    EXPECT_EQ(queue.actions[1].actionId, 37);
    EXPECT_EQ(queue.actions[2].actionId, 1);
    EXPECT_EQ(queue.actions[1].groupActionId, 0xffff);
    EXPECT_EQ(queue.actions[2].groupActionId, 0xfffe);
    EXPECT_EQ(queue.actions[0].declaredParameterCount, 0);
    ASSERT_EQ(queue.actions[1].parameters.size(), 6);
    EXPECT_EQ(std::get<int32_t>(queue.actions[1].parameters[0].payload), -11);
    EXPECT_FLOAT_EQ(std::get<float>(queue.actions[1].parameters[1].payload), 2.5f);
    EXPECT_EQ(std::get<SavedObjectReference>(queue.actions[1].parameters[2].payload).id,
              kSavedRuntimeInvalidObjectId);
    EXPECT_EQ(std::get<std::string>(queue.actions[1].parameters[3].payload), "hello");
    EXPECT_TRUE(std::holds_alternative<SerializedScriptSituation>(queue.actions[1].parameters[4].payload));
    EXPECT_TRUE(std::holds_alternative<UnsupportedSavedPayload>(queue.actions[1].parameters[5].payload));
    EXPECT_EQ(queue.actions[1].executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);
}

TEST(SavedAction, should_convert_only_a_proven_supported_reone_action) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto wait = SavedActionRecord::fromGff(
        *action(30, 7, {valueParameter(2, Gff::Field::newFloat("Value", 1.5f))}),
        savedRuntimeIdentityContext());
    auto unknown = SavedActionRecord::fromGff(
        *action(0xdead, 8), savedRuntimeIdentityContext());

    auto runtime = wait.toRuntimeAction(game);
    ASSERT_TRUE(runtime);
    EXPECT_EQ(runtime->type(), ActionType::Wait);
    EXPECT_EQ(wait.executionSupport(), SavedExecutionSupport::Executable);
    EXPECT_FALSE(unknown.toRuntimeAction(game));
    EXPECT_EQ(unknown.executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);
}

TEST(SavedAction, follow_leader_uses_exact_parameterless_retail_shape_in_both_titles) {
    for (auto gameId : {resource::GameID::KotOR, resource::GameID::TSL}) {
        TestEngine &engine = testEngine();
        StubConsole console;
        Game game(gameId, "", engine.options(), engine.services(), console);

        auto runtime = game.newAction<FollowLeaderAction>();
        SavedActionRecord provenance;
        provenance.groupActionId = 23;
        runtime->attachSavedAction(provenance);

        auto exported = runtime->saveFacingState();
        ASSERT_TRUE(exported);
        EXPECT_EQ(exported->actionId, 61u);
        EXPECT_EQ(exported->groupActionId, 23);
        EXPECT_EQ(exported->declaredParameterCount, 0);
        EXPECT_TRUE(exported->parameters.empty());

        auto encoded = action(exported->actionId, exported->groupActionId);
        auto imported = SavedActionRecord::fromGff(
            *encoded, savedRuntimeIdentityContext());
        EXPECT_EQ(imported.executionSupport(), SavedExecutionSupport::Executable);
        auto restored = std::dynamic_pointer_cast<FollowLeaderAction>(
            imported.toRuntimeAction(game));
        ASSERT_TRUE(restored);
        ASSERT_TRUE(restored->originalSavedAction());
        EXPECT_EQ(restored->originalSavedAction()->groupActionId, 23);
        auto reexported = restored->saveFacingState();
        ASSERT_TRUE(reexported);
        EXPECT_EQ(reexported->actionId, 61u);
        EXPECT_EQ(reexported->declaredParameterCount, 0);
        EXPECT_TRUE(reexported->parameters.empty());
    }
}

TEST(SavedAction, follow_leader_rejects_any_parameter_shape) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    auto wrongCount = SavedActionRecord::fromGff(
        *action(61, 7, {valueParameter(1, Gff::Field::newInt("Value", 0))}),
        savedRuntimeIdentityContext());
    EXPECT_EQ(wrongCount.executionSupport(),
              SavedExecutionSupport::RepresentableButUnsupported);
    EXPECT_FALSE(wrongCount.toRuntimeAction(game));

    auto hiddenParameter = SavedActionRecord::fromGff(
        *action(61, 7), savedRuntimeIdentityContext());
    hiddenParameter.parameters.push_back(SavedActionParameter {
        static_cast<uint32_t>(SavedActionParameterType::Integer), int32_t {0}});
    EXPECT_EQ(hiddenParameter.executionSupport(),
              SavedExecutionSupport::RepresentableButUnsupported);
    EXPECT_FALSE(hiddenParameter.toRuntimeAction(game));
}

TEST(SavedAction, restored_follow_leader_resolves_the_leader_only_at_execution) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);

    ASSERT_TRUE(game.party().isEmpty());
    auto record = SavedActionRecord::fromGff(
        *action(61, 9), savedRuntimeIdentityContext());
    auto restored = std::dynamic_pointer_cast<FollowLeaderAction>(
        record.toRuntimeAction(game));
    ASSERT_TRUE(restored);

    // Publication may establish or switch the party leader after action import.
    // The parameterless record must remain inert and unbound until execution.
    auto publishedLeader = game.newCreature();
    publishedLeader->setPosition(glm::vec3(0.0f));
    ASSERT_TRUE(game.party().addMember(kNpcPlayer, publishedLeader));
    auto follower = game.newCreature();
    ASSERT_EQ(game.party().getLeader(), publishedLeader);
    restored->execute(restored, *follower, 1.0f);

    // Completion at the leader published after import proves the wire record
    // did not capture or bind a target during reconstruction.
    EXPECT_TRUE(restored->isCompleted());
}

TEST(SavedAction, attack_object_exports_exact_retail_basic_attack_shape_in_both_titles) {
    for (GameID id : {GameID::KotOR, GameID::TSL}) {
        TestEngine &engine = testEngine();
        StubConsole console;
        Game game(id, "", engine.options(), engine.services(), console);
        auto target = game.newCreature();
        auto runtime = game.newAction<AttackObjectAction>(target);
        SavedActionRecord provenance;
        provenance.groupActionId = 23;
        runtime->attachSavedAction(provenance);

        auto exported = runtime->saveFacingState();

        ASSERT_TRUE(exported);
        EXPECT_EQ(exported->actionId, 12u);
        EXPECT_EQ(exported->groupActionId, 23);
        EXPECT_EQ(exported->declaredParameterCount, 10);
        ASSERT_EQ(exported->parameters.size(), 10);
        std::array<uint32_t, 10> types {1, 3, 1, 1, 1, 1, 1, 1, 1, 1};
        for (size_t i = 0; i < types.size(); ++i) {
            EXPECT_EQ(exported->parameters[i].type, types[i]);
        }
        EXPECT_EQ(
            std::get<SavedObjectReference>(exported->parameters[1].payload).id,
            target->id());
        std::array<int32_t, 9> values {0, 1, 10009, 1500, 1, 0, 0, 4, 0};
        for (size_t i = 0, value = 0; i < exported->parameters.size(); ++i) {
            if (i == 1) continue;
            EXPECT_EQ(std::get<int32_t>(exported->parameters[i].payload),
                      values[value++]);
        }
        EXPECT_EQ(exported->executionSupport(), SavedExecutionSupport::Executable);
    }
}

TEST(SavedAction, attack_object_imports_bound_target_and_preserves_group) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    registerModuleIdentity(game, target->id(), target);
    auto record = SavedActionRecord::fromGff(
        *basicAttackAction(target->id(), 41), savedRuntimeIdentityContext());

    ASSERT_EQ(record.executionSupport(), SavedExecutionSupport::Executable);
    ASSERT_TRUE(record.bindObjectReferences(game));
    auto restored = std::dynamic_pointer_cast<AttackObjectAction>(
        record.toRuntimeAction(game));

    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->target(), target);
    ASSERT_TRUE(restored->originalSavedAction());
    EXPECT_EQ(restored->originalSavedAction()->groupActionId, 41);
    EXPECT_FALSE(restored->isCompleted());
    EXPECT_FALSE(restored->isCancelled());
}

TEST(SavedAction, attack_object_rejects_malformed_special_or_missing_targets) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto record = SavedActionRecord::fromGff(
        *basicAttackAction(123456), savedRuntimeIdentityContext());
    EXPECT_FALSE(record.bindObjectReferences(game));
    EXPECT_FALSE(record.toRuntimeAction(game));

    auto target = game.newCreature();
    record = SavedActionRecord::fromGff(
        *basicAttackAction(target->id()), savedRuntimeIdentityContext());
    record.parameters[6].payload = int32_t {1703936};
    EXPECT_EQ(record.executionSupport(),
              SavedExecutionSupport::RepresentableButUnsupported);
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[6].payload = int32_t {0};
    record.parameters[1].type = 1;
    EXPECT_EQ(record.executionSupport(),
              SavedExecutionSupport::RepresentableButUnsupported);
    record.parameters[1].type = 3;
    record.declaredParameterCount = 9;
    EXPECT_EQ(record.executionSupport(),
              SavedExecutionSupport::RepresentableButUnsupported);
}

TEST(SavedAction, attack_object_self_target_completes_without_starting_combat) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto actor = game.newCreature();
    auto attack = game.newAction<AttackObjectAction>(actor);

    attack->execute(attack, *actor, 0.0f);

    EXPECT_TRUE(attack->isCompleted());
    EXPECT_FALSE(attack->isCancelled());
}

TEST(SavedRuntimePublication, attack_object_is_installed_inertly_ahead_of_later_action) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    registerModuleIdentity(game, target->id(), target);
    auto saved = Gff::Builder()
                     .field(Gff::Field::newList(
                         "ActionList",
                         {basicAttackAction(target->id(), 11),
                          action(30, 12, {
                              valueParameter(
                                  2, Gff::Field::newFloat("Value", 2.0f)),
                          })}))
                     .build();
    auto actor = game.newCreature();

    actor->deserializeRuntimeState(*saved, savedRuntimeIdentityContext());
    EXPECT_TRUE(actor->actions().empty());
    actor->bindSavedRuntimeState();
    EXPECT_TRUE(actor->actions().empty());
    actor->publishSavedRuntimeState();

    ASSERT_EQ(actor->actions().size(), 2);
    auto attack = std::dynamic_pointer_cast<AttackObjectAction>(actor->actions()[0]);
    ASSERT_TRUE(attack);
    EXPECT_EQ(attack->target(), target);
    EXPECT_EQ(actor->actions()[1]->type(), ActionType::Wait);
    EXPECT_FALSE(attack->isCompleted());
    EXPECT_FALSE(actor->actions()[1]->isCompleted());
}

TEST(SavedAction, play_animation_rejects_malformed_parameter_shapes) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto valid = action(6, 7, {
        valueParameter(3, Gff::Field::newDword(
            "Value", static_cast<uint32_t>(AnimationType::LoopingPause))),
        valueParameter(2, Gff::Field::newFloat("Value", 1.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 4.0f)),
        valueParameter(1, Gff::Field::newInt("Value", 1)),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
    });
    auto record = SavedActionRecord::fromGff(
        *valid, savedRuntimeIdentityContext());
    EXPECT_EQ(record.executionSupport(), SavedExecutionSupport::Executable);
    EXPECT_TRUE(record.toRuntimeAction(game));

    record.declaredParameterCount = 4;
    EXPECT_EQ(record.executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.declaredParameterCount = 5;
    record.parameters[3].payload = int32_t {2};
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[3].payload = int32_t {1};
    record.parameters[1].payload = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[1].payload = 1.0f;
    record.parameters[0].payload = SavedObjectReference {42};
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[0].payload = SavedObjectReference {100};
    record.parameters[0].type = 1;
    EXPECT_EQ(record.executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);
}

TEST(SavedAction, move_to_object_uses_retail_action_17_shape_and_bound_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    registerModuleIdentity(game, target->id(), target);
    auto saved = action(17, 13, {
        valueParameter(3, Gff::Field::newDword("Value", target->id())),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.5f)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.5f)),
        valueParameter(1, Gff::Field::newInt("Value", 1)),
    });
    auto record = SavedActionRecord::fromGff(
        *saved, savedRuntimeIdentityContext());

    EXPECT_EQ(record.executionSupport(), SavedExecutionSupport::Executable);
    EXPECT_TRUE(record.bindObjectReferences(game));
    auto runtime = std::dynamic_pointer_cast<MoveToObjectAction>(
        record.toRuntimeAction(game));
    ASSERT_TRUE(runtime);
    EXPECT_EQ(runtime->target(), target);
    EXPECT_FALSE(runtime->isRun());
    EXPECT_FLOAT_EQ(runtime->range(), 0.5f);
    EXPECT_FALSE(runtime->isForced());
    EXPECT_FLOAT_EQ(runtime->timeout(), -1.0f);
    ASSERT_TRUE(runtime->originalSavedAction());
    EXPECT_EQ(runtime->originalSavedAction()->groupActionId, 13);

    auto exported = runtime->saveFacingState();
    ASSERT_TRUE(exported);
    EXPECT_EQ(exported->actionId, 17u);
    EXPECT_EQ(exported->groupActionId, 13);
    ASSERT_EQ(exported->parameters.size(), 5);
    EXPECT_EQ(std::get<SavedObjectReference>(exported->parameters[0].payload).id,
              target->id());
    EXPECT_EQ(std::get<int32_t>(exported->parameters[1].payload), 0);
    EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[2].payload), 0.5f);
    EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[3].payload), 0.5f);
    EXPECT_EQ(std::get<int32_t>(exported->parameters[4].payload), 1);
}

TEST(SavedAction, move_to_object_rejects_malformed_or_missing_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto saved = action(17, 3, {
        valueParameter(3, Gff::Field::newDword("Value", 123456)),
        valueParameter(1, Gff::Field::newInt("Value", 1)),
        valueParameter(2, Gff::Field::newFloat("Value", 1.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 1.0f)),
        valueParameter(1, Gff::Field::newInt("Value", 1)),
    });
    auto record = SavedActionRecord::fromGff(
        *saved, savedRuntimeIdentityContext());
    EXPECT_FALSE(record.bindObjectReferences(game));
    EXPECT_FALSE(record.toRuntimeAction(game));

    record.parameters[1].payload = int32_t {2};
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[1].payload = int32_t {1};
    record.parameters[3].payload = 2.0f;
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[3].payload = 1.0f;
    record.parameters[4].payload = int32_t {0};
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.declaredParameterCount = 4;
    EXPECT_EQ(record.executionSupport(),
              SavedExecutionSupport::RepresentableButUnsupported);
}

TEST(SavedAction, forced_move_to_object_uses_retail_action_1_and_preserves_timing) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto target = game.newCreature();
    registerModuleIdentity(game, area->id(), area);
    registerModuleIdentity(game, target->id(), target);
    auto saved = action(1, 13, {
        valueParameter(2, Gff::Field::newFloat("Value", 46.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 17.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 1.9f)),
        valueParameter(3, Gff::Field::newDword("Value", area->id())),
        valueParameter(3, Gff::Field::newDword("Value", target->id())),
        valueParameter(1, Gff::Field::newInt("Value", 5)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.5f)),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
        valueParameter(2, Gff::Field::newFloat("Value", 30.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.0f)),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
    });
    auto record = SavedActionRecord::fromGff(
        *saved, savedRuntimeIdentityContext());
    ASSERT_EQ(record.executionSupport(), SavedExecutionSupport::Executable);
    ASSERT_TRUE(record.bindObjectReferences(game));
    auto runtime = std::dynamic_pointer_cast<MoveToObjectAction>(record.toRuntimeAction(game));
    ASSERT_TRUE(runtime);
    EXPECT_TRUE(runtime->isForced());
    EXPECT_TRUE(runtime->isRun());
    EXPECT_FLOAT_EQ(runtime->timeout(), 30.0f);
    EXPECT_FALSE(runtime->forcedState().active);
    EXPECT_EQ(runtime->target(), target);

    auto exported = runtime->saveFacingState();
    ASSERT_TRUE(exported);
    EXPECT_EQ(exported->actionId, 1u);
    EXPECT_EQ(exported->groupActionId, 13);
    ASSERT_EQ(exported->parameters.size(), 13);
    std::array<uint32_t, 13> types {2,2,2,3,3,1,2,1,2,2,2,1,1};
    for (size_t i = 0; i < types.size(); ++i) EXPECT_EQ(exported->parameters[i].type, types[i]);
    EXPECT_EQ(std::get<int32_t>(exported->parameters[5].payload), 5);
    EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[8].payload), 30.0f);

    record.parameters[5].payload = int32_t {8};
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[5].payload = int32_t {5};
    record.parameters[9].payload = 1.0f;
    EXPECT_FALSE(record.toRuntimeAction(game));
}

TEST(SavedAction, move_to_location_uses_exact_retail_action_1_shape_in_both_titles) {
    for (GameID id : {GameID::KotOR, GameID::TSL}) {
        TestEngine &engine = testEngine();
        StubConsole console;
        Game game(id, "", engine.options(), engine.services(), console);
        auto area = game.newArea();
        auto location = std::make_shared<Location>(glm::vec3(12.0f, 34.0f, 5.0f), 1.2f);
        MoveToLocationAction::ForcedState state;
        state.areaId = area->id();
        auto runtime = game.newAction<MoveToLocationAction>(
            location, true, false, -1.0f, state);
        SavedActionRecord provenance;
        provenance.groupActionId = 31;
        runtime->attachSavedAction(provenance);

        auto exported = runtime->saveFacingState();

        ASSERT_TRUE(exported);
        EXPECT_EQ(exported->actionId, 1u);
        EXPECT_EQ(exported->groupActionId, 31);
        EXPECT_EQ(exported->declaredParameterCount, 13);
        ASSERT_EQ(exported->parameters.size(), 13);
        std::array<uint32_t, 13> types {2,2,2,3,3,1,2,1,2,2,2,1,1};
        for (size_t i = 0; i < types.size(); ++i) {
            EXPECT_EQ(exported->parameters[i].type, types[i]);
        }
        EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[0].payload), 12.0f);
        EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[1].payload), 34.0f);
        EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[2].payload), 5.0f);
        EXPECT_EQ(std::get<SavedObjectReference>(exported->parameters[3].payload).id,
                  area->id());
        EXPECT_TRUE(std::get<SavedObjectReference>(exported->parameters[4].payload).isInvalid());
        EXPECT_EQ(std::get<int32_t>(exported->parameters[5].payload), 1);
        EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[6].payload), 0.0f);
        EXPECT_EQ(std::get<int32_t>(exported->parameters[7].payload), 0);
    }
}

TEST(SavedAction, move_to_location_imports_ordinary_pending_and_active_forced_timing) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    registerModuleIdentity(game, area->id(), area);

    auto ordinaryRecord = SavedActionRecord::fromGff(
        *moveToPointAction(area->id()), savedRuntimeIdentityContext());
    ASSERT_TRUE(ordinaryRecord.bindObjectReferences(game));
    auto ordinary = std::dynamic_pointer_cast<MoveToLocationAction>(
        ordinaryRecord.toRuntimeAction(game));
    ASSERT_TRUE(ordinary);
    EXPECT_TRUE(ordinary->isRun());
    EXPECT_FALSE(ordinary->isForced());
    EXPECT_EQ(ordinary->destination()->position(), glm::vec3(12.0f, 34.0f, 5.0f));

    auto pendingRecord = SavedActionRecord::fromGff(
        *moveToPointAction(area->id(), glm::vec3(4.0f), 5, 30.0f, 0, 0, 22),
        savedRuntimeIdentityContext());
    ASSERT_TRUE(pendingRecord.bindObjectReferences(game));
    auto pending = std::dynamic_pointer_cast<MoveToLocationAction>(
        pendingRecord.toRuntimeAction(game));
    ASSERT_TRUE(pending);
    EXPECT_TRUE(pending->isForced());
    EXPECT_FALSE(pending->forcedState().active);
    EXPECT_FLOAT_EQ(pending->timeout(), 30.0f);
    EXPECT_EQ(pending->originalSavedAction()->groupActionId, 22);

    auto activeRecord = SavedActionRecord::fromGff(
        *moveToPointAction(area->id(), glm::vec3(8.0f), 1, 0.0f, 7, 12345),
        savedRuntimeIdentityContext());
    ASSERT_TRUE(activeRecord.bindObjectReferences(game));
    auto active = std::dynamic_pointer_cast<MoveToLocationAction>(
        activeRecord.toRuntimeAction(game));
    ASSERT_TRUE(active);
    EXPECT_TRUE(active->isForced());
    EXPECT_TRUE(active->forcedState().active);
    // The retail pair is composed into an absolute deadline on restore.
    EXPECT_EQ(active->forcedState().expiryMilliseconds,
              7ull * game.millisecondsPerWorldDay() + 12345ull);
    auto reexported = active->saveFacingState();
    ASSERT_TRUE(reexported);
    EXPECT_EQ(std::get<int32_t>(reexported->parameters[5].payload), 1);
    EXPECT_FLOAT_EQ(std::get<float>(reexported->parameters[8].payload), 0.0f);
    EXPECT_EQ(std::get<int32_t>(reexported->parameters[11].payload), 7);
    EXPECT_EQ(std::get<int32_t>(reexported->parameters[12].payload), 12345);
}

TEST(SavedAction, move_to_location_rejects_malformed_semantics_and_invalid_area) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    registerModuleIdentity(game, area->id(), area);
    auto record = SavedActionRecord::fromGff(
        *moveToPointAction(area->id()), savedRuntimeIdentityContext());
    ASSERT_TRUE(record.bindObjectReferences(game));

    record.parameters[6].payload = 1.0f;
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[6].payload = 0.0f;
    record.parameters[7].payload = int32_t {1};
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[7].payload = int32_t {0};
    record.parameters[5].payload = int32_t {8};
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.parameters[5].payload = int32_t {1};
    record.parameters[0].payload = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(record.toRuntimeAction(game));

    auto missingArea = SavedActionRecord::fromGff(
        *moveToPointAction(123456), savedRuntimeIdentityContext());
    EXPECT_FALSE(missingArea.bindObjectReferences(game));
    EXPECT_FALSE(missingArea.toRuntimeAction(game));
    auto invalidArea = SavedActionRecord::fromGff(
        *moveToPointAction(kSavedRuntimeInvalidObjectId),
        savedRuntimeIdentityContext());
    ASSERT_TRUE(invalidArea.bindObjectReferences(game));
    EXPECT_FALSE(invalidArea.toRuntimeAction(game));
    record.declaredParameterCount = 12;
    EXPECT_EQ(record.executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);
}

TEST(SavedRuntimePublication, move_to_location_is_inert_and_blocks_later_action) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    registerModuleIdentity(game, area->id(), area);
    auto saved = Gff::Builder()
                     .field(Gff::Field::newList(
                         "ActionList",
                         {moveToPointAction(area->id()),
                          action(30, 10, {valueParameter(
                              2, Gff::Field::newFloat("Value", 2.0f))})}))
                     .build();
    auto actor = game.newCreature();

    actor->deserializeRuntimeState(*saved, savedRuntimeIdentityContext());
    EXPECT_TRUE(actor->actions().empty());
    actor->bindSavedRuntimeState();
    EXPECT_TRUE(actor->actions().empty());
    actor->publishSavedRuntimeState();

    ASSERT_EQ(actor->actions().size(), 2);
    EXPECT_TRUE(std::dynamic_pointer_cast<MoveToLocationAction>(actor->actions()[0]));
    EXPECT_EQ(actor->actions()[1]->type(), ActionType::Wait);
    EXPECT_FALSE(actor->actions()[0]->isCompleted());
    EXPECT_FALSE(actor->actions()[1]->isCompleted());
}

TEST(SavedAction, active_forced_move_preserves_absolute_world_time) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto target = game.newCreature();
    registerModuleIdentity(game, area->id(), area);
    registerModuleIdentity(game, target->id(), target);
    auto saved = action(1, 27, {
        valueParameter(2, Gff::Field::newFloat("Value", 1.0f)), valueParameter(2, Gff::Field::newFloat("Value", 2.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 3.0f)), valueParameter(3, Gff::Field::newDword("Value", area->id())),
        valueParameter(3, Gff::Field::newDword("Value", target->id())), valueParameter(1, Gff::Field::newInt("Value", 1)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.5f)), valueParameter(1, Gff::Field::newInt("Value", 0)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.0f)), valueParameter(2, Gff::Field::newFloat("Value", 0.0f)),
        valueParameter(2, Gff::Field::newFloat("Value", 0.0f)), valueParameter(1, Gff::Field::newInt("Value", 4)),
        valueParameter(1, Gff::Field::newInt("Value", 12345)),
    });
    auto record = SavedActionRecord::fromGff(
        *saved, savedRuntimeIdentityContext());
    ASSERT_TRUE(record.bindObjectReferences(game));
    auto runtime = std::dynamic_pointer_cast<MoveToObjectAction>(record.toRuntimeAction(game));
    ASSERT_TRUE(runtime);
    EXPECT_TRUE(runtime->forcedState().active);
    auto exported = runtime->saveFacingState();
    ASSERT_TRUE(exported);
    EXPECT_EQ(std::get<int32_t>(exported->parameters[5].payload), 1);
    EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[8].payload), 0.0f);
    EXPECT_EQ(std::get<int32_t>(exported->parameters[11].payload), 4);
    EXPECT_EQ(std::get<int32_t>(exported->parameters[12].payload), 12345);
}

TEST(SavedAction, forced_move_deadline_round_trips_across_a_day_boundary) {
    // The runtime holds one absolute deadline; the retail record holds a
    // day/time pair. Splitting on save and composing on load must be lossless
    // even when the deadline sits on the far side of a day boundary.
    for (GameID id : {GameID::KotOR, GameID::TSL}) {
        TestEngine &engine = testEngine();
        StubConsole console;
        Game game(id, "", engine.options(), engine.services(), console);
        auto area = game.newArea();
        auto location = std::make_shared<Location>(glm::vec3(1.0f, 2.0f, 3.0f), 0.0f);

        const uint64_t deadline =
            3ull * game.millisecondsPerWorldDay() + 4567ull;
        MoveToLocationAction::ForcedState state;
        state.areaId = area->id();
        state.active = true;
        state.expiryMilliseconds = deadline;
        auto runtime = game.newAction<MoveToLocationAction>(
            location, false, true, 0.0f, state);

        auto exported = runtime->saveFacingState();
        ASSERT_TRUE(exported);
        // Written records are always normalized.
        EXPECT_EQ(std::get<int32_t>(exported->parameters[11].payload), 3);
        EXPECT_EQ(std::get<int32_t>(exported->parameters[12].payload), 4567);

        ASSERT_TRUE(exported->bindObjectReferences(game));
        auto restored = std::dynamic_pointer_cast<MoveToLocationAction>(
            exported->toRuntimeAction(game));
        ASSERT_TRUE(restored);
        EXPECT_TRUE(restored->forcedState().active);
        EXPECT_EQ(restored->forcedState().expiryMilliseconds, deadline);
    }
}

TEST(SavedAction, forced_move_accepts_an_unnormalized_legacy_time_of_day) {
    // Records written before the day length became Mod_MinPerHour-derived hold
    // a time of day on the old fixed 24-hour scale, which overflows the current
    // day length. That is unnormalized, not invalid: it must still restore.
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    registerModuleIdentity(game, area->id(), area);
    constexpr uint32_t kLegacyTimeOfDay = 24u * 60u * 60u * 1000u - 1u;
    ASSERT_GT(kLegacyTimeOfDay, game.millisecondsPerWorldDay());

    auto record = SavedActionRecord::fromGff(
        *moveToPointAction(area->id(), glm::vec3(8.0f), 1, 0.0f, 2,
                           static_cast<int32_t>(kLegacyTimeOfDay)),
        savedRuntimeIdentityContext());
    ASSERT_TRUE(record.bindObjectReferences(game));
    auto restored = std::dynamic_pointer_cast<MoveToLocationAction>(
        record.toRuntimeAction(game));
    ASSERT_TRUE(restored);
    EXPECT_TRUE(restored->forcedState().active);
    EXPECT_EQ(restored->forcedState().expiryMilliseconds,
              2ull * game.millisecondsPerWorldDay() + kLegacyTimeOfDay);

    // And re-exporting it normalizes the pair.
    auto exported = restored->saveFacingState();
    ASSERT_TRUE(exported);
    EXPECT_GT(std::get<int32_t>(exported->parameters[11].payload), 2);
    EXPECT_LT(static_cast<uint32_t>(std::get<int32_t>(exported->parameters[12].payload)),
              game.millisecondsPerWorldDay());
}

TEST(SavedRuntimePublication, should_compare_event_due_times_on_the_absolute_clock) {
    // An unnormalized record whose Time exceeds one day length is due two days
    // later, not on the next calendar rollover. A lexicographic day/time
    // comparison fires it a whole day early.
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));

    auto ifo = Gff::Builder()
                   .field(Gff::Field::newDword("Mod_CalendarDay", 4))
                   .field(Gff::Field::newDword("Mod_TimeOfDay", 0))
                   .field(Gff::Field::newDword("Mod_MinPerHour", 5))
                   .build();
    game.prepareSavedRuntimeNamespace(
        *ifo, SerializedIdentityContext::moduleGraph("test-module"));
    const uint32_t millisecondsPerDay = game.millisecondsPerWorldDay();
    auto module = game.newModule();
    auto caller = game.newCreature();
    registerModuleIdentity(game, 2, module);
    registerModuleIdentity(game, 3, caller);

    auto queue = Gff::Builder()
                     .field(Gff::Field::newList(
                         "EventQueue",
                         {event(5, 4, 2u * millisecondsPerDay, minimalEffect())}))
                     .build();
    module->deserializeSavedEventQueue(
        *queue, savedRuntimeIdentityContext());
    module->bindSavedEventQueue();
    module->publishSavedEventQueue();
    ASSERT_EQ(module->pendingSavedEventCount(), 1);

    // One day on: a lexicographic comparison would call this due already.
    TestGameModule::setSnapshotWorldTime(game, 5, 0, 5);
    module->dispatchDueSavedEvents();
    EXPECT_EQ(module->pendingSavedEventCount(), 1)
        << "an event two day lengths out must not fire after one";

    TestGameModule::setSnapshotWorldTime(game, 6, 0, 5);
    module->dispatchDueSavedEvents();
    EXPECT_EQ(module->pendingSavedEventCount(), 0);
    EXPECT_EQ(module->effects().size(), 1);
}

TEST(SavedAction, runtime_do_command_exports_retail_action_37_situation) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto program = std::make_shared<script::ScriptProgram>("runtime_do_command");
    program->add(script::Instruction(script::InstructionType::RETN));
    auto state = std::make_shared<script::ExecutionState>();
    state->program = program;
    state->insOffset = 13;
    state->globals = {script::Variable::ofInt(17)};
    state->locals = {script::Variable::ofString("pending")};
    auto context = std::make_shared<script::ExecutionContext>();
    context->savedState = state;
    auto runtime = game.newAction<DoCommandAction>(context);
    SavedActionRecord provenance;
    provenance.groupActionId = 41;
    runtime->attachSavedAction(provenance);

    auto exported = runtime->saveFacingState();

    ASSERT_TRUE(exported);
    EXPECT_EQ(exported->actionId, 37u);
    EXPECT_EQ(exported->groupActionId, 41);
    EXPECT_EQ(exported->declaredParameterCount, 1);
    ASSERT_EQ(exported->parameters.size(), 1);
    EXPECT_EQ(exported->parameters[0].type,
              static_cast<uint32_t>(SavedActionParameterType::ScriptSituation));
    auto &situation = std::get<SerializedScriptSituation>(
        exported->parameters[0].payload);
    EXPECT_EQ(situation.scriptName, "runtime_do_command");
    EXPECT_EQ(situation.instructionPointer, 0);
    EXPECT_EQ(situation.basePointer, 1);
    EXPECT_EQ(situation.stackPointer, 2);
    EXPECT_EQ(situation.stack.size(), 2);
    EXPECT_EQ(exported->executionSupport(), SavedExecutionSupport::Executable);

    exported->declaredParameterCount = 0;
    EXPECT_EQ(exported->executionSupport(),
              SavedExecutionSupport::RepresentableButUnsupported);
    exported->declaredParameterCount = 1;
    exported->parameters[0].type =
        static_cast<uint32_t>(SavedActionParameterType::String);
    EXPECT_EQ(exported->executionSupport(),
              SavedExecutionSupport::RepresentableButUnsupported);
}

TEST(SavedAction, start_conversation_uses_retail_action_24_shape) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    auto runtime = game.newAction<StartConversationAction>(
        target, "meeting", true);
    SavedActionRecord provenance;
    provenance.groupActionId = 12;
    runtime->attachSavedAction(provenance);

    auto exported = runtime->saveFacingState();

    ASSERT_TRUE(exported);
    EXPECT_EQ(exported->actionId, 24u);
    EXPECT_EQ(exported->groupActionId, 12);
    EXPECT_EQ(exported->declaredParameterCount, 3);
    ASSERT_EQ(exported->parameters.size(), 3);
    EXPECT_EQ(exported->parameters[0].type, 3u);
    EXPECT_EQ(exported->parameters[1].type, 4u);
    EXPECT_EQ(exported->parameters[2].type, 1u);
    EXPECT_EQ(std::get<SavedObjectReference>(exported->parameters[0].payload).id,
              target->id());
    EXPECT_EQ(std::get<std::string>(exported->parameters[1].payload), "meeting");
    EXPECT_EQ(std::get<int32_t>(exported->parameters[2].payload), 1);
    ASSERT_TRUE(exported->bindObjectReferences(game));
    auto restored = std::dynamic_pointer_cast<StartConversationAction>(
        exported->toRuntimeAction(game));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->target(), target);
    EXPECT_EQ(restored->dialogResRef(), "meeting");
    EXPECT_TRUE(restored->isPrivateConversation());
    EXPECT_FALSE(restored->isCompleted());
}

TEST(SavedAction, start_conversation_rejects_malformed_or_unbound_records) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto saved = action(24, 7, {
        valueParameter(3, Gff::Field::newDword("Value", 123456)),
        valueParameter(4, Gff::Field::newCExoString("Value", "dialog")),
        valueParameter(1, Gff::Field::newInt("Value", 0)),
    });
    auto record = SavedActionRecord::fromGff(
        *saved, savedRuntimeIdentityContext());
    EXPECT_FALSE(record.bindObjectReferences(game));
    EXPECT_FALSE(record.toRuntimeAction(game));
    record.declaredParameterCount = 2;
    EXPECT_EQ(record.executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);
    record.declaredParameterCount = 3;
    record.parameters[1].type = 1;
    EXPECT_EQ(record.executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);
}

TEST(SavedRuntimePublication, move_to_object_waits_for_publication_and_preserves_queue_order) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    registerModuleIdentity(game, target->id(), target);
    auto saved = Gff::Builder()
                     .field(Gff::Field::newList(
                         "ActionList",
                         {action(17, 21, {
                              valueParameter(3, Gff::Field::newDword("Value", target->id())),
                              valueParameter(1, Gff::Field::newInt("Value", 1)),
                              valueParameter(2, Gff::Field::newFloat("Value", 1.0f)),
                              valueParameter(2, Gff::Field::newFloat("Value", 1.0f)),
                              valueParameter(1, Gff::Field::newInt("Value", 1)),
                          }),
                          action(30, 22, {
                              valueParameter(2, Gff::Field::newFloat("Value", 2.0f)),
                          })}))
                     .build();
    auto actor = game.newCreature();
    actor->deserializeRuntimeState(*saved, savedRuntimeIdentityContext());

    EXPECT_TRUE(actor->actions().empty());
    actor->bindSavedRuntimeState();
    EXPECT_TRUE(actor->actions().empty());
    actor->publishSavedRuntimeState();

    ASSERT_EQ(actor->actions().size(), 2);
    EXPECT_EQ(actor->actions()[0]->type(), ActionType::MoveToObject);
    EXPECT_EQ(actor->actions()[1]->type(), ActionType::Wait);
    EXPECT_FALSE(actor->actions()[0]->isCompleted());
    EXPECT_FALSE(actor->actions()[1]->isCompleted());
}

TEST(SavedRuntimePublication, should_separate_parse_bind_and_idempotent_publication) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));

    auto saved = Gff::Builder()
                     .field(Gff::Field::newList(
                         "EffectList", {minimalEffect()}))
                     .field(Gff::Field::newList(
                         "ActionList",
                         {action(
                              30,
                              7,
                              {valueParameter(
                                  2,
                                  Gff::Field::newFloat("Value", 1.5f))}),
                          action(0xdead, 8)}))
                     .build();
    auto object = game.newCreature();

    object->deserializeRuntimeState(*saved, savedRuntimeIdentityContext());

    ASSERT_EQ(object->savedEffects().size(), 1);
    ASSERT_EQ(object->savedActionQueue().actions.size(), 2);
    EXPECT_TRUE(object->effects().empty());
    EXPECT_TRUE(object->actions().empty());
    EXPECT_FALSE(object->hasPublishedSavedRuntimeState());

    object->bindSavedRuntimeState();
    EXPECT_FALSE(object->hasPublishedSavedRuntimeState());
    object->publishSavedRuntimeState();

    ASSERT_EQ(object->effects().size(), 1);
    EXPECT_EQ(object->effects().front().id, 10);
    EXPECT_FALSE(object->effects().front().effect);
    EXPECT_NO_THROW(object->getAttackBonus());
    EXPECT_NO_THROW(object->getDefense());
    ASSERT_EQ(object->actions().size(), 1);
    EXPECT_EQ(object->actions().front()->type(), ActionType::Wait);
    EXPECT_FALSE(object->actions().front()->isCompleted());
    EXPECT_EQ(
        object->savedActionQueue().actions[1].executionSupport(),
        SavedExecutionSupport::RepresentableButUnsupported);
    EXPECT_TRUE(object->hasPublishedSavedRuntimeState());

    object->publishSavedRuntimeState();
    EXPECT_EQ(object->effects().size(), 1);
    EXPECT_EQ(object->actions().size(), 1);
}

TEST(SavedRuntimePublication, detached_state_never_binds_through_module_graph) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto moduleTarget = game.newCreature();
    registerModuleIdentity(game, 77u, moduleTarget);
    auto effect = minimalEffect();
    replaceSaveField(
        *effect, Gff::Field::newDword("CreatorId", 77u));
    auto record = Gff::Builder()
                      .field(Gff::Field::newList("EffectList", {effect}))
                      .field(Gff::Field::newList(
                          "ActionList", {basicAttackAction(77u)}))
                      .build();
    auto detached = game.newCreature();
    detached->deserializeRuntimeState(
        *record,
        SerializedIdentityContext::detachedRecord("availnpc0.utc"));

    detached->bindSavedRuntimeState();
    detached->publishSavedRuntimeState();

    ASSERT_EQ(detached->savedEffects().size(), 1u);
    EXPECT_FALSE(detached->savedEffects().front().boundCreator());
    EXPECT_TRUE(detached->effects().empty());
    EXPECT_TRUE(detached->actions().empty());
    EXPECT_NE(game.getObjectBySavedId(77u), nullptr);
}

TEST(SavedRuntimePublication, detached_state_binds_only_inside_exact_record) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    const auto context =
        SerializedIdentityContext::detachedRecord("availnpc0.utc");
    auto localTarget = game.newCreature();
    localTarget->assignSerializedObjectIdentity({context, 77u});
    auto effect = minimalEffect();
    replaceSaveField(
        *effect, Gff::Field::newDword("CreatorId", 77u));
    auto record = Gff::Builder()
                      .field(Gff::Field::newList("EffectList", {effect}))
                      .field(Gff::Field::newList(
                          "ActionList", {basicAttackAction(77u)}))
                      .build();
    auto detached = game.newCreature();
    detached->deserializeRuntimeState(*record, context);

    detached->bindSavedRuntimeState();
    detached->publishSavedRuntimeState();

    ASSERT_EQ(detached->effects().size(), 1u);
    EXPECT_EQ(detached->effects().front().boundCreator(), localTarget);
    ASSERT_EQ(detached->actions().size(), 1u);
    auto attack = std::dynamic_pointer_cast<AttackObjectAction>(
        detached->actions().front());
    ASSERT_TRUE(attack);
    EXPECT_EQ(attack->target(), localTarget);
}


TEST(SavedRuntimePublication, should_publish_supported_events_without_dispatching_them) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));

    auto ifo = Gff::Builder()
                   .field(Gff::Field::newDword("Mod_CalendarDay", 4))
                   .field(Gff::Field::newDword("Mod_TimeOfDay", 100))
                   .field(Gff::Field::newDword("Mod_MinPerHour", 5))
                   .build();
    game.prepareSavedRuntimeNamespace(
        *ifo, SerializedIdentityContext::moduleGraph("test-module"));
    auto module = game.newModule();
    auto caller = game.newCreature();
    registerModuleIdentity(game, 2, module);
    registerModuleIdentity(game, 3, caller);

    auto queue = Gff::Builder()
                     .field(Gff::Field::newList(
                         "EventQueue",
                         {event(5, 4, 100, minimalEffect()),
                          event(11, 4, 101),
                          event(18, 4, 102)}))
                     .build();
    module->deserializeSavedEventQueue(
        *queue, savedRuntimeIdentityContext());
    module->bindSavedEventQueue();
    module->publishSavedEventQueue();

    ASSERT_EQ(module->savedEventQueue().events.size(), 3);
    EXPECT_EQ(module->pendingSavedEventCount(), 2);
    EXPECT_TRUE(module->effects().empty());

    module->publishSavedEventQueue();
    EXPECT_EQ(module->pendingSavedEventCount(), 2);
    module->dispatchDueSavedEvents();

    EXPECT_EQ(module->pendingSavedEventCount(), 1);
    ASSERT_EQ(module->effects().size(), 1);
    EXPECT_EQ(module->effects().front().id, 10);
    EXPECT_EQ(
        module->savedEventQueue().events[1].executionSupport(),
        SavedExecutionSupport::RepresentableButUnsupported);
    EXPECT_EQ(
        module->savedEventQueue().events[2].executionSupport(),
        SavedExecutionSupport::RetailDiscards);

    EffectInstance expiring;
    expiring.subType = static_cast<uint16_t>(DurationType::Temporary);
    expiring.expiryDay = 4;
    // Five real seconds after the current world time of 100. World-time
    // milliseconds are real milliseconds, as in CWorldTimer.
    expiring.expiryTime = 5100;
    auto remaining = game.remainingEffectDuration(expiring);
    ASSERT_TRUE(remaining);
    EXPECT_FLOAT_EQ(*remaining, 5.0f);
}


TEST(ScriptSituation, should_preserve_the_retail_snapshot_without_claiming_resume) {
    SerializedScriptSituation situation = SerializedScriptSituation::fromGff(
        *savedSituation(), savedRuntimeIdentityContext());

    EXPECT_EQ(situation.codeSize, 4);
    EXPECT_EQ(situation.code, (ByteBuffer {1, 2, 3, 4}));
    EXPECT_EQ(situation.crc, 0x1234);
    EXPECT_EQ(situation.instructionPointer, 42);
    EXPECT_EQ(situation.secondaryPointer, 3);
    EXPECT_EQ(situation.scriptName, "saved_script");
    EXPECT_EQ(situation.basePointer, 1);
    EXPECT_EQ(situation.stackPointer, 4);
    EXPECT_EQ(situation.totalSize, 8);
    ASSERT_EQ(situation.stack.size(), 4);
    EXPECT_EQ(std::get<int32_t>(situation.stack[0].payload), -7);
    EXPECT_EQ(std::get<SavedObjectReference>(situation.stack[1].payload).id, 2);
    EXPECT_EQ(std::get<SavedLocationValue>(situation.stack[2].payload).position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(std::holds_alternative<UnsupportedSavedPayload>(situation.stack[3].payload));
    ASSERT_EQ(situation.unsupportedFields.size(), 1);
    EXPECT_EQ(situation.unsupportedFields[0].label, "FutureField");
    EXPECT_EQ(situation.resumeSupport(), ScriptSituationResumeSupport::ValidatedImport);
}

TEST(SavedObjectReference, should_bind_only_after_B_exists_and_never_rebind_A_into_B) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));

    auto savedCreature = Gff::Builder()
                             .field(Gff::Field::newDword("ObjectId", 2))
                             .build();
    auto git = Gff::Builder()
                   .field(Gff::Field::newList("Creature List", {savedCreature}))
                   .build();
    const auto identityContext =
        SerializedIdentityContext::moduleGraph("test-module");
    game.reserveSavedObjectIds(
        *git, identityContext, SerializedGraphRoot::AreaGit);

    auto reference = SavedObjectReference::fromSerializedId(
        2, identityContext);
    EXPECT_FALSE(game.bindSavedObjectReference(reference));

    auto sessionA = game.newCreature(*savedCreature, identityContext);
    ASSERT_NE(sessionA->id(), 2);
    ASSERT_TRUE(game.bindSavedObjectReference(reference));
    EXPECT_EQ(reference.boundObject(), sessionA);

    game.retireRuntimeSession();
    game.reserveSavedObjectIds(
        *git, identityContext, SerializedGraphRoot::AreaGit);
    auto sessionB = game.newCreature(*savedCreature, identityContext);
    ASSERT_NE(sessionB->id(), 2);
    EXPECT_FALSE(game.bindSavedObjectReference(reference));
    EXPECT_FALSE(reference.boundObject());
    EXPECT_NE(sessionA, sessionB);

    auto parsedForB = SavedObjectReference::fromSerializedId(
        2, identityContext);
    EXPECT_TRUE(game.bindSavedObjectReference(parsedForB));
    EXPECT_EQ(parsedForB.boundObject(), sessionB);
    auto invalid = SavedObjectReference::fromSerializedId(
        kSavedRuntimeInvalidObjectId, identityContext);
    EXPECT_FALSE(game.bindSavedObjectReference(invalid));
}

TEST(SavedObjectReference, serialized_identity_dies_with_its_module_graph) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));

    const auto identityContext =
        SerializedIdentityContext::moduleGraph("shared-module-name");
    auto savedCreature = Gff::Builder()
                             .field(Gff::Field::newDword("ObjectId", 2))
                             .build();
    auto source = game.newCreature(*savedCreature, identityContext);
    auto reference = SavedObjectReference::fromSerializedId(
        2, identityContext);
    ASSERT_TRUE(game.bindSavedObjectReference(reference));
    ASSERT_EQ(reference.boundObject(), source);

    game.retireActiveModuleRuntime();
    auto destination = game.newCreature(*savedCreature, identityContext);
    ASSERT_NE(source, destination);
    EXPECT_FALSE(game.bindSavedObjectReference(reference));
    EXPECT_FALSE(reference.boundObject());

    auto destinationReference = SavedObjectReference::fromSerializedId(
        2, identityContext);
    EXPECT_TRUE(game.bindSavedObjectReference(destinationReference));
    EXPECT_EQ(destinationReference.boundObject(), destination);
}

TEST(SavedObjectReference, runtime_identity_can_survive_an_ordinary_module_transition) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setActualPlayer(player);
    auto reference = SavedObjectReference::fromRuntimeId(player->id());
    ASSERT_TRUE(game.bindSavedObjectReference(reference));

    game.retireActiveModuleRuntime();

    EXPECT_TRUE(game.bindSavedObjectReference(reference));
    EXPECT_EQ(reference.boundObject(), player);
}

TEST(SavedEventQueue, should_preserve_K1_and_K2_records_absolute_time_payload_and_order) {
    auto root = Gff::Builder()
                    .field(Gff::Field::newList(
                        "EventQueue",
                        {event(1, 4, 100, savedSituation()),
                         event(5, 4, 101, minimalEffect()),
                         event(11, 4, 102),
                         event(18, 4, 103),
                         event(99, 4, 104, Gff::Builder().field(Gff::Field::newInt("Value", 7)).build())}))
                    .build();

    SavedEventQueue queue = SavedEventQueue::fromGff(
        *root, savedRuntimeIdentityContext());

    ASSERT_EQ(queue.events.size(), 5);
    EXPECT_EQ(queue.events[0].eventId, 1);
    EXPECT_EQ(queue.events[0].day, 4);
    EXPECT_EQ(queue.events[0].time, 100);
    EXPECT_EQ(queue.events[0].object.id, 2);
    EXPECT_EQ(queue.events[0].caller.id, 3);
    EXPECT_TRUE(std::holds_alternative<SerializedScriptSituation>(queue.events[0].payload));
    EXPECT_TRUE(std::holds_alternative<EffectInstance>(queue.events[1].payload));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(queue.events[2].payload));
    EXPECT_EQ(queue.events[3].executionSupport(), SavedExecutionSupport::RetailDiscards);
    EXPECT_FALSE(queue.events[3].shouldRestore());
    EXPECT_TRUE(std::holds_alternative<UnsupportedSavedPayload>(queue.events[4].payload));
    EXPECT_FALSE(queue.events[4].shouldRestore());
}

TEST(SavedEventQueue, should_bind_event_references_only_through_current_B_registry) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));

    auto target = game.newCreature();
    auto caller = game.newCreature();
    registerModuleIdentity(game, 2, target);
    registerModuleIdentity(game, 3, caller);
    SavedEventRecord record = SavedEventRecord::fromGff(
        *event(1, 1, 2, savedSituation()), savedRuntimeIdentityContext());

    EXPECT_TRUE(record.bindObjectReferences(game));
    EXPECT_EQ(record.object.boundObject(), target);
    EXPECT_EQ(record.caller.boundObject(), caller);

    game.retireRuntimeSession();
    game.newCreature();
    game.newCreature();
    EXPECT_FALSE(record.bindObjectReferences(game));
    EXPECT_FALSE(record.object.boundObject());
    EXPECT_FALSE(record.caller.boundObject());
}

TEST(SavedEventQueue, should_preserve_observed_spell_body_bag_and_script_event_payloads) {
    auto spell = Gff::Builder()
                     .type(0x6666)
                     .field(Gff::Field::newInt("SpellId", 12))
                     .field(Gff::Field::newDword("CasterId", 2))
                     .field(Gff::Field::newDword("TargetId", 3))
                     .field(Gff::Field::newDword("AreaId", 4))
                     .field(Gff::Field::newDword("ItemId", kSavedRuntimeInvalidObjectId))
                     .field(Gff::Field::newCExoString("Script", "spell_script"))
                     .field(Gff::Field::newFloat("TargetPosX", 1.0f))
                     .field(Gff::Field::newFloat("TargetPosY", 2.0f))
                     .field(Gff::Field::newFloat("TargetPosZ", 3.0f))
                     .field(Gff::Field::newInt("FinalForceCost", 7))
                     .build();
    auto bodyBag = Gff::Builder()
                       .type(0x5555)
                       .field(Gff::Field::newDword("BodyBagId", 5))
                       .field(Gff::Field::newFloat("PositionX", 4.0f))
                       .field(Gff::Field::newFloat("PositionY", 5.0f))
                       .field(Gff::Field::newFloat("PositionZ", 6.0f))
                       .build();
    auto intParam = Gff::Builder().type(0x69).field(Gff::Field::newInt("Parameter", -2)).build();
    auto floatParam = Gff::Builder().type(0x69).field(Gff::Field::newFloat("Parameter", 2.5f)).build();
    auto stringParam = Gff::Builder()
                           .type(0x69)
                           .field(Gff::Field::newCExoString("Parameter", "payload"))
                           .build();
    auto objectParam = Gff::Builder().type(0x69).field(Gff::Field::newDword("Parameter", 6)).build();
    auto scriptEvent = Gff::Builder()
                           .type(0x4444)
                           .field(Gff::Field::newWord("EventType", 9))
                           .field(Gff::Field::newList("IntList", {intParam}))
                           .field(Gff::Field::newList("FloatList", {floatParam}))
                           .field(Gff::Field::newList("StringList", {stringParam}))
                           .field(Gff::Field::newList("ObjectList", {objectParam}))
                           .build();
    auto root = Gff::Builder()
                    .field(Gff::Field::newList(
                        "EventQueue",
                        {event(8, 1, 10, spell), event(17, 1, 11, bodyBag), event(10, 1, 12, scriptEvent)}))
                    .build();

    SavedEventQueue queue = SavedEventQueue::fromGff(
        *root, savedRuntimeIdentityContext());

    ASSERT_EQ(queue.events.size(), 3);
    const auto &savedSpell = std::get<SavedSpellImpact>(queue.events[0].payload);
    EXPECT_EQ(savedSpell.spellId, 12);
    EXPECT_EQ(savedSpell.caster.id, 2);
    EXPECT_EQ(savedSpell.item.id, kSavedRuntimeInvalidObjectId);
    EXPECT_EQ(savedSpell.script, "spell_script");
    EXPECT_EQ(savedSpell.targetPosition, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(savedSpell.finalForceCost, 7);
    const auto &savedBag = std::get<SavedBodyBag>(queue.events[1].payload);
    EXPECT_EQ(savedBag.object.id, 5);
    EXPECT_EQ(savedBag.position, glm::vec3(4.0f, 5.0f, 6.0f));
    const auto &savedScriptEvent = std::get<SavedScriptEvent>(queue.events[2].payload);
    EXPECT_EQ(savedScriptEvent.type, 9);
    EXPECT_EQ(savedScriptEvent.integers, (std::vector<int32_t> {-2}));
    EXPECT_EQ(savedScriptEvent.floats, (std::vector<float> {2.5f}));
    EXPECT_EQ(savedScriptEvent.strings, (std::vector<std::string> {"payload"}));
    ASSERT_EQ(savedScriptEvent.objects.size(), 1);
    EXPECT_EQ(savedScriptEvent.objects[0].id, 6);
}

TEST(SavedEventQueue, recovered_reference_bearing_payloads_bind_explicitly) {
    auto aoo = Gff::Builder().type(0x3333)
                   .field(Gff::Field::newDword("Value", 4)).build();
    auto combat = Gff::Builder().type(0x2222)
                      .field(Gff::Field::newDword("ReactObject", 5))
                      .field(Gff::Field::newDword("AmmoItem", 6))
                      .field(Gff::Field::newInt("AttackType", 17)).build();
    auto feedbackObject1 = Gff::Builder().type(0xbaad)
                               .field(Gff::Field::newDword("ObjectValue", 7)).build();
    auto feedbackObject2 = Gff::Builder().type(0xbaad)
                               .field(Gff::Field::newDword("ObjectValue", 8)).build();
    auto feedback = Gff::Builder().type(0xcccc)
                        .field(Gff::Field::newByte("Type", 9))
                        .field(Gff::Field::newList(
                            "ObjectIDList", {feedbackObject1, feedbackObject2})).build();
    auto spell = Gff::Builder().type(0x6666)
                     .field(Gff::Field::newDword("CasterId", 4))
                     .field(Gff::Field::newDword("TargetId", 5))
                     .field(Gff::Field::newDword("AreaId", 6))
                     .field(Gff::Field::newDword("ItemId", 7)).build();
    auto root = Gff::Builder().field(Gff::Field::newList(
        "EventQueue",
        {event(20, 1, 10, aoo),
         event(15, 1, 11, combat),
         event(21, 1, 12, combat),
         event(22, 1, 13, feedback),
         event(19, 1, 14, spell)})).build();

    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    std::map<uint32_t, std::shared_ptr<Object>> objects;
    for (uint32_t id = 2; id <= 8; ++id) {
        objects.emplace(id, game.newCreature());
        registerModuleIdentity(game, id, objects.at(id));
    }
    auto queue = SavedEventQueue::fromGff(
        *root, savedRuntimeIdentityContext());

    ASSERT_EQ(queue.events.size(), 5u);
    for (auto &record : queue.events) {
        EXPECT_TRUE(record.bindObjectReferences(game));
    }
    const auto &savedAoo = std::get<SavedBroadcastAoo>(
        queue.events[0].payload);
    EXPECT_EQ(savedAoo.target.boundObject(), objects.at(4));
    const auto &melee = std::get<SavedCombatAttack>(
        queue.events[1].payload);
    EXPECT_EQ(melee.reactionObject.boundObject(), objects.at(5));
    EXPECT_EQ(melee.ammoItem.boundObject(), objects.at(6));
    const auto &projectile = std::get<SavedCombatAttack>(
        queue.events[2].payload);
    EXPECT_EQ(projectile.reactionObject.boundObject(), objects.at(5));
    EXPECT_EQ(projectile.ammoItem.boundObject(), objects.at(6));
    const auto &message = std::get<SavedFeedbackMessage>(
        queue.events[3].payload);
    ASSERT_EQ(message.objects.size(), 2u);
    EXPECT_EQ(message.objects[0].boundObject(), objects.at(7));
    EXPECT_EQ(message.objects[1].boundObject(), objects.at(8));
    const auto &itemSpell = std::get<SavedSpellImpact>(
        queue.events[4].payload);
    EXPECT_EQ(itemSpell.caster.boundObject(), objects.at(4));
    EXPECT_EQ(itemSpell.item.boundObject(), objects.at(7));
}

} // namespace
