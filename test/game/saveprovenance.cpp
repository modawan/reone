/* Copyright (c) 2026 The reone project contributors */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/action/wait.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/saveprovenance.h"
#include "reone/resource/gff.h"

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
    loaded->captureSaveRecord(*saved);
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
    item->captureOwnerLocalSaveRecord(
        *saved, {SaveRecordOriginKind::ContainedItem, "owner-a"});

    EXPECT_EQ(item->originalOwnerLocalObjectId(), std::optional<uint32_t>(700));
    EXPECT_EQ(game.getObjectById(runtimeId), item);
    EXPECT_FALSE(game.getObjectById(700));
    EXPECT_EQ(game.newItem()->id(), runtimeId + 1);

    auto ownerA = game.newCreature();
    auto ownerB = game.newCreature();
    ownerA->addItem(item);
    bool last = false;
    EXPECT_TRUE(ownerA->removeItem(item, last));
    ownerB->addItem(item);
    EXPECT_EQ(item->originalOwnerLocalObjectId(), std::optional<uint32_t>(700));
    EXPECT_NE(item->id(), 700u);

    auto fresh = game.newItem();
    EXPECT_FALSE(fresh->originalOwnerLocalObjectId());
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
    game.prepareSavedRuntimeNamespace(*clock);
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
