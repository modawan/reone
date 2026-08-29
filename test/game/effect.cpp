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

#include "reone/game/effect.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/resource/gff.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

std::shared_ptr<Gff> intValue(int value) {
    return Gff::Builder().type(3).field(Gff::Field::newInt("Value", value)).build();
}

std::shared_ptr<Gff> floatValue(float value) {
    return Gff::Builder().type(4).field(Gff::Field::newFloat("Value", value)).build();
}

std::shared_ptr<Gff> stringValue(std::string value) {
    return Gff::Builder().type(5).field(Gff::Field::newCExoString("Value", std::move(value))).build();
}

std::shared_ptr<Gff> objectValue(uint32_t value) {
    return Gff::Builder().type(6).field(Gff::Field::newDword("Value", value)).build();
}

std::shared_ptr<Gff> richSavedEffect(uint16_t subType = 0x11, bool skipOnLoad = false) {
    std::vector<std::shared_ptr<Gff>> integers;
    for (int value : {6, -2, 42, 0, 8, 9, 10, 11}) {
        integers.push_back(intValue(value));
    }
    std::vector<std::shared_ptr<Gff>> floats;
    for (float value : {1.25f, 2.5f, 3.75f, 5.0f}) {
        floats.push_back(floatValue(value));
    }
    std::vector<std::shared_ptr<Gff>> strings;
    for (std::string value : {"alpha", "beta", "gamma", "delta", "epsilon", "zeta"}) {
        strings.push_back(stringValue(std::move(value)));
    }
    std::vector<std::shared_ptr<Gff>> objects;
    for (uint32_t value : {2u, 77u, kSavedEffectInvalidObjectId, 99u}) {
        objects.push_back(objectValue(value));
    }

    return Gff::Builder()
        .type(2)
        .field(Gff::Field::newDword64("Id", 0x100000002ULL))
        .field(Gff::Field::newWord("Type", 68))
        .field(Gff::Field::newWord("SubType", subType))
        .field(Gff::Field::newFloat("Duration", 12.5f))
        .field(Gff::Field::newByte("SkipOnLoad", skipOnLoad ? 1 : 0))
        .field(Gff::Field::newDword("ExpireDay", 123))
        .field(Gff::Field::newDword("ExpireTime", 456))
        .field(Gff::Field::newDword("CreatorId", 77))
        .field(Gff::Field::newDword("SpellId", 88))
        .field(Gff::Field::newInt("IsExposed", 1))
        .field(Gff::Field::newInt("NumIntegers", static_cast<int>(integers.size())))
        .field(Gff::Field::newList("IntList", std::move(integers)))
        .field(Gff::Field::newList("FloatList", std::move(floats)))
        .field(Gff::Field::newList("StringList", std::move(strings)))
        .field(Gff::Field::newList("ObjectList", std::move(objects)))
        .build();
}

EffectInstance parsedSavedEffect(
    uint16_t subType = 0x11,
    bool skipOnLoad = false) {
    return EffectInstance::fromGff(
        *richSavedEffect(subType, skipOnLoad),
        SerializedIdentityContext::moduleGraph("test-module"));
}

class CountingEffect : public Effect {
public:
    CountingEffect() : Effect(EffectType::Haste) {}

    void applyTo(Object &) override { ++applications; }

    int applications {0};
};

class EffectTestObject : public Object {
public:
    EffectTestObject(uint32_t id, Game &game, ServicesView &services) :
        Object(id, ObjectType::Creature, kSceneMain, game, services) {
    }

    void tickEffects(float dt) { updateEffects(dt); }
};

TEST(SavedEffect, should_preserve_the_complete_observed_payload) {
    EffectInstance effect = parsedSavedEffect();

    EXPECT_EQ(effect.id, 0x100000002ULL);
    EXPECT_EQ(effect.retailType, 68);
    EXPECT_EQ(effect.subType, 0x11);
    EXPECT_EQ(effect.durationType(), DurationType::Temporary);
    EXPECT_EQ(effect.semanticSubType(), 0x10);
    EXPECT_FLOAT_EQ(effect.duration, 12.5f);
    EXPECT_FALSE(effect.remainingDuration);
    EXPECT_EQ(effect.expiryDay, 123);
    EXPECT_EQ(effect.expiryTime, 456);
    EXPECT_EQ(effect.creatorId, 77);
    EXPECT_EQ(effect.spellId, 88);
    EXPECT_EQ(effect.exposed, 1);
    EXPECT_FALSE(effect.skipOnLoad);
    EXPECT_EQ(effect.integerParameters, (std::vector<int32_t> {6, -2, 42, 0, 8, 9, 10, 11}));
    EXPECT_EQ(effect.floatParameters, (std::array<float, 4> {1.25f, 2.5f, 3.75f, 5.0f}));
    EXPECT_EQ(effect.stringParameters[0], "alpha");
    EXPECT_EQ(effect.stringParameters[5], "zeta");
    EXPECT_EQ(effect.objectParameters[0], 2);
    EXPECT_EQ(effect.objectParameters[1], 77);
    EXPECT_EQ(effect.objectParameters[2], kSavedEffectInvalidObjectId);
    EXPECT_EQ(effect.objectParameters[3], 99);
}

TEST(SavedEffect, should_decode_all_directly_proven_duration_types_from_subtype) {
    EXPECT_EQ(parsedSavedEffect(0).durationType(), DurationType::Instant);
    EXPECT_EQ(parsedSavedEffect(1).durationType(), DurationType::Temporary);
    EXPECT_EQ(parsedSavedEffect(2).durationType(), DurationType::Permanent);
    EXPECT_EQ(parsedSavedEffect(3).durationType(), DurationType::Equipped);
    EXPECT_EQ(parsedSavedEffect(4).durationType(), DurationType::Innate);
    EXPECT_EQ(parsedSavedEffect(7).durationType(), DurationType::Invalid);
}

TEST(SavedEffect, should_follow_direct_skip_and_equipped_load_policy) {
    EXPECT_FALSE(parsedSavedEffect(1).skipOnLoad);
    EXPECT_TRUE(parsedSavedEffect(1).shouldRestoreOnLoad());
    EXPECT_FALSE(parsedSavedEffect(3).shouldRestoreOnLoad());
    EXPECT_FALSE(parsedSavedEffect(1, true).shouldRestoreOnLoad());
}

TEST(EffectIdNamespace, should_import_authoritative_cursor_and_skip_imported_ids) {
    EffectIdNamespace ids;
    EXPECT_EQ(ids.importId(10), EffectIdImportResult::Imported);
    EXPECT_EQ(ids.importId(12), EffectIdImportResult::Imported);
    ASSERT_TRUE(ids.setNextId(10));
    EXPECT_EQ(ids.allocate(), 11);
    EXPECT_EQ(ids.allocate(), 13);
}

TEST(EffectIdNamespace, should_treat_duplicate_ids_as_an_existing_linkage_group) {
    EffectIdNamespace ids;
    EXPECT_EQ(ids.importId(42), EffectIdImportResult::Imported);
    EXPECT_EQ(ids.importId(42), EffectIdImportResult::Existing);
    EXPECT_EQ(ids.size(), 1);
}

TEST(EffectIdNamespace, should_only_treat_directly_proven_zero_as_unassigned) {
    EffectIdNamespace ids;
    EXPECT_EQ(ids.importId(kUnassignedEffectId), EffectIdImportResult::Unassigned);
    EXPECT_EQ(ids.importId(std::numeric_limits<EffectId>::max()), EffectIdImportResult::Imported);
    EXPECT_FALSE(ids.setNextId(kUnassignedEffectId));
    EXPECT_FALSE(ids.setNextId(std::numeric_limits<EffectId>::max()));
    EXPECT_EQ(ids.nextId(), EffectIdNamespace::kFirstId);
}

TEST(EffectIdNamespace, should_reset_all_session_identity) {
    EffectIdNamespace ids;
    EXPECT_EQ(ids.allocate(), 1);
    EXPECT_EQ(ids.importId(50), EffectIdImportResult::Imported);
    ids.reset();
    EXPECT_EQ(ids.nextId(), 1);
    EXPECT_EQ(ids.size(), 0);
    EXPECT_EQ(ids.allocate(), 1);
}

TEST(SavedEffect, should_not_rebind_serialized_objects_across_saved_graphs) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_))
        .WillByDefault(ReturnRef(sceneGraph));
    auto creator = game.newCreature();

    EffectInstance effect = parsedSavedEffect();
    effect.creatorId = creator->id();
    effect.objectParameters.fill(kSavedEffectInvalidObjectId);
    game.registerSavedObjectIdentity(
        effect.creatorId,
        creator,
        SerializedIdentityContext::moduleGraph("test-module"));
    EXPECT_TRUE(game.bindEffectCreator(effect));
    EXPECT_EQ(effect.boundCreator(), creator);

    game.retireActiveModuleRuntime();

    EXPECT_FALSE(game.bindEffectCreator(effect));
    EXPECT_FALSE(effect.boundCreator());

    auto replacement = game.newCreature();
    game.registerSavedObjectIdentity(
        effect.creatorId,
        replacement,
        SerializedIdentityContext::moduleGraph("test-module"));
    EXPECT_FALSE(game.bindEffectCreator(effect));
    EXPECT_FALSE(effect.boundCreator());

    auto replacementEffect = parsedSavedEffect();
    replacementEffect.creatorId = effect.creatorId;
    replacementEffect.objectParameters.fill(kSavedEffectInvalidObjectId);
    EXPECT_TRUE(game.bindEffectCreator(replacementEffect));
    EXPECT_EQ(replacementEffect.boundCreator(), replacement);
}

TEST(SavedEffect, linked_vm_value_matches_retails_flat_inert_encoding) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto linked = std::make_shared<Effect>(EffectType::LinkEffects);
    linked->captureSaveFacingScriptArguments(
        {script::Variable::ofEffect(
             std::make_shared<Effect>(EffectType::Haste)),
         script::Variable::ofEffect(
             std::make_shared<Effect>(EffectType::Slow))},
        game);

    const auto saved = linked->saveFacingInstance();

    EXPECT_EQ(saved.retailType, static_cast<uint16_t>(EffectType::LinkEffects));
    EXPECT_TRUE(saved.integerParameters.empty());
    EXPECT_EQ(saved.creatorId, kSavedEffectInvalidObjectId);
}

TEST(EffectInstance, should_keep_unsupported_saved_effects_in_the_runtime_collection) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<EffectTestObject>(2, game, engine.services());
    EffectInstance effect = parsedSavedEffect();
    effect.id = 41;

    EXPECT_TRUE(object->restoreEffect(std::move(effect)));
    ASSERT_EQ(object->effects().size(), 1);
    EXPECT_EQ(object->effects().front().retailType, 68);
    EXPECT_FALSE(object->effects().front().effect);
}

TEST(EffectInstance, should_remove_every_member_of_a_shared_id_group) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<EffectTestObject>(2, game, engine.services());
    EffectInstance left = parsedSavedEffect(2);
    EffectInstance right = parsedSavedEffect(2);
    left.id = right.id = 77;
    EffectInstance unrelated = parsedSavedEffect(2);
    unrelated.id = 78;
    ASSERT_TRUE(object->restoreEffect(std::move(left)));
    ASSERT_TRUE(object->restoreEffect(std::move(right)));
    ASSERT_TRUE(object->restoreEffect(std::move(unrelated)));

    EXPECT_EQ(object->removeEffectsById(77), 2);
    EXPECT_EQ(object->removeEffectsById(999), 0);
    ASSERT_EQ(object->effects().size(), 1);
    EXPECT_EQ(object->effects().front().id, 78);
}

TEST(EffectInstance, should_preserve_ordinary_runtime_effect_application) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<EffectTestObject>(2, game, engine.services());
    auto effect = std::make_shared<CountingEffect>();

    object->applyEffect(effect, DurationType::Permanent);
    ASSERT_EQ(object->effects().size(), 1);
    EXPECT_EQ(effect->applications, 1);
    EXPECT_TRUE(object->effects().front().hasStableId());
    EXPECT_EQ(object->effects().front().durationType(), DurationType::Permanent);
    EXPECT_EQ(object->effects().front().effect, effect);
}

TEST(EffectInstance, should_preserve_ordinary_instant_effect_application) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<EffectTestObject>(2, game, engine.services());
    auto effect = std::make_shared<CountingEffect>();

    object->applyEffect(effect, DurationType::Instant);
    EXPECT_EQ(effect->applications, 1);
    EXPECT_TRUE(object->effects().empty());
    EXPECT_EQ(game.nextEffectId(), 2);
}

TEST(EffectInstance, should_expire_temporary_runtime_effects) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<EffectTestObject>(2, game, engine.services());
    auto effect = std::make_shared<CountingEffect>();

    object->applyEffect(effect, DurationType::Temporary, 0.5f);
    object->tickEffects(0.25f);
    ASSERT_EQ(object->effects().size(), 1);
    EXPECT_FLOAT_EQ(object->effects().front().duration, 0.5f);
    EXPECT_FLOAT_EQ(*object->effects().front().remainingDuration, 0.25f);
    object->tickEffects(0.25f);
    EXPECT_TRUE(object->effects().empty());
}

TEST(EffectInstance, should_not_restore_skipped_or_equipped_records) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<EffectTestObject>(2, game, engine.services());

    EXPECT_FALSE(object->restoreEffect(parsedSavedEffect(1, true)));
    EXPECT_FALSE(object->restoreEffect(parsedSavedEffect(3)));
    EXPECT_TRUE(object->effects().empty());
}

TEST(EffectInstance, retirement_should_reset_effect_identity_ownership) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_))
        .WillByDefault(ReturnRef(sceneGraph));
    EXPECT_EQ(game.allocateEffectId(), 1);
    EXPECT_EQ(game.importEffectId(40), EffectIdImportResult::Imported);

    game.retireRuntimeSession();

    EXPECT_EQ(game.nextEffectId(), 1);
    EXPECT_EQ(game.effectIdCount(), 0);
    EXPECT_EQ(game.allocateEffectId(), 1);
}

} // namespace
