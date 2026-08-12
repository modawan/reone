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
#include "../fixtures/resource.h"
#include "../fixtures/script.h"

#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/script/runner.h"
#include "reone/game/script/savedsituation.h"
#include "reone/script/format/ncswriter.h"
#include "reone/script/program.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

struct ProgramFixture {
    std::shared_ptr<ScriptProgram> program;
    ByteBuffer headerlessCode;
    uint32_t resumeOffset {13};
};

ProgramFixture continuationProgram() {
    ProgramFixture result;
    result.program = std::make_shared<ScriptProgram>("saved_resume");
    result.program->add(Instruction::newCONSTI(999));
    result.resumeOffset = result.program->length();
    result.program->add(Instruction::newCPTOPBP(-4, 4));
    result.program->add(Instruction::newCPTOPSP(-8, 4));
    result.program->add(Instruction(InstructionType::ADDII));
    result.program->add(Instruction(InstructionType::RETN));

    ByteBuffer ncs;
    NcsWriter(*result.program).save(std::make_shared<MemoryOutputStream>(ncs));
    result.headerlessCode.assign(ncs.begin() + 13, ncs.end());
    return result;
}

ProgramFixture callerProgram() {
    ProgramFixture result;
    result.program = std::make_shared<ScriptProgram>("saved_caller");
    result.program->add(Instruction::newCONSTI(999));
    result.resumeOffset = result.program->length();
    result.program->add(Instruction::newCONSTO(kObjectSelf));
    result.program->add(Instruction::newACTION(0, 1));
    result.program->add(Instruction(InstructionType::RETN));

    ByteBuffer ncs;
    NcsWriter(*result.program).save(std::make_shared<MemoryOutputStream>(ncs));
    result.headerlessCode.assign(ncs.begin() + 13, ncs.end());
    return result;
}

SerializedScriptSituation situationFor(const ProgramFixture &fixture) {
    SerializedScriptSituation result;
    result.codeSize = static_cast<int32_t>(fixture.headerlessCode.size());
    result.code = fixture.headerlessCode;
    result.scriptName = fixture.program->name();
    result.instructionPointer = static_cast<int32_t>(fixture.resumeOffset - 13);
    result.stackSize = 2;
    result.basePointer = 1;
    result.stackPointer = 2;
    result.totalSize = 18;
    result.stack = {
        SavedVmStackValue {
            static_cast<int8_t>(SavedVmStackType::Integer), int32_t {12}},
        SavedVmStackValue {
            static_cast<int8_t>(SavedVmStackType::Integer), int32_t {30}},
    };
    return result;
}

class TestRoutines : public IRoutines {
public:
    TestRoutines() {
        _routines.push_back(std::make_unique<Routine>(
            "CallerEcho",
            VariableType::Int,
            Variable::ofInt(0),
            std::vector<VariableType> {VariableType::Object},
            [](const std::vector<Variable> &args, ExecutionContext &) {
                return Variable::ofInt(static_cast<int32_t>(args[0].objectId));
            }));
    }

    Routine &get(int index) override { return *_routines.at(static_cast<size_t>(index)); }
    int getNumRoutines() const override { return static_cast<int>(_routines.size()); }
    int getIndexByName(const std::string &name) const override {
        return name == "CallerEcho" ? 0 : -1;
    }

private:
    std::vector<std::unique_ptr<Routine>> _routines;
};

class SavedSituationTest : public Test {
protected:
    SavedSituationTest() :
        game(GameID::KotOR, "", engine.options(), engine.services(), console),
        runner(routines, scripts) {
    }

    void SetUp() override {
        ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));
        TestGameModule::setActiveModule(game, true);
        game.openInGame();
    }

    SavedScriptSituationImportResult bindAndImport(SerializedScriptSituation &situation) {
        situation.bindObjectReferences(game);
        return SavedScriptSituationImporter(game, scripts).import(situation);
    }

    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    NiceMock<MockScripts> scripts;
    TestRoutines routines;
    ScriptRunner runner;
};

TEST_F(SavedSituationTest, reconstructs_ordered_globals_locals_and_resumes_at_saved_instruction) {
    auto fixture = continuationProgram();
    auto situation = situationFor(fixture);

    auto imported = bindAndImport(situation);

    ASSERT_TRUE(imported) << imported.message;
    const auto &state = imported.continuation->executionState();
    ASSERT_EQ(state.globals.size(), 1);
    ASSERT_EQ(state.locals.size(), 1);
    EXPECT_EQ(state.globals[0].type, VariableType::Int);
    EXPECT_EQ(state.globals[0].intValue, 12);
    EXPECT_EQ(state.locals[0].intValue, 30);
    EXPECT_EQ(state.insOffset, fixture.resumeOffset);
    EXPECT_EQ(state.program->name(), "saved_resume");

    // The 999 instruction before the saved pointer must not execute. The
    // result is derived only from the preserved global/local values.
    EXPECT_EQ(runner.run(*imported.continuation, game, uint32_t {0}), 42);
}

TEST_F(SavedSituationTest, translates_every_supported_stack_value_without_losing_engine_payloads) {
    auto fixture = continuationProgram();
    auto situation = situationFor(fixture);
    EffectInstance effect;
    effect.id = 77;
    effect.retailType = static_cast<uint16_t>(EffectType::Disease);
    effect.subType = 8;
    effect.creatorId = kSavedRuntimeInvalidObjectId;
    effect.integerParameters = {4, 5, 6};
    SavedScriptEvent event;
    event.type = 9;
    event.integers = {1, 2};
    event.floats = {3.5f};
    event.strings = {"event"};
    event.objects = {SavedObjectReference {kSavedRuntimeInvalidObjectId}};

    situation.stackSize = 8;
    situation.basePointer = 4;
    situation.stackPointer = 8;
    situation.totalSize = 24;
    situation.stack = {
        {static_cast<int8_t>(SavedVmStackType::Integer), int32_t {-7}},
        {static_cast<int8_t>(SavedVmStackType::Float), 2.5f},
        {static_cast<int8_t>(SavedVmStackType::String), std::string("text")},
        {static_cast<int8_t>(SavedVmStackType::Object), SavedObjectReference {kSavedRuntimeInvalidObjectId}},
        {static_cast<int8_t>(SavedVmStackType::Effect), effect},
        {static_cast<int8_t>(SavedVmStackType::Event), event},
        {static_cast<int8_t>(SavedVmStackType::Location),
         SavedLocationValue {glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f)}},
        {static_cast<int8_t>(SavedVmStackType::Object), SavedObjectReference {1234}},
    };

    auto imported = bindAndImport(situation);

    ASSERT_TRUE(imported) << imported.message;
    const auto &state = imported.continuation->executionState();
    ASSERT_EQ(state.globals.size(), 4);
    ASSERT_EQ(state.locals.size(), 4);
    EXPECT_EQ(state.globals[0].intValue, -7);
    EXPECT_FLOAT_EQ(state.globals[1].floatValue, 2.5f);
    EXPECT_EQ(state.globals[2].strValue, "text");
    EXPECT_EQ(state.globals[3].objectId, kObjectInvalid);

    auto savedEffect = std::dynamic_pointer_cast<SavedEffectValue>(state.locals[0].engineType);
    ASSERT_TRUE(savedEffect);
    EXPECT_EQ(savedEffect->instance().id, 77);
    EXPECT_EQ(savedEffect->instance().integerParameters, (std::vector<int32_t> {4, 5, 6}));
    EXPECT_TRUE(game.hasEffectId(77));
    auto runtimeEvent = std::dynamic_pointer_cast<Event>(state.locals[1].engineType);
    ASSERT_TRUE(runtimeEvent);
    EXPECT_EQ(runtimeEvent->number(), 9);
    EXPECT_EQ(runtimeEvent->integers(), (std::vector<int32_t> {1, 2}));
    EXPECT_EQ(runtimeEvent->objects(), (std::vector<uint32_t> {kObjectInvalid}));
    auto location = std::dynamic_pointer_cast<Location>(state.locals[2].engineType);
    ASSERT_TRUE(location);
    EXPECT_EQ(location->position(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(location->facing(), glm::half_pi<float>());
    EXPECT_EQ(state.locals[3].objectId, 1234);
}

TEST_F(SavedSituationTest, saved_effect_vm_value_reuses_effect_instance_when_applied) {
    auto fixture = continuationProgram();
    auto situation = situationFor(fixture);
    EffectInstance effect;
    effect.id = 19160;
    effect.retailType = static_cast<uint16_t>(EffectType::Disease);
    effect.subType = 8;
    effect.creatorId = kSavedRuntimeInvalidObjectId;
    effect.spellId = 321;
    effect.integerParameters = {9, 8, 7};
    situation.stackSize = situation.basePointer = situation.stackPointer = 1;
    situation.totalSize = 17;
    situation.stack = {{static_cast<int8_t>(SavedVmStackType::Effect), effect}};
    auto target = game.newCreature();

    auto imported = bindAndImport(situation);
    ASSERT_TRUE(imported) << imported.message;
    auto value = std::dynamic_pointer_cast<SavedEffectValue>(
        imported.continuation->executionState().globals[0].engineType);
    ASSERT_TRUE(value);

    target->applyEffect(value, DurationType::Permanent, 12.0f);

    ASSERT_EQ(target->effects().size(), 1);
    const auto &applied = target->effects().front();
    EXPECT_EQ(applied.id, 19160);
    EXPECT_EQ(applied.retailType, static_cast<uint16_t>(EffectType::Disease));
    EXPECT_EQ(applied.semanticSubType(), 8);
    EXPECT_EQ(applied.durationType(), DurationType::Permanent);
    EXPECT_FLOAT_EQ(applied.duration, 12.0f);
    EXPECT_EQ(applied.spellId, 321);
    EXPECT_EQ(applied.integerParameters, (std::vector<int32_t> {9, 8, 7}));
    EXPECT_FALSE(applied.effect);
}

TEST_F(SavedSituationTest, binds_self_to_the_current_action_or_event_owner) {
    auto fixture = callerProgram();
    auto situation = situationFor(fixture);
    situation.stackSize = situation.basePointer = situation.stackPointer = situation.totalSize = 0;
    situation.stack.clear();
    auto owner = game.newCreature();

    auto imported = bindAndImport(situation);

    ASSERT_TRUE(imported) << imported.message;
    EXPECT_EQ(runner.run(*imported.continuation, game, owner->id()), owner->id());
}

TEST_F(SavedSituationTest, embedded_code_is_authoritative_over_an_installed_script_revision) {
    auto fixture = continuationProgram();
    auto situation = situationFor(fixture);
    EXPECT_CALL(scripts, get(_)).Times(0);

    auto imported = bindAndImport(situation);

    ASSERT_TRUE(imported) << imported.message;
    EXPECT_EQ(imported.continuation->executionState().program->name(), "saved_resume");
    EXPECT_EQ(runner.run(*imported.continuation, game, uint32_t {0}), 42);
}

TEST_F(SavedSituationTest, absent_embedded_code_falls_back_to_the_named_current_resource) {
    auto fixture = continuationProgram();
    auto situation = situationFor(fixture);
    situation.code.clear();
    situation.codeSize = 0;
    EXPECT_CALL(scripts, get("saved_resume")).WillOnce(Return(fixture.program));

    auto imported = bindAndImport(situation);

    ASSERT_TRUE(imported) << imported.message;
    EXPECT_EQ(imported.continuation->executionState().program, fixture.program);
    EXPECT_EQ(runner.run(*imported.continuation, game, uint32_t {0}), 42);
}

TEST_F(SavedSituationTest, refuses_unbound_crc_secondary_code_pointer_and_stack_corruption) {
    auto fixture = continuationProgram();
    auto base = situationFor(fixture);
    auto unbound = SavedScriptSituationImporter(game, scripts).import(base);
    EXPECT_EQ(unbound.error, SavedScriptSituationImportError::UnboundRuntimeSession);

    auto crc = base;
    crc.crc = 1;
    EXPECT_EQ(bindAndImport(crc).error, SavedScriptSituationImportError::UnsupportedCrc);

    auto secondary = base;
    secondary.secondaryPointer = 1;
    EXPECT_EQ(bindAndImport(secondary).error, SavedScriptSituationImportError::UnsupportedSecondaryPointer);

    auto size = base;
    --size.codeSize;
    EXPECT_EQ(bindAndImport(size).error, SavedScriptSituationImportError::InvalidCode);

    auto truncated = base;
    truncated.code.pop_back();
    truncated.codeSize = static_cast<int32_t>(truncated.code.size());
    EXPECT_EQ(bindAndImport(truncated).error, SavedScriptSituationImportError::InvalidCode);

    auto pointer = base;
    ++pointer.instructionPointer;
    EXPECT_EQ(bindAndImport(pointer).error, SavedScriptSituationImportError::InvalidInstructionPointer);

    auto bounds = base;
    bounds.basePointer = 3;
    EXPECT_EQ(bindAndImport(bounds).error, SavedScriptSituationImportError::InvalidStackBounds);

    auto capacity = base;
    capacity.totalSize = 1;
    EXPECT_EQ(bindAndImport(capacity).error, SavedScriptSituationImportError::InvalidStackBounds);

    auto unsupported = base;
    unsupported.stack[0] = SavedVmStackValue {23, UnsupportedSavedPayload {}};
    EXPECT_EQ(bindAndImport(unsupported).error, SavedScriptSituationImportError::InvalidStackValue);
}

TEST_F(SavedSituationTest, session_retirement_invalidates_imported_and_save_facing_continuations) {
    auto fixture = callerProgram();
    auto situationA = situationFor(fixture);
    situationA.stackSize = situationA.basePointer = situationA.stackPointer = situationA.totalSize = 0;
    situationA.stack.clear();
    auto ownerA = game.newCreature();
    ASSERT_TRUE(situationA.bindObjectReferences(game));
    auto importedA = SavedScriptSituationImporter(game, scripts).import(situationA);
    ASSERT_TRUE(importedA) << importedA.message;
    EXPECT_EQ(runner.run(*importedA.continuation, game, ownerA->id()), ownerA->id());

    game.retireRuntimeSession();
    TestGameModule::setActiveModule(game, true);
    game.openInGame();
    auto ownerB = game.newCreature();
    ASSERT_EQ(ownerB->id(), ownerA->id());

    EXPECT_FALSE(importedA.continuation->isCurrent(game));
    EXPECT_EQ(runner.run(*importedA.continuation, game, ownerB->id()), -1);
    EXPECT_FALSE(situationA.bindObjectReferences(game));
    EXPECT_EQ(
        SavedScriptSituationImporter(game, scripts).import(situationA).error,
        SavedScriptSituationImportError::UnboundRuntimeSession);

    auto situationB = situationFor(fixture);
    situationB.stackSize = situationB.basePointer = situationB.stackPointer = situationB.totalSize = 0;
    situationB.stack.clear();
    auto importedB = bindAndImport(situationB);
    ASSERT_TRUE(importedB) << importedB.message;
    EXPECT_EQ(runner.run(*importedB.continuation, game, ownerB->id()), ownerB->id());
}

TEST_F(SavedSituationTest, execution_waits_for_explicit_playable_session_publication) {
    auto fixture = continuationProgram();
    game.retireRuntimeSession();
    auto situation = situationFor(fixture);
    ASSERT_TRUE(situation.bindObjectReferences(game));
    auto imported = SavedScriptSituationImporter(game, scripts).import(situation);
    ASSERT_TRUE(imported) << imported.message;

    EXPECT_FALSE(game.hasPlayableRuntimeSession());
    EXPECT_EQ(runner.run(*imported.continuation, game, uint32_t {0}), -1);

    TestGameModule::setActiveModule(game, true);
    game.openInGame();
    ASSERT_TRUE(game.hasPlayableRuntimeSession());
    EXPECT_EQ(runner.run(*imported.continuation, game, uint32_t {0}), 42);
}

TEST_F(SavedSituationTest, delay_event_and_do_command_action_payloads_share_the_resume_adapter) {
    auto fixture = continuationProgram();
    auto actionSituation = situationFor(fixture);
    // ActionDoCommand/DoCommand owns the same script-situation wire payload;
    // the broad action restorer decides when the resulting action is run.
    SavedActionRecord action;
    action.actionId = static_cast<uint32_t>(ActionType::DoCommand);
    action.parameters.push_back(SavedActionParameter {
        static_cast<uint32_t>(SavedActionParameterType::ScriptSituation),
        actionSituation});
    ASSERT_TRUE(action.bindObjectReferences(game));
    auto &actionPayload = std::get<SerializedScriptSituation>(action.parameters[0].payload);
    auto importedAction = SavedScriptSituationImporter(game, scripts).import(actionPayload);
    ASSERT_TRUE(importedAction) << importedAction.message;
    EXPECT_EQ(runner.run(*importedAction.continuation, game, uint32_t {0}), 42);
    EXPECT_EQ(action.executionSupport(), SavedExecutionSupport::RepresentableButUnsupported);

    // Retail DelayCommand delivery is a Timed EventQueue situation. Absolute
    // Day/Time scheduling remains outside this explicit import/execution seam.
    SavedEventRecord event;
    event.eventId = static_cast<uint32_t>(SavedEventType::Timed);
    event.object.id = kSavedRuntimeInvalidObjectId;
    event.caller.id = kSavedRuntimeInvalidObjectId;
    event.payload = situationFor(fixture);
    ASSERT_TRUE(event.bindObjectReferences(game));
    auto &eventPayload = std::get<SerializedScriptSituation>(event.payload);
    auto importedEvent = SavedScriptSituationImporter(game, scripts).import(eventPayload);
    ASSERT_TRUE(importedEvent) << importedEvent.message;
    EXPECT_EQ(runner.run(*importedEvent.continuation, game, uint32_t {0}), 42);
}

TEST_F(SavedSituationTest, missing_stack_object_remains_a_safe_raw_identity_for_runtime_resolution) {
    auto fixture = continuationProgram();
    auto situation = situationFor(fixture);
    situation.stack[0] = {
        static_cast<int8_t>(SavedVmStackType::Object), SavedObjectReference {123456}};

    EXPECT_FALSE(situation.bindObjectReferences(game));
    auto imported = SavedScriptSituationImporter(game, scripts).import(situation);

    ASSERT_TRUE(imported) << imported.message;
    EXPECT_EQ(imported.continuation->executionState().globals[0].objectId, 123456);
}

TEST_F(SavedSituationTest, ordinary_script_runner_path_remains_unchanged) {
    auto program = std::make_shared<ScriptProgram>("ordinary");
    program->add(Instruction::newCONSTI(55));
    ON_CALL(scripts, get("ordinary")).WillByDefault(Return(program));

    EXPECT_EQ(runner.run("ordinary", std::vector<Argument> {}), 55);
}

} // namespace
