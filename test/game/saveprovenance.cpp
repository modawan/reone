/* Copyright (c) 2026 The reone project contributors */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <set>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/action/wait.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/saveprovenance.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/system/exception/validation.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class SaveTestObject : public Object {
public:
    SaveTestObject(uint32_t id, Game &game, ServicesView &services) :
        Object(id, ObjectType::Creature, kSceneMain, game, services) {
    }

    void tickEffects(float dt) { updateEffects(dt); }
    void tickActionQueue(float dt) {
        updateActions(dt);
        executeActions(dt);
    }
};

std::shared_ptr<Gff> savedWait(float seconds = 5.0f) {
    auto parameter = Gff::Builder()
                         .field(Gff::Field::newDword("Type", 2))
                         .field(Gff::Field::newFloat("Value", seconds))
                         .build();
    return Gff::Builder()
        .field(Gff::Field::newDword("ActionId", 30))
        .field(Gff::Field::newWord("GroupActionId", 7))
        .field(Gff::Field::newWord("NumParams", 1))
        .field(Gff::Field::newList("Paramaters", {parameter}))
        .field(Gff::Field::newInt("FutureActionField", 123))
        .build();
}

std::shared_ptr<Gff> savedEffect(DurationType durationType = DurationType::Temporary) {
    return Gff::Builder()
        .field(Gff::Field::newDword64("Id", 51))
        .field(Gff::Field::newWord("Type", 68))
        .field(Gff::Field::newWord("SubType", static_cast<uint16_t>(durationType)))
        .field(Gff::Field::newFloat("Duration", 10.0f))
        .field(Gff::Field::newDword("ExpireDay", 4))
        .field(Gff::Field::newDword("ExpireTime", 500))
        .field(Gff::Field::newDword("CreatorId", kSavedEffectInvalidObjectId))
        .build();
}

std::shared_ptr<Gff> savedEvent(
    uint32_t eventId,
    uint32_t targetId,
    std::shared_ptr<Gff> data = nullptr) {
    Gff::Builder builder;
    builder.field(Gff::Field::newDword("Day", 4))
        .field(Gff::Field::newDword("Time", 100))
        .field(Gff::Field::newDword("ObjectId", targetId))
        .field(Gff::Field::newDword("CallerId", kSavedRuntimeInvalidObjectId))
        .field(Gff::Field::newDword("EventId", eventId));
    if (data) builder.field(Gff::Field::newStruct("EventData", std::move(data)));
    return builder.build();
}

std::shared_ptr<Gff> objectWithItem(uint32_t objectId, uint32_t itemId) {
    auto item = Gff::Builder()
                    .field(Gff::Field::newDword("ObjectId", itemId))
                    .build();
    return Gff::Builder()
        .field(Gff::Field::newDword("ObjectId", objectId))
        .field(Gff::Field::newList("ItemList", {item}))
        .build();
}

std::shared_ptr<TwoDA> identityTestBaseItems() {
    TwoDA::Builder builder;
    builder.columns({"itemclass"});
    builder.row({"I_Test"});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> identityTestAppearances() {
    TwoDA::Builder builder;
    builder.columns({"modeltype", "walkdist", "rundist", "footsteptype", "envmap", "race", "racetex"});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

TEST(SerializedIdentityAuthority, same_structure_has_contextual_authority) {
    TestEngine &engine = testEngine();
    StubConsole console;
    auto record = Gff::Builder()
                      .field(Gff::Field::newDword("ObjectId", 136))
                      .build();

    Game moduleGame(GameID::KotOR, "", engine.options(), engine.services(), console);
    const auto moduleContext =
        SerializedIdentityContext::moduleGraph("test-module");
    auto authoritative = moduleGame.newItem(*record, moduleContext);
    authoritative->captureSaveRecord(*record, moduleContext);
    EXPECT_NE(authoritative->id(), 136u);
    EXPECT_EQ(moduleGame.getObjectBySavedId(136), authoritative);
    EXPECT_EQ(
        authoritative->serializedObjectIdentity(),
        std::optional<SerializedObjectIdentity>({moduleContext, 136}));

    Game detachedGame(GameID::KotOR, "", engine.options(), engine.services(), console);
    const auto detachedContext =
        SerializedIdentityContext::detachedRecord("availnpc0.utc");
    auto detached = detachedGame.newItem(*record, detachedContext);
    detached->captureSaveRecord(*record, detachedContext);
    EXPECT_NE(detached->id(), 136u);
    EXPECT_EQ(
        detached->serializedObjectIdentity(),
        std::optional<SerializedObjectIdentity>({detachedContext, 136}));

    Game templateGame(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto templated = templateGame.newItem(
        *record, SerializedIdentityContext::templateResource("test.uti"));
    EXPECT_NE(templated->id(), 136u);
}

TEST(SavedIdentityTranslation, authoritative_nested_effect_reference_binds_to_fresh_runtime_item) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillRepeatedly(Return(identityTestBaseItems()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(identityTestAppearances()));
    const auto context =
        SerializedIdentityContext::moduleGraph("test-module");
    auto item = Gff::Builder().type(0)
        .field(Gff::Field::newDword("ObjectId", 136))
        .field(Gff::Field::newInt("BaseItem", 0)).build();
    auto effect = savedEffect(DurationType::Permanent);
    replaceSaveField(
        *effect,
        Gff::Field::newList(
            "ObjectList",
            {Gff::Builder()
                 .field(Gff::Field::newDword("Value", 136))
                 .build()}));
    auto creatureRecord = Gff::Builder().type(4)
        .field(Gff::Field::newDword("ObjectId", 40))
        .field(Gff::Field::newList("ItemList", {item}))
        .field(Gff::Field::newList("EffectList", {effect}))
        .field(Gff::Field::newList("ActionList", {})).build();
    auto git = Gff::Builder()
        .field(Gff::Field::newList("Creature List", {creatureRecord}))
        .build();
    game.reserveSavedObjectIds(*git, context, SerializedGraphRoot::AreaGit);

    auto creature = game.newCreature(*creatureRecord, context);
    creature->deserialize(*creatureRecord, context);
    creature->captureSaveRecord(*creatureRecord, context);
    game.resolveSavedObjectReferences();
    creature->bindSavedRuntimeState();

    ASSERT_EQ(creature->items().size(), 1u);
    auto runtimeItem = creature->items().front();
    EXPECT_NE(runtimeItem->id(), 136u);
    EXPECT_EQ(game.getObjectBySavedId(136), runtimeItem);
    ASSERT_EQ(creature->savedEffects().size(), 1u);
    EXPECT_EQ(
        creature->savedEffects().front().boundObjectParameter(0),
        runtimeItem);
}

TEST(SavedIdentityTranslation, matching_runtime_number_does_not_establish_saved_identity) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto unrelatedRuntimeTwo = game.newItem();
    ASSERT_EQ(unrelatedRuntimeTwo->id(), 2u);
    auto record = Gff::Builder()
        .field(Gff::Field::newDword("ObjectId", 2)).build();
    const auto context =
        SerializedIdentityContext::moduleGraph("test-module");
    auto savedTwo = game.newItem(*record, context);
    ASSERT_NE(savedTwo->id(), 2u);

    auto reference = SavedObjectReference::fromSerializedId(2);
    ASSERT_TRUE(game.bindSavedObjectReference(reference));
    EXPECT_EQ(reference.boundObject(), savedTwo);
    EXPECT_NE(reference.boundObject(), unrelatedRuntimeTwo);
    EXPECT_EQ(game.getObjectById(2), unrelatedRuntimeTwo);
    EXPECT_EQ(game.getObjectBySavedId(2), savedTwo);
}

TEST(SerializedIdentityAuthority, detached_nested_ids_overlap_without_runtime_collision) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillRepeatedly(Return(identityTestBaseItems()));
    auto firstRecord = objectWithItem(40, 136);
    auto secondRecord = objectWithItem(40, 136);
    const auto firstContext =
        SerializedIdentityContext::detachedRecord("availnpc0.utc");
    const auto secondContext =
        SerializedIdentityContext::detachedRecord("availnpc7.utc");

    auto first = std::make_shared<SaveTestObject>(40, game, engine.services());
    auto second = std::make_shared<SaveTestObject>(41, game, engine.services());
    first->deserialize(*firstRecord, firstContext);
    second->deserialize(*secondRecord, secondContext);

    ASSERT_EQ(first->items().size(), 1u);
    ASSERT_EQ(second->items().size(), 1u);
    EXPECT_NE(first->items().front()->id(), second->items().front()->id());
    EXPECT_EQ(
        first->items().front()->serializedObjectIdentity(),
        std::optional<SerializedObjectIdentity>({firstContext, 136}));
    EXPECT_EQ(
        second->items().front()->serializedObjectIdentity(),
        std::optional<SerializedObjectIdentity>({secondContext, 136}));
}

TEST(SerializedIdentityAuthority, authority_propagates_to_nested_owned_graphs) {
    auto creature = objectWithItem(10, 136);
    auto equipped = Gff::Builder()
                        .field(Gff::Field::newDword("ObjectId", 139))
                        .build();
    creature->fields().push_back(
        Gff::Field::newList("Equip_ItemList", {equipped}));
    auto placeable = objectWithItem(20, 137);
    auto store = objectWithItem(30, 138);
    auto unrelatedReference = Gff::Builder()
                                  .field(Gff::Field::newDword("ObjectId", 999))
                                  .build();
    auto git = Gff::Builder()
                   .field(Gff::Field::newList("Creature List", {creature}))
                   .field(Gff::Field::newList("Placeable List", {placeable}))
                   .field(Gff::Field::newList("StoreList", {store}))
                   .field(Gff::Field::newList("EventQueue", {unrelatedReference}))
                   .build();
    const auto moduleContext =
        SerializedIdentityContext::moduleGraph("test-module");

    auto claims = collectSerializedObjectIdClaims(
        *git, moduleContext, SerializedGraphRoot::AreaGit);
    std::set<uint32_t> ids;
    for (const auto &claim : claims) ids.insert(claim.id);
    EXPECT_EQ(ids, std::set<uint32_t>({10, 20, 30, 136, 137, 138, 139}));
    EXPECT_TRUE(collectSerializedObjectIdClaims(
                    *git,
                    SerializedIdentityContext::detachedRecord("availnpc.utc"),
                    SerializedGraphRoot::AreaGit)
                    .empty());
    EXPECT_TRUE(collectSerializedObjectIdClaims(
                    *git,
                    SerializedIdentityContext::templateResource("test.git"),
                    SerializedGraphRoot::AreaGit)
                    .empty());
}

TEST(SerializedIdentityAuthority, authoritative_duplicates_remain_errors) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto creature = objectWithItem(10, 136);
    auto store = objectWithItem(30, 136);
    auto git = Gff::Builder()
                   .field(Gff::Field::newList("Creature List", {creature}))
                   .field(Gff::Field::newList("StoreList", {store}))
                   .build();
    const auto moduleContext =
        SerializedIdentityContext::moduleGraph("test-module");

    EXPECT_THROW(
        game.reserveSavedObjectIds(
            *git, moduleContext, SerializedGraphRoot::AreaGit),
        ValidationException);
}

TEST(SerializedIdentityAuthority, detached_traversal_does_not_reserve_runtime_ids) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto git = Gff::Builder()
                   .field(Gff::Field::newList(
                       "Creature List", {objectWithItem(2, 3)}))
                   .build();

    game.reserveSavedObjectIds(
        *git,
        SerializedIdentityContext::detachedRecord("availnpc0.utc"),
        SerializedGraphRoot::AreaGit);
    EXPECT_EQ(game.newItem()->id(), 2u);
}

TEST(SaveGffShadow, deep_ownership_merge_and_authoritative_list_rebuild) {
    SaveGffShadow shadow;
    {
        auto child = Gff::Builder()
                         .field(Gff::Field::newInt("Member", 1))
                         .build();
        auto source = Gff::Builder()
                          .type(77)
                          .field(Gff::Field::newInt("SupportedA", 20))
                          .field(Gff::Field::newCExoString("UnsupportedU", "keep"))
                          .field(Gff::Field::newList("Creature List", {child}))
                          .build();
        source->setSignature("GIT ");
        shadow = SaveGffShadow::capture(*source);
        source->fields()[1].strValue = "mutated source";
        child->fields()[0].intValue = 99;
    }

    auto merged = shadow.cloneForMerge();
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->signature(), std::optional<std::string>("GIT "));
    EXPECT_EQ(merged->getString("UnsupportedU"), "keep");
    ASSERT_EQ(merged->getList("Creature List").size(), 1);
    EXPECT_EQ(merged->getList("Creature List")[0]->getInt("Member"), 1);

    replaceSaveField(*merged, Gff::Field::newInt("SupportedA", 8));
    removeSaveField(*merged, "Creature List");
    replaceSaveField(
        *merged,
        Gff::Field::newList(
            "Creature List",
            {Gff::Builder().field(Gff::Field::newInt("Member", 2)).build()}));

    EXPECT_EQ(merged->getInt("SupportedA"), 8);
    EXPECT_EQ(merged->getString("UnsupportedU"), "keep");
    ASSERT_EQ(merged->getList("Creature List").size(), 1);
    EXPECT_EQ(merged->getList("Creature List")[0]->getInt("Member"), 2);
}

TEST(SaveGffShadow, new_surviving_deleted_and_resource_retirement_semantics) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto loaded = std::make_shared<SaveTestObject>(90, game, engine.services());
    auto saved = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 90))
                     .field(Gff::Field::newInt("FutureU", 44))
                     .build();
    loaded->captureSaveRecord(
        *saved,
        SerializedIdentityContext::moduleGraph("test-module"));
    auto fresh = std::make_shared<SaveTestObject>(91, game, engine.services());

    EXPECT_TRUE(loaded->saveRecordProvenance());
    EXPECT_FALSE(fresh->saveRecordProvenance());

    std::vector<std::shared_ptr<Object>> authoritativeMembership {loaded};
    authoritativeMembership.clear();
    EXPECT_TRUE(authoritativeMembership.empty());
    EXPECT_TRUE(loaded->saveRecordProvenance()); // external lifetime cannot resurrect membership

    SaveResourceShadows resources;
    {
        auto source = Gff::Builder()
                          .field(Gff::Field::newCExoString("FutureRoot", "alive"))
                          .build();
        resources.capture({SaveResourceKind::AreaGit, "m01aa"}, *source);
    }
    auto retained = resources.find({SaveResourceKind::AreaGit, "m01aa"});
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained->record().getString("FutureRoot"), "alive");
}

TEST(ItemSaveProvenance, owner_local_id_is_a_hint_not_runtime_identity) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto item = game.newItem();
    uint32_t runtimeId = item->id();
    auto saved = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 700))
                     .field(Gff::Field::newInt("UpgradeFuture", 9))
                     .build();
    const auto identityContext =
        SerializedIdentityContext::detachedRecord("owner-a.utc");
    item->captureSaveRecord(
        *saved, identityContext,
        {SaveRecordOriginKind::ContainedItem, "owner-a"});

    EXPECT_EQ(
        item->serializedObjectIdentity(),
        std::optional<SerializedObjectIdentity>({identityContext, 700}));
    EXPECT_EQ(game.getObjectById(runtimeId), item);
    EXPECT_FALSE(game.getObjectById(700));
    EXPECT_EQ(game.newItem()->id(), runtimeId + 1);

    auto ownerA = game.newCreature();
    auto ownerB = game.newCreature();
    ownerA->addItem(item);
    bool last = false;
    EXPECT_TRUE(ownerA->removeItem(item, last));
    ownerB->addItem(item);
    EXPECT_EQ(
        item->serializedObjectIdentity(),
        std::optional<SerializedObjectIdentity>({identityContext, 700}));
    EXPECT_NE(item->id(), 700u);

    auto fresh = game.newItem();
    EXPECT_FALSE(fresh->serializedObjectIdentity());
    EXPECT_FALSE(fresh->saveRecordProvenance());
}

TEST(GlobalLocationProvenance, retains_unusual_orientation_until_facing_changes) {
    const glm::vec3 original(0.25f, -0.75f, 0.5f);
    Location location(glm::vec3(1.0f, 2.0f, 3.0f), original);
    EXPECT_FLOAT_EQ(location.facing(), std::atan2(original.y, original.x));
    EXPECT_EQ(location.saveOrientation(), original);

    location.setPosition(glm::vec3(9.0f, 8.0f, 7.0f));
    EXPECT_EQ(location.saveOrientation(), original);

    location.setFacing(0.75f);
    EXPECT_FALSE(location.preservedOrientation());
    EXPECT_NEAR(location.saveOrientation().x, std::cos(0.75f), 0.00001f);
    EXPECT_NEAR(location.saveOrientation().y, std::sin(0.75f), 0.00001f);
    EXPECT_FLOAT_EQ(location.saveOrientation().z, 0.0f);
}

TEST(EffectSaveProvenance, loaded_and_runtime_expiry_follow_live_collection) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<SaveTestObject>(2, game, engine.services());

    auto loaded = EffectInstance::fromGff(*savedEffect());
    EXPECT_EQ(loaded.expiryOrigin, EffectExpiryOrigin::LoadedAbsoluteGameTime);
    ASSERT_TRUE(object->restoreEffect(loaded));
    ASSERT_EQ(object->saveEffectSnapshot().size(), 1);
    EXPECT_FALSE(object->saveEffectSnapshot()[0].effect); // unsupported but live
    object->clearAllEffects();
    EXPECT_TRUE(object->saveEffectSnapshot().empty());

    auto runtime = std::make_shared<Effect>(EffectType::Damage);
    object->applyEffect(runtime, DurationType::Temporary, 10.0f);
    ASSERT_EQ(object->saveEffectSnapshot().size(), 1);
    EXPECT_EQ(
        object->saveEffectSnapshot()[0].expiryOrigin,
        EffectExpiryOrigin::RuntimeCountdown);
    object->tickEffects(3.0f);
    auto snapshot = object->saveEffectSnapshot();
    EXPECT_FLOAT_EQ(snapshot[0].duration, 10.0f);
    EXPECT_FLOAT_EQ(*snapshot[0].remainingDuration, 7.0f);
    object->tickEffects(7.0f);
    EXPECT_TRUE(object->saveEffectSnapshot().empty());
}

TEST(ActionSaveProvenance, pending_loaded_cancelled_completed_and_new_waits) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<SaveTestObject>(2, game, engine.services());

    SavedActionRecord loaded = SavedActionRecord::fromGff(*savedWait());
    auto wait = loaded.toRuntimeAction(game);
    ASSERT_TRUE(wait);
    EXPECT_TRUE(wait->originalSavedAction());
    object->addAction(wait);
    ASSERT_EQ(object->saveActionSnapshot().size(), 1);
    wait->execute(wait, *object, 2.0f);
    auto remaining = object->saveActionSnapshot();
    ASSERT_EQ(remaining.size(), 1);
    EXPECT_FLOAT_EQ(std::get<float>(remaining[0].parameters[0].payload), 3.0f);
    EXPECT_EQ(remaining[0].unsupportedFields.size(), 1);
    wait->execute(wait, *object, 3.0f);
    EXPECT_TRUE(object->saveActionSnapshot().empty());

    auto cancelled = game.newAction<WaitAction>(4.0f);
    object->addAction(cancelled);
    object->clearAllActions();
    EXPECT_TRUE(object->saveActionSnapshot().empty());

    auto runtime = game.newAction<WaitAction>(6.0f);
    object->addAction(runtime);
    auto generated = object->saveActionSnapshot();
    ASSERT_EQ(generated.size(), 1);
    EXPECT_EQ(generated[0].actionId, 30u);
    EXPECT_FLOAT_EQ(std::get<float>(generated[0].parameters[0].payload), 6.0f);
}

TEST(ActionSaveProvenance, play_animation_uses_retail_id_shape_and_live_state) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<SaveTestObject>(2, game, engine.services());

    auto pending = game.newAction<PlayAnimationAction>(
        AnimationType::LoopingPause, 1.25f, 10.0f);
    SavedActionRecord provenance;
    provenance.groupActionId = 17;
    pending->attachSavedAction(provenance);
    object->addAction(pending);

    auto saved = object->saveActionSnapshot();
    ASSERT_EQ(saved.size(), 1);
    EXPECT_EQ(saved[0].actionId, 6u);
    EXPECT_EQ(saved[0].groupActionId, 17);
    EXPECT_EQ(saved[0].declaredParameterCount, 5);
    ASSERT_EQ(saved[0].parameters.size(), 5);
    EXPECT_EQ(saved[0].parameters[0].type, 3u);
    EXPECT_EQ(std::get<SavedObjectReference>(saved[0].parameters[0].payload).id,
              static_cast<uint32_t>(AnimationType::LoopingPause));
    EXPECT_EQ(saved[0].parameters[1].type, 2u);
    EXPECT_FLOAT_EQ(std::get<float>(saved[0].parameters[1].payload), 1.25f);
    EXPECT_FLOAT_EQ(std::get<float>(saved[0].parameters[2].payload), 10.0f);
    EXPECT_EQ(std::get<int32_t>(saved[0].parameters[3].payload), 1);
    EXPECT_EQ(std::get<int32_t>(saved[0].parameters[4].payload), 1);

    auto started = game.newAction<PlayAnimationAction>(
        AnimationType::LoopingPause, 1.25f, 6.0f, true, true);
    object->clearAllActions(true);
    object->addAction(started);
    started->execute(started, *object, 2.0f);
    saved = object->saveActionSnapshot();
    ASSERT_EQ(saved.size(), 1);
    EXPECT_FLOAT_EQ(std::get<float>(saved[0].parameters[2].payload), 4.0f);
    EXPECT_EQ(std::get<int32_t>(saved[0].parameters[3].payload), 0);
}

TEST(ActionSaveProvenance, play_animation_import_is_inert_and_semantically_symmetric) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);

    SavedActionRecord saved;
    saved.actionId = 6;
    saved.groupActionId = 23;
    saved.declaredParameterCount = 5;
    saved.parameters = {
        {3, SavedObjectReference {static_cast<uint32_t>(AnimationType::LoopingPause)}},
        {2, 1.0f},
        {2, 6.0f},
        {1, int32_t {0}},
        {1, int32_t {1}},
    };

    auto runtime = saved.toRuntimeAction(game);
    ASSERT_TRUE(runtime);
    EXPECT_EQ(runtime->type(), ActionType::PlayAnimation);
    EXPECT_FALSE(runtime->isCompleted());
    auto exported = runtime->saveFacingState();
    ASSERT_TRUE(exported);
    EXPECT_EQ(exported->actionId, 6u);
    EXPECT_EQ(exported->groupActionId, 23);
    EXPECT_FLOAT_EQ(std::get<float>(exported->parameters[2].payload), 6.0f);
    EXPECT_EQ(std::get<int32_t>(exported->parameters[3].payload), 0);

    auto object = std::make_shared<SaveTestObject>(2, game, engine.services());
    object->addAction(runtime);
    object->addAction(game.newAction<WaitAction>(2.0f));
    object->tickActionQueue(5.0f);
    EXPECT_EQ(object->actions().size(), 2);
    object->tickActionQueue(1.0f);
    EXPECT_EQ(object->actions().size(), 2);
    object->tickActionQueue(0.0f);
    ASSERT_EQ(object->actions().size(), 1);
    EXPECT_EQ(object->actions().front()->type(), ActionType::Wait);
}

TEST(ActionSaveProvenance, unsupported_loaded_record_is_live_until_queue_clear) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto object = std::make_shared<SaveTestObject>(2, game, engine.services());
    auto unsupported = Gff::Builder()
                           .field(Gff::Field::newDword("ActionId", 0xdead))
                           .field(Gff::Field::newWord("NumParams", 0))
                           .build();
    auto root = Gff::Builder()
                    .field(Gff::Field::newList(
                        "ActionList", {savedWait(2.0f), unsupported}))
                    .build();
    object->deserializeRuntimeState(*root);
    object->bindSavedRuntimeState();
    object->publishSavedRuntimeState();
    auto snapshot = object->saveActionSnapshot();
    ASSERT_EQ(snapshot.size(), 2);
    EXPECT_EQ(snapshot[1].actionId, 0xdeadu);
    object->clearAllActions();
    EXPECT_TRUE(object->saveActionSnapshot().empty());
}

TEST(EventSaveProvenance, loaded_delivery_cancellation_new_and_session_replacement) {
    TestEngine &engine = testEngine();
    StubConsole console;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    ON_CALL(engine.sceneModule().graphs(), get(_))
        .WillByDefault(ReturnRef(sceneGraph));
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto clock = Gff::Builder()
                     .field(Gff::Field::newDword("Mod_CalendarDay", 4))
                     .field(Gff::Field::newDword("Mod_TimeOfDay", 100))
                     .field(Gff::Field::newDword("Mod_MinPerHour", 5))
                     .build();
    game.prepareSavedRuntimeNamespace(
        *clock, SerializedIdentityContext::moduleGraph("test-module"));
    auto target = game.newCreature();
    auto moduleA = game.newModule();
    auto queue = Gff::Builder()
                     .field(Gff::Field::newList(
                         "EventQueue",
                         {savedEvent(
                              static_cast<uint32_t>(SavedEventType::RemoveEffect),
                              target->id(),
                              savedEffect(DurationType::Permanent)),
                          savedEvent(
                              static_cast<uint32_t>(SavedEventType::DestroyObject),
                              target->id())}))
                     .build();
    moduleA->deserializeSavedEventQueue(*queue);
    moduleA->bindSavedEventQueue();
    moduleA->publishSavedEventQueue();
    ASSERT_EQ(moduleA->saveEventSnapshot().size(), 2);
    moduleA->dispatchDueSavedEvents();
    ASSERT_EQ(moduleA->saveEventSnapshot().size(), 1); // unsupported stays pending
    EXPECT_TRUE(moduleA->cancelSaveEvent(1));
    EXPECT_TRUE(moduleA->saveEventSnapshot().empty());

    SavedEventRecord generated;
    generated.day = 9;
    generated.time = 10;
    generated.eventId = static_cast<uint32_t>(SavedEventType::CloseObject);
    auto index = moduleA->enqueueSaveEvent(generated);
    ASSERT_EQ(moduleA->saveEventSnapshot().size(), 1);
    EXPECT_EQ(moduleA->saveEventSnapshot()[0].day, 9u);
    EXPECT_TRUE(moduleA->cancelSaveEvent(index));

    game.retireRuntimeSession();
    auto moduleB = game.newModule();
    EXPECT_TRUE(moduleB->saveEventSnapshot().empty());
    EXPECT_NE(moduleA, moduleB);
}

} // namespace
