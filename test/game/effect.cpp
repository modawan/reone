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
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damagereduction.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/invisibility.h"
#include "reone/game/effect/seeinvisible.h"
#include "reone/game/effect/source.h"
#include "reone/game/effect/trueseeing.h"
#include "reone/game/effect/ultravision.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
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

std::shared_ptr<Gff> savedAbilityModifier() {
    return Gff::Builder()
        .type(2)
        .field(Gff::Field::newDword64("Id", 91))
        .field(Gff::Field::newWord(
            "Type", 36))
        .field(Gff::Field::newWord(
            "SubType", static_cast<uint16_t>(DurationType::Permanent)))
        .field(Gff::Field::newDword("CreatorId", kSavedEffectInvalidObjectId))
        .field(Gff::Field::newDword(
            "SpellId", std::numeric_limits<uint32_t>::max()))
        .field(Gff::Field::newInt("NumIntegers", 2))
        .field(Gff::Field::newList(
            "IntList",
            {intValue(static_cast<int>(Ability::Constitution)), intValue(4)}))
        .build();
}

class CountingEffect : public Effect {
public:
    CountingEffect() : Effect(EffectType::Haste) {}

    void applyTo(Object &) override { ++applications; }

    int applications {0};
};

class LifecycleEffect : public Effect {
public:
    LifecycleEffect() : Effect(EffectType::Haste) {}

    bool onApply(Object &object, const EffectInstance &) override {
        visibleDuringApply =
            object.effects().size() == 1 &&
            object.effects().front().effect.get() == this &&
            object.effects().front().hasStableId();
        return true;
    }

    void onRemove(Object &, const EffectInstance &) override { ++removals; }

    bool visibleDuringApply {false};
    int removals {0};
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
    auto object = game.newObject<EffectTestObject>(game, engine.services());
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
    auto object = game.newObject<EffectTestObject>(game, engine.services());
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
    auto object = game.newObject<EffectTestObject>(game, engine.services());
    auto effect = std::make_shared<CountingEffect>();

    object->applyEffect(effect, DurationType::Permanent);
    ASSERT_EQ(object->effects().size(), 1);
    EXPECT_EQ(effect->applications, 1);
    EXPECT_TRUE(object->effects().front().hasStableId());
    EXPECT_EQ(object->effects().front().semanticSubType(), 8);
    EXPECT_EQ(object->effects().front().durationType(), DurationType::Permanent);
    EXPECT_EQ(object->effects().front().effect, effect);
}

TEST(EffectInstance, application_and_removal_use_one_canonical_collection) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = game.newObject<EffectTestObject>(game, engine.services());
    auto effect = std::make_shared<LifecycleEffect>();

    object->applyEffect(effect, DurationType::Permanent);
    ASSERT_EQ(object->effects().size(), 1);
    EXPECT_TRUE(effect->visibleDuringApply);

    object->removeEffect(effect);
    EXPECT_TRUE(object->effects().empty());
    EXPECT_EQ(effect->removals, 1);
}

TEST(CombatEffectSource, independent_effects_use_canonical_effect_ids) {
    EffectInstance first;
    first.id = 10;
    EffectInstance second;
    second.id = 11;
    EffectModifierReducer reducer;

    reducer.addIncrease(getEffectSourceKey(first), 0, 3);
    reducer.addIncrease(getEffectSourceKey(second), 0, 4);

    EXPECT_EQ(reducer.totalIncrease(20), 7);
}

TEST(CombatEffectSource, one_spell_groups_by_canonical_spell_id) {
    EffectInstance weaker;
    weaker.id = 10;
    weaker.spellId = 42;
    EffectInstance stronger;
    stronger.id = 11;
    stronger.spellId = 42;
    EffectModifierReducer reducer;

    reducer.addIncrease(getEffectSourceKey(weaker), 0, 3);
    reducer.addIncrease(getEffectSourceKey(stronger), 0, 5);

    EXPECT_EQ(reducer.totalIncrease(20), 5);
}

TEST(CombatEffectSource, item_grouping_uses_exact_runtime_incarnation) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newObject<EffectTestObject>(game, engine.services());
    auto firstItem = game.newItem();
    auto secondItem = game.newItem();

    auto first = std::make_shared<AttackIncreaseEffect>(3, AttackBonus::Misc);
    first->setSaveFacingCreator(firstItem);
    target->applyEffect(first, DurationType::Permanent);
    auto stronger = std::make_shared<AttackIncreaseEffect>(5, AttackBonus::Misc);
    stronger->setSaveFacingCreator(firstItem);
    target->applyEffect(stronger, DurationType::Permanent);
    auto other = std::make_shared<AttackIncreaseEffect>(4, AttackBonus::Misc);
    other->setSaveFacingCreator(secondItem);
    target->applyEffect(other, DurationType::Permanent);

    EffectModifierReducer reducer;
    for (const EffectInstance &instance : target->effects()) {
        reducer.addIncrease(
            getEffectSourceKey(instance),
            instance.integerParameter(1),
            instance.integerParameter(0));
    }
    EXPECT_EQ(reducer.totalIncrease(20), 9);
    EXPECT_EQ(target->effects()[0].boundCreator(), firstItem);
    EXPECT_NE(
        getEffectSourceKey(target->effects()[0]).value,
        getEffectSourceKey(target->effects()[2]).value);
}

TEST(CombatEffectSource, retired_creator_fails_closed) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newObject<EffectTestObject>(game, engine.services());
    auto item = game.newItem();
    auto effect = std::make_shared<AttackIncreaseEffect>(3, AttackBonus::Misc);
    effect->setSaveFacingCreator(item);
    target->applyEffect(effect, DurationType::Permanent);
    ASSERT_EQ(target->effects().front().boundCreator(), item);

    game.destroyRuntimeObjectGraph(item);

    EXPECT_FALSE(target->effects().front().boundCreator());
    EXPECT_EQ(
        getEffectSourceKey(target->effects().front()).kind,
        EffectSourceKind::Independent);
}

TEST(CombatEffectSource, application_uses_the_canonical_creator_binding) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    auto creator = game.newCreature();
    auto immunity = std::make_shared<ImmunityEffect>(
        ImmunityType::AttackDecrease);
    ASSERT_TRUE(immunity->setVersusRacialType(
        static_cast<int>(RacialType::Unknown)));
    target->applyEffect(immunity, DurationType::Permanent);
    auto penalty = std::make_shared<AttackDecreaseEffect>(
        2, AttackBonus::Misc);
    penalty->setSaveFacingCreator(creator);

    target->applyEffect(penalty, DurationType::Permanent);

    ASSERT_EQ(target->effects().size(), 1);
    EXPECT_EQ(target->effects().front().effect, immunity);
}

TEST(CombatEffectQualifier, canonical_parameters_drive_versus_filtering) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    auto effect = std::make_shared<AttackIncreaseEffect>(3, AttackBonus::Misc);
    ASSERT_TRUE(effect->setVersusRacialType(static_cast<int>(RacialType::Unknown)));
    ASSERT_TRUE(effect->setVersusAlignment(0, static_cast<int>(Alignment::DarkSide)));
    EffectInstance instance = effect->saveFacingInstance();

    EXPECT_TRUE(instance.appliesVersus(target.get()));
    instance.integerParameters[2] = static_cast<int>(RacialType::Human);
    EXPECT_FALSE(instance.appliesVersus(target.get()));
    EXPECT_EQ(instance.retailType, 10);
}

TEST(CombatEffectRestore, saved_modifier_is_queryable_without_parallel_payload) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto creature = game.newCreature();
    EffectInstance instance = EffectInstance::fromGff(
        *savedAbilityModifier(),
        SerializedIdentityContext::moduleGraph("test-module"));

    ASSERT_TRUE(creature->restoreEffect(std::move(instance)));
    ASSERT_TRUE(creature->effects().front().effect);
    EXPECT_EQ(creature->getAbilityEffectModifier(Ability::Constitution), 4);

    EXPECT_EQ(creature->removeEffectsById(91), 1);
    EXPECT_EQ(creature->getAbilityEffectModifier(Ability::Constitution), 0);
}

TEST(CombatEffectRestore, saved_vm_value_reuses_the_canonical_executable_payload) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto creature = game.newCreature();
    EffectInstance instance = EffectInstance::fromGff(
        *savedAbilityModifier(),
        SerializedIdentityContext::moduleGraph("test-module"));
    auto value = std::make_shared<SavedEffectValue>(std::move(instance));

    creature->applyEffect(value, DurationType::Permanent);

    ASSERT_EQ(creature->effects().size(), 1);
    EXPECT_EQ(creature->effects().front().retailType, 36);
    EXPECT_EQ(creature->getAbilityEffectModifier(Ability::Constitution), 4);
}

TEST(CombatEffectRestore, absorption_updates_the_canonical_save_facing_state) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto creature = game.newCreature();
    auto effect = std::make_shared<DamageResistanceEffect>(
        DamageType::Fire, 5, 8);
    creature->applyEffect(effect, DurationType::Permanent);

    DamagePacket first;
    first.add(6, DamageType::Fire);
    first.setDamageFlags(static_cast<int>(DamageType::Fire));
    first.resolve(*creature);

    EXPECT_EQ(first.resolvedDamage(), 1);
    ASSERT_EQ(creature->effects().size(), 1);
    EXPECT_EQ(creature->effects().front().integerParameter(2), 2);
    ASSERT_EQ(creature->saveEffectSnapshot().size(), 1);
    EXPECT_EQ(creature->saveEffectSnapshot().front().integerParameter(2), 2);

    DamagePacket second;
    second.add(3, DamageType::Fire);
    second.setDamageFlags(static_cast<int>(DamageType::Fire));
    second.resolve(*creature);

    EXPECT_EQ(second.resolvedDamage(), 1);
    EXPECT_TRUE(creature->effects().empty());
}

TEST(CombatDifficulty, loads_retail_damage_multipliers_once) {
    NiceMock<MockTwoDAs> twoDas;
    TwoDA::Builder builder;
    builder.columns({"name", "desc", "multiplier"})
        .row({"10", "Easy", "0.5"})
        .row({"11", "Normal", "1.0"})
        .row({"12", "Difficult", "1.5"})
        .row({"13", "Default", ""});
    auto table = std::shared_ptr<TwoDA>(builder.build());
    EXPECT_CALL(twoDas, get("difficultyopt")).WillOnce(Return(table));

    DifficultyOptions options(twoDas);
    options.init();

    EXPECT_FLOAT_EQ(0.5f, options.get(0).damageMultiplier);
    EXPECT_FLOAT_EQ(1.0f, options.get(1).damageMultiplier);
    EXPECT_FLOAT_EQ(1.5f, options.get(2).damageMultiplier);
    EXPECT_FLOAT_EQ(1.0f, options.get(3).damageMultiplier);
}

TEST(CombatDifficulty, scales_only_party_damage_and_uses_the_scaled_amount) {
    TestEngine engine;
    engine.init();
    engine.options().game.clientDifficulty = 0;
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto partyTarget = game.newCreature();
    auto nonPartyTarget = game.newCreature();
    ASSERT_TRUE(game.party().addMember(kNpcPlayer, partyTarget));
    game.party().setPlayer(partyTarget);

    EXPECT_EQ(4, game.scaleDamageForDifficulty(9, *partyTarget));
    EXPECT_EQ(9, game.scaleDamageForDifficulty(9, *nonPartyTarget));
    EXPECT_EQ(0, game.scaleDamageForDifficulty(0, *partyTarget));

    partyTarget->setCurrentHitPoints(20);
    auto damage = game.newEffect<DamageEffect>(
        9, DamageType::Universal, DamagePower::Normal);
    partyTarget->applyEffect(damage, DurationType::Instant);
    EXPECT_EQ(16, partyTarget->currentHitPoints());

    engine.options().game.clientDifficulty = 2;
    EXPECT_EQ(13, game.scaleDamageForDifficulty(9, *partyTarget));
}

TEST(CompositeDamage, single_type_mitigation_is_unchanged) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->applyEffect(
        std::make_shared<DamageResistanceEffect>(DamageType::Fire, 5, 0),
        DurationType::Permanent);

    DamagePacket damage;
    damage.add(12, DamageType::Fire);
    damage.setDamageFlags(static_cast<int>(DamageType::Fire));
    damage.resolve(*target);

    EXPECT_EQ(7, damage.resolvedDamage());
}

TEST(CompositeDamage, resistance_applies_once_to_the_aggregate_packet) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->applyEffect(
        std::make_shared<DamageResistanceEffect>(DamageType::Fire, 5, 0),
        DurationType::Permanent);

    DamagePacket damage;
    damage.add(6, DamageType::Slashing);
    damage.add(6, DamageType::Fire);
    damage.setDamageFlags(
        static_cast<int>(DamageType::Slashing) |
        static_cast<int>(DamageType::Fire));
    damage.resolve(*target);

    EXPECT_EQ(7, damage.resolvedDamage());
}

TEST(CompositeDamage, least_immunity_governs_the_aggregate_packet) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->applyEffect(
        std::make_shared<DamageImmunityIncreaseEffect>(DamageType::Fire, 50),
        DurationType::Permanent);
    target->applyEffect(
        std::make_shared<DamageImmunityIncreaseEffect>(DamageType::Electrical, 25),
        DurationType::Permanent);

    DamagePacket damage;
    damage.add(6, DamageType::Fire);
    damage.add(6, DamageType::Electrical);
    damage.setDamageFlags(
        static_cast<int>(DamageType::Fire) |
        static_cast<int>(DamageType::Electrical));
    damage.resolve(*target);

    EXPECT_EQ(9, damage.resolvedDamage());
}

TEST(CompositeDamage, physical_component_enables_one_aggregate_reduction) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->applyEffect(
        std::make_shared<DamageReductionEffect>(
            5, DamagePower::PlusOne, 0),
        DurationType::Permanent);

    DamagePacket damage;
    damage.add(6, DamageType::Slashing);
    damage.add(6, DamageType::Fire);
    damage.setDamageFlags(
        static_cast<int>(DamageType::Slashing) |
        static_cast<int>(DamageType::Fire));
    damage.resolve(*target);

    EXPECT_EQ(7, damage.resolvedDamage());
}

TEST(CompositeDamage, universal_is_an_ordinary_damage_bit_in_a_packet) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->applyEffect(
        std::make_shared<DamageImmunityIncreaseEffect>(
            DamageType::Universal, 100),
        DurationType::Permanent);

    DamagePacket universal;
    universal.add(5, DamageType::Universal);
    universal.setDamageFlags(static_cast<int>(DamageType::Universal));
    universal.resolve(*target);
    EXPECT_EQ(0, universal.resolvedDamage());

    DamagePacket mixed;
    mixed.add(0, DamageType::Electrical);
    mixed.add(5, DamageType::Universal);
    mixed.add(5, DamageType::Fire);
    mixed.setDamageFlags(
        static_cast<int>(DamageType::Universal) |
        static_cast<int>(DamageType::Fire));
    mixed.resolve(*target);
    EXPECT_EQ(10, mixed.resolvedDamage());
}

TEST(CompositeDamage, mitigation_clamps_at_zero) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto target = game.newCreature();
    target->applyEffect(
        std::make_shared<DamageResistanceEffect>(DamageType::Fire, 20, 0),
        DurationType::Permanent);

    DamagePacket damage;
    damage.add(5, DamageType::Fire);
    damage.setDamageFlags(static_cast<int>(DamageType::Fire));
    damage.resolve(*target);
    EXPECT_EQ(0, damage.resolvedDamage());
}

TEST(CombatVisibility, see_invisible_and_ultravision_counter_distinct_types) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto observer = game.newCreature();
    auto normalTarget = game.newCreature();
    auto darknessTarget = game.newCreature();
    normalTarget->applyEffect(
        std::make_shared<InvisibilityEffect>(InvisibilityType::Normal),
        DurationType::Permanent);
    darknessTarget->applyEffect(
        std::make_shared<InvisibilityEffect>(InvisibilityType::Darkness),
        DurationType::Permanent);

    EXPECT_TRUE(normalTarget->isInvisibleTo(*observer));
    EXPECT_TRUE(darknessTarget->isInvisibleTo(*observer));

    auto seeInvisible = std::make_shared<SeeInvisibleEffect>();
    observer->applyEffect(seeInvisible, DurationType::Permanent);
    EXPECT_FALSE(normalTarget->isInvisibleTo(*observer));
    EXPECT_TRUE(darknessTarget->isInvisibleTo(*observer));
    observer->removeEffect(seeInvisible);

    auto ultravision = std::make_shared<UltravisionEffect>();
    observer->applyEffect(ultravision, DurationType::Permanent);
    EXPECT_TRUE(normalTarget->isInvisibleTo(*observer));
    EXPECT_FALSE(darknessTarget->isInvisibleTo(*observer));
    observer->removeEffect(ultravision);
    EXPECT_TRUE(darknessTarget->isInvisibleTo(*observer));
}

TEST(CombatVisibility, unseen_and_invisible_attackers_remove_dexterity_and_dodge_defense) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto defender = game.newCreature();
    defender->attributes().setAbilityScore(Ability::Dexterity, 18);
    defender->applyEffect(
        std::make_shared<ACIncreaseEffect>(
            3, ACBonus::Dodge, kAllDamageTypeFlags),
        DurationType::Permanent);
    defender->applyEffect(
        std::make_shared<ACIncreaseEffect>(
            2, ACBonus::Deflection, kAllDamageTypeFlags),
        DurationType::Permanent);

    defender->setObjectSeen(attacker, true);
    EXPECT_EQ(19, defender->getDefense(attacker.get(), 0));

    auto invisibility = std::make_shared<InvisibilityEffect>(
        InvisibilityType::Normal);
    attacker->applyEffect(invisibility, DurationType::Permanent);
    EXPECT_EQ(12, defender->getDefense(attacker.get(), 0));
    attacker->removeEffect(invisibility);
    EXPECT_EQ(19, defender->getDefense(attacker.get(), 0));

    defender->setObjectSeen(attacker, false);
    EXPECT_EQ(12, defender->getDefense(attacker.get(), 0));

    defender->setObjectSeen(attacker, true);
    game.destroyRuntimeObjectGraph(attacker);
    ASSERT_FALSE(attacker->isRuntimeLive());
    EXPECT_EQ(12, defender->getDefense(attacker.get(), 0));
}

TEST(CombatVisibility, one_true_seeing_effect_counters_both_invisibility_families) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto observer = game.newCreature();
    auto normalTarget = game.newCreature();
    auto darknessTarget = game.newCreature();
    normalTarget->applyEffect(
        std::make_shared<InvisibilityEffect>(InvisibilityType::Improved),
        DurationType::Permanent);
    darknessTarget->applyEffect(
        std::make_shared<InvisibilityEffect>(InvisibilityType::Darkness),
        DurationType::Permanent);
    auto trueSeeing = std::make_shared<TrueSeeingEffect>();

    observer->applyEffect(trueSeeing, DurationType::Permanent);
    EXPECT_FALSE(normalTarget->isInvisibleTo(*observer));
    EXPECT_FALSE(darknessTarget->isInvisibleTo(*observer));

    observer->removeEffect(trueSeeing);
    EXPECT_TRUE(normalTarget->isInvisibleTo(*observer));
    EXPECT_TRUE(darknessTarget->isInvisibleTo(*observer));
}

TEST(CombatVisibility, stacked_true_seeing_preserves_the_native_removal_quirk) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto observer = game.newCreature();
    auto normalTarget = game.newCreature();
    auto darknessTarget = game.newCreature();
    normalTarget->applyEffect(
        std::make_shared<InvisibilityEffect>(InvisibilityType::Normal),
        DurationType::Permanent);
    darknessTarget->applyEffect(
        std::make_shared<InvisibilityEffect>(InvisibilityType::Darkness),
        DurationType::Permanent);
    auto first = std::make_shared<TrueSeeingEffect>();
    auto second = std::make_shared<TrueSeeingEffect>();
    observer->applyEffect(first, DurationType::Permanent);
    observer->applyEffect(second, DurationType::Permanent);

    observer->removeEffect(first);

    // Native K2 clears bit 4 and sets bit 2 when another type-72 effect
    // remains: ordinary invisibility returns, while Darkness stays visible.
    EXPECT_TRUE(normalTarget->isInvisibleTo(*observer));
    EXPECT_FALSE(darknessTarget->isInvisibleTo(*observer));
}

TEST(CombatVisibility, saved_true_seeing_rebuilds_executable_capability) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto observer = game.newCreature();
    auto target = game.newCreature();
    target->applyEffect(
        std::make_shared<InvisibilityEffect>(InvisibilityType::Normal),
        DurationType::Permanent);

    auto saved = Gff::Builder()
                     .type(2)
                     .field(Gff::Field::newDword64("Id", 701))
                     .field(Gff::Field::newWord("Type", 72))
                     .field(Gff::Field::newWord(
                         "SubType",
                         static_cast<uint16_t>(DurationType::Permanent)))
                     .field(Gff::Field::newDword(
                         "CreatorId", kSavedEffectInvalidObjectId))
                     .field(Gff::Field::newInt("NumIntegers", 0))
                     .build();
    EffectInstance restored = EffectInstance::fromGff(
        *saved,
        SerializedIdentityContext::moduleGraph("visibility-test"));

    ASSERT_TRUE(observer->restoreEffect(std::move(restored)));
    ASSERT_TRUE(observer->effects().front().effect);
    EXPECT_FALSE(target->isInvisibleTo(*observer));
    EXPECT_EQ(1u, observer->removeEffectsById(701));
    EXPECT_TRUE(target->isInvisibleTo(*observer));
}

TEST(EffectInstance, should_preserve_ordinary_instant_effect_application) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = game.newObject<EffectTestObject>(game, engine.services());
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
    auto object = game.newObject<EffectTestObject>(game, engine.services());
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
    auto object = game.newObject<EffectTestObject>(game, engine.services());

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
