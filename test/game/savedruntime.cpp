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

#include "reone/game/action.h"
#include "reone/game/game.h"
#include "reone/game/savedruntime.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

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
        .field(Gff::Field::newDword("CreatorId", 3))
        .build();
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

    SavedActionQueue queue = SavedActionQueue::fromGff(*root);

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
        *action(30, 7, {valueParameter(2, Gff::Field::newFloat("Value", 1.5f))}));
    auto unknown = SavedActionRecord::fromGff(*action(0xdead, 8));

    auto runtime = wait.toRuntimeAction(game);
    ASSERT_TRUE(runtime);
    EXPECT_EQ(runtime->type(), ActionType::Wait);
    EXPECT_EQ(wait.executionSupport(), SavedExecutionSupport::Executable);
    EXPECT_FALSE(unknown.toRuntimeAction(game));
    EXPECT_EQ(unknown.executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);
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

    object->deserializeRuntimeState(*saved);

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
    game.prepareSavedRuntimeNamespace(*ifo);
    auto module = game.newModule();

    auto queue = Gff::Builder()
                     .field(Gff::Field::newList(
                         "EventQueue",
                         {event(5, 4, 100, minimalEffect()),
                          event(11, 4, 101),
                          event(18, 4, 102)}))
                     .build();
    module->deserializeSavedEventQueue(*queue);
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
    expiring.expiryTime = 60100;
    auto remaining = game.remainingEffectDuration(expiring);
    ASSERT_TRUE(remaining);
    EXPECT_FLOAT_EQ(*remaining, 5.0f);
}


TEST(ScriptSituation, should_preserve_the_retail_snapshot_without_claiming_resume) {
    SerializedScriptSituation situation = SerializedScriptSituation::fromGff(*savedSituation());

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

    SavedObjectReference reference(2);
    EXPECT_FALSE(game.bindSavedObjectReference(reference));

    auto sessionA = game.newCreature();
    ASSERT_EQ(sessionA->id(), 2);
    ASSERT_TRUE(game.bindSavedObjectReference(reference));
    EXPECT_EQ(reference.boundObject(), sessionA);

    game.retireRuntimeSession();
    auto sessionB = game.newCreature();
    ASSERT_EQ(sessionB->id(), 2);
    EXPECT_FALSE(game.bindSavedObjectReference(reference));
    EXPECT_FALSE(reference.boundObject());
    EXPECT_NE(sessionA, sessionB);

    SavedObjectReference parsedForB(2);
    EXPECT_TRUE(game.bindSavedObjectReference(parsedForB));
    EXPECT_EQ(parsedForB.boundObject(), sessionB);
    SavedObjectReference invalid(kSavedRuntimeInvalidObjectId);
    EXPECT_FALSE(game.bindSavedObjectReference(invalid));
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

    SavedEventQueue queue = SavedEventQueue::fromGff(*root);

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
    SavedEventRecord record = SavedEventRecord::fromGff(*event(1, 1, 2, savedSituation()));

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

    SavedEventQueue queue = SavedEventQueue::fromGff(*root);

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

} // namespace
