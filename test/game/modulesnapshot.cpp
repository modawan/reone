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

#include "reone/game/action/wait.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/docommand.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/effect.h"
#include "reone/game/game.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera/static.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/sound.h"
#include "reone/game/object/store.h"
#include "reone/game/object/trigger.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/script/savedsituation.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/gffreader.h"
#include "reone/script/program.h"
#include "reone/script/executioncontext.h"
#include "reone/script/executionstate.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

SerializedIdentityContext snapshotIdentityContext() {
    return SerializedIdentityContext::moduleGraph("module003");
}

class CompleteReferenceEffect : public Effect {
public:
    CompleteReferenceEffect(
        const std::shared_ptr<Object> &creator,
        const std::array<std::shared_ptr<Object>, 4> &objects) :
        Effect(EffectType::Beam) {
        setSaveFacingCreator(creator);
        for (size_t index = 0; index < objects.size(); ++index) {
            setSaveFacingObject(index, objects[index]);
        }
    }
};

} // namespace

void reone::game::TestGameModule::configureModuleSnapshot(
    Game &game,
    std::shared_ptr<Area> area,
    std::shared_ptr<Creature> player,
    std::string moduleName,
    std::string areaName) {
    game._module = game.newModule();
    game._module->_name = std::move(moduleName);
    game._module->_tag = game._module->_name;
    game._module->_area = std::move(area);
    game._module->_info.entryArea = areaName;
    game._module->_area->_name = std::move(areaName);
    game._module->_area->_tag = game._module->_area->_name;
    game._party.addMember(kNpcPlayer, player);
    game._party.setPlayer(player);
    game._party.setActualPlayer(std::move(player));
    game._runtimeSessionPlayable = true;
}

void reone::game::TestGameModule::addSnapshotObject(
    Area &area, std::shared_ptr<Object> object) {
    area._objects.push_back(std::move(object));
}

void reone::game::TestGameModule::markSnapshotObjectDeleted(
    Area &area, uint32_t objectId) {
    area._objectsToDestroy.insert(objectId);
}

void reone::game::TestGameModule::addSnapshotLimboCreature(
    Module &module, std::shared_ptr<Creature> creature) {
    module._limboCreatures.push_back(std::move(creature));
}

void reone::game::TestGameModule::dispatchSnapshotEvents(Module &module) {
    module.dispatchDueSavedEvents();
}

void reone::game::TestGameModule::clearSnapshotDelayed(Object &object) {
    object._delayed.clear();
}

void reone::game::TestGameModule::initSnapshotLocalServices(Game &game) {
    game.initLocalServices();
}

void reone::game::TestGameModule::setSnapshotWorldTime(
    Game &game, uint32_t day, uint32_t time, uint8_t minutesPerHour) {
    // Compose the canonical clock the same way the load boundary does: the day
    // length has to be known before the pair means anything.
    game._minutesPerHour = minutesPerHour;
    game._worldTimeMilliseconds =
        static_cast<uint64_t>(day) * game.millisecondsPerWorldDay() + time;
}

void reone::game::TestGameModule::setSnapshotMinutesPerHour(
    Game &game, uint8_t minutesPerHour) {
    game._minutesPerHour = minutesPerHour;
}

void reone::game::TestGameModule::deserializeSnapshotRuntimeState(
    Object &object, const Gff &gff,
    const SerializedIdentityContext &identityContext) {
    object.deserializeRuntimeState(gff, identityContext);
}

void reone::game::TestGameModule::setSnapshotObjectId(
    Object &object, uint32_t objectId) {
    object._id = objectId;
}

void reone::game::TestGameModule::setSnapshotEquipment(
    Creature &creature, int slot, std::shared_ptr<Item> item) {
    creature._equipment[slot] = std::move(item);
    creature._equipment[slot]->setEquipped(true);
}
void reone::game::TestGameModule::setSnapshotDoorState(
    Door &door, DoorState state) {
    door._state = state;
}

void reone::game::TestGameModule::configureSnapshotLinkedDoor(
    Door &door, std::string module, std::string entry) {
    door._appearance = 0;
    door._genericType = 57;
    door._linkedToFlags = 2;
    door._linkedToModule = std::move(module);
    door._linkedTo = std::move(entry);
    door._linkedTransitionGeometry = {
        {-1.0f, -0.5f, 0.0f}, {-1.0f, 0.5f, 0.0f},
        {1.0f, 0.5f, 0.0f}, {1.0f, -0.5f, 0.0f}};
}

void reone::game::TestGameModule::markSnapshotLinkedDoorHelper(
    Trigger &trigger) {
    trigger._linkedDoorTransition = true;
}

void reone::game::TestGameModule::configureSnapshotCamera(
    StaticCamera &camera, int cameraId, glm::vec3 position,
    glm::quat orientation, float pitch, float height,
    float fieldOfView, float micRange) {
    camera._cameraId = cameraId;
    camera._position = std::move(position);
    camera._position.z += height;
    camera._staticOrientation = std::move(orientation);
    camera._staticPitch = pitch;
    camera._height = height;
    camera._fieldOfView = fieldOfView;
    camera._micRange = micRange;
}

namespace {

class UnserializableAction : public reone::game::Action {
public:
    UnserializableAction(Game &game, ServicesView &services) :
        reone::game::Action(game, services, ActionType::MoveToPoint) {
    }
};

std::shared_ptr<Gff> readGff(ByteBuffer bytes) {
    MemoryInputStream stream(bytes);
    GffReader reader(stream);
    reader.load();
    return reader.root();
}

std::shared_ptr<Gff> recordById(
    const Gff &root, const std::string &list, uint32_t id) {
    for (const auto &record : root.getList(list)) {
        if (record->getUint("ObjectId", kSavedRuntimeInvalidObjectId) == id) {
            return record;
        }
    }
    return nullptr;
}

struct SnapshotFixture : Test {
    SnapshotFixture() :
        game(GameID::KotOR, "", engine.options(), engine.services(), console) {
    }

    void SetUp() override {
        area = game.newArea();
        player = game.newCreature();
        TestGameModule::configureModuleSnapshot(
            game, area, player, "tat_m17ab", "tat_m17ab");
        TestGameModule::addSnapshotObject(*area, player);
        captureResourceShadows();
    }

    void captureResourceShadows() {
        auto ifo = Gff::Builder().type(0xffffffff)
            .field(Gff::Field::newDword("Mod_NextObjId0", 50))
            .field(Gff::Field::newCExoString("FutureIfo", "preserve-ifo"))
            .build();
        auto are = Gff::Builder().type(0xffffffff)
            .field(Gff::Field::newCExoString("FutureArea", "preserve-area"))
            .build();
        auto staleDoor = Gff::Builder().type(8)
            .field(Gff::Field::newDword("ObjectId", 999)).build();
        auto git = Gff::Builder().type(0xffffffff)
            .field(Gff::Field::newCExoString("FutureGit", "preserve-git"))
            .field(Gff::Field::newList("Door List", {staleDoor})).build();
        game.captureSaveResourceShadow(
            {SaveResourceKind::ModuleIfo, "tat_m17ab"}, *ifo);
        game.captureSaveResourceShadow(
            {SaveResourceKind::AreaAre, "tat_m17ab"}, *are);
        game.captureSaveResourceShadow(
            {SaveResourceKind::AreaGit, "tat_m17ab"}, *git);
    }

    std::shared_ptr<Door> addDoorWithShadow() {
        auto door = game.newDoor();
        door->setTag("test_door");
        door->setPosition({2.0f, 3.0f, 4.0f});
        auto source = Gff::Builder().type(8)
            .field(Gff::Field::newDword("ObjectId", door->id()))
            .field(Gff::Field::newByte("OpenState", 0))
            .field(Gff::Field::newByte("Locked", 0))
            .field(Gff::Field::newCExoString("FutureDoor", "preserve-door"))
            .build();
        door->captureSaveRecord(
            *source,
            SerializedIdentityContext::moduleGraph("tat_m17ab"),
            {SaveRecordOriginKind::ActiveGitObject, "tat_m17ab"});
        door->open();
        door->setLocked(true);
        TestGameModule::addSnapshotObject(*area, door);
        return door;
    }

    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
};

TEST(ModuleSnapshot, reports_no_playable_module_without_exposing_partial_bytes) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto before = TestGameModule::nextObjectId(game);

    auto result = ModuleSnapshotBuilder(game, "module000").build();

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, ModuleSnapshotError::NoPlayableModule);
    EXPECT_FALSE(result.snapshot);
    EXPECT_EQ(TestGameModule::nextObjectId(game), before);
}

TEST_F(SnapshotFixture, writes_and_reopens_complete_deterministic_module_state) {
    game.setCustomToken(9, "nine");
    game.setCustomToken(2, "two");
    game.module()->setLocalBoolean(7, true);
    game.module()->setLocalNumber(3, 44);
    area->setUnescapable(true);
    area->setStealthXPEnabled(true);
    area->setMaxStealthXP(80);
    area->setCurrentStealthXP(30);
    area->setStealthXPDecrement(5);
    TestGameModule::setSnapshotWorldTime(game, 3, 1000, 5);

    player->setCurrentHitPoints(17);
    player->setMaxHitPoints(35);
    player->setLocalBoolean(31, true);
    player->setLocalNumber(4, 23);
    player->applyEffect(game.newEffect<Effect>(EffectType::Haste), DurationType::Temporary, 30.0f);
    player->addAction(game.newAction<WaitAction>(30.0f));
    player->update(10.0f);

    auto door = addDoorWithShadow();
    auto placeable = game.newPlaceable();
    auto worldItem = game.newItem();
    auto trigger = game.newTrigger();
    auto encounter = game.newEncounter();
    auto store = game.newStore();
    auto waypoint = game.newWaypoint();
    auto sound = game.newSound();
    sound->setActive(true);
    auto cameraSource = Gff::Builder().type(14)
        .field(Gff::Field::newInt("CameraID", 7))
        .field(Gff::Field::newVector("Position", {4.0f, 5.0f, 6.0f}))
        .field(Gff::Field::newOrientation(
            "Orientation", glm::quat(1.0f, 0.0f, 0.0f, 0.0f)))
        .field(Gff::Field::newFloat("Pitch", 12.0f))
        .field(Gff::Field::newFloat("Height", 1.5f))
        .field(Gff::Field::newFloat("FieldOfView", 55.0f))
        .field(Gff::Field::newFloat("MicRange", 8.0f))
        .field(Gff::Field::newCExoString("FutureCamera", "preserve-camera"))
        .build();
    auto camera = game.newStaticCamera();
    TestGameModule::configureSnapshotCamera(
        *camera, 7, {4.0f, 5.0f, 6.0f},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 12.0f, 1.5f, 55.0f, 8.0f);
    camera->captureSaveRecord(
        *cameraSource,
        SerializedIdentityContext::moduleGraph("tat_m17ab"),
        {SaveRecordOriginKind::ActiveGitObject, "tat_m17ab"});
    for (const auto &object : std::vector<std::shared_ptr<Object>> {
             placeable, worldItem, trigger, encounter, store, waypoint, sound,
             camera}) {
        TestGameModule::addSnapshotObject(*area, object);
    }

    auto triggerShadow = Gff::Builder().type(1)
        .field(Gff::Field::newDword("ObjectId", trigger->id()))
        .field(Gff::Field::newDword("CreatorId", player->id()))
        .field(Gff::Field::newCExoString("FutureTrigger", "preserve-trigger"))
        .build();
    trigger->captureSaveRecord(
        *triggerShadow,
        SerializedIdentityContext::moduleGraph("tat_m17ab"),
        {SaveRecordOriginKind::ActiveGitObject, "tat_m17ab"});
    TestGameModule::deserializeSnapshotRuntimeState(
        *trigger, *triggerShadow,
        SerializedIdentityContext::moduleGraph("tat_m17ab"));
    game.registerSavedObjectIdentity(
        player->id(),
        player,
        SerializedIdentityContext::moduleGraph("tat_m17ab"));
    game.registerSavedObjectIdentity(
        trigger->id(),
        trigger,
        SerializedIdentityContext::moduleGraph("tat_m17ab"));
    game.resolveSavedObjectReferences();

    auto limbo = game.newCreature();
    limbo->setCurrentHitPoints(8);
    TestGameModule::addSnapshotLimboCreature(*game.module(), limbo);
    SavedEventRecord event;
    event.day = 8;
    event.time = 9123;
    event.object = SavedObjectReference(player->id());
    event.caller = SavedObjectReference(door->id());
    event.eventId = static_cast<uint32_t>(SavedEventType::CloseObject);
    event.bindObjectReferences(game);
    game.module()->enqueueSaveEvent(event);

    auto first = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(first) << first.message;
    auto second = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(second) << second.message;
    EXPECT_EQ(first.snapshot->archiveBytes, second.snapshot->archiveBytes);
    EXPECT_EQ(first.snapshot->target, ResourceId("module003", ResType::Sav));

    // The module archive is frozen before the live Area attachment retires.
    // Runtime execution must not leak into the destination merely because the
    // party creature survives, while persistent gameplay effects remain live.
    std::set<const Object *> retainedObjects {player.get()};
    player->retireAreaRuntime(area->pathfinder(), retainedObjects);
    EXPECT_TRUE(player->actions().empty());
    ASSERT_EQ(1u, player->effects().size());

    auto ifo = readGff(first.snapshot->ifoBytes);
    auto are = readGff(first.snapshot->areBytes);
    auto git = readGff(first.snapshot->gitBytes);
    ASSERT_EQ(ifo->signature(), std::optional<std::string>("IFO V3.2"));
    ASSERT_EQ(are->signature(), std::optional<std::string>("ARE V3.2"));
    ASSERT_EQ(git->signature(), std::optional<std::string>("GIT V3.2"));
    EXPECT_TRUE(ifo->getBool("Mod_IsSaveGame"));
    EXPECT_EQ(ifo->getString("Mod_Entry_Area"), "tat_m17ab");
    EXPECT_EQ(ifo->getUint("Mod_Area"), area->id());
    EXPECT_EQ(ifo->getUint("Mod_NextObjId0"), 50u);
    EXPECT_EQ(ifo->getUint64("Mod_Effect_NxtId"), game.nextEffectId());
    EXPECT_EQ(ifo->getString("FutureIfo"), "preserve-ifo");
    ASSERT_EQ(ifo->getList("Mod_Tokens").size(), 2);
    EXPECT_EQ(ifo->getList("Mod_Tokens")[0]->type(), 7u);
    EXPECT_EQ(ifo->getList("Mod_Tokens")[0]->getUint("Mod_TokensNumber"), 2u);
    EXPECT_EQ(ifo->getList("Mod_Tokens")[1]->getUint("Mod_TokensNumber"), 9u);
    const auto reparsedTokens = game.parseCustomTokens(*ifo);
    EXPECT_EQ(reparsedTokens.at(2), "two");
    EXPECT_EQ(reparsedTokens.at(9), "nine");
    Module e2Module(1000, game, engine.services());
    TestGameModule::deserializeSnapshotRuntimeState(
        e2Module, *ifo, snapshotIdentityContext());
    EXPECT_TRUE(e2Module.getLocalBoolean(7));
    EXPECT_EQ(e2Module.getLocalNumber(3), 44);
    ASSERT_EQ(ifo->getList("EventQueue").size(), 1);
    EXPECT_EQ(ifo->getList("EventQueue")[0]->getUint("Day"), 8u);
    EXPECT_EQ(ifo->getList("EventQueue")[0]->getUint("Time"), 9123u);
    const auto e2Events = SavedEventQueue::fromGff(
        *ifo, snapshotIdentityContext());
    ASSERT_EQ(e2Events.events.size(), 1);
    EXPECT_EQ(e2Events.events.front().day, 8u);
    EXPECT_EQ(e2Events.events.front().time, 9123u);
    ASSERT_EQ(ifo->getList("Mod_PlayerList").size(), 1);
    ASSERT_EQ(ifo->getList("Creature List").size(), 1);

    auto playerRecord = ifo->getList("Mod_PlayerList").front();
    EXPECT_EQ(playerRecord->getInt("CurrentHitPoints"), 17);
    ASSERT_EQ(playerRecord->getList("EffectList").size(), 1);
    ASSERT_EQ(playerRecord->getList("ActionList").size(), 1);
    EXPECT_FLOAT_EQ(
        std::get<float>(SavedActionRecord::fromGff(
            *playerRecord->getList("ActionList").front(),
            snapshotIdentityContext()).parameters.front().payload),
        20.0f);
    auto effect = EffectInstance::fromGff(
        *playerRecord->getList("EffectList").front(),
        snapshotIdentityContext());
    EXPECT_EQ(effect.expiryDay, 3u);
    // Twenty seconds remaining. World-time milliseconds are real milliseconds,
    // as in CWorldTimer, so this is 1000 + 20 * 1000.
    EXPECT_EQ(effect.expiryTime, 21000u);
    ASSERT_TRUE(game.remainingEffectDuration(effect));
    EXPECT_FLOAT_EQ(*game.remainingEffectDuration(effect), 20.0f);
    Creature e2Creature(player->id() + 1000, "", game, engine.services());
    TestGameModule::deserializeSnapshotRuntimeState(
        e2Creature, *playerRecord, snapshotIdentityContext());
    EXPECT_TRUE(e2Creature.getLocalBoolean(31));
    EXPECT_EQ(e2Creature.getLocalNumber(4), 23);
    ASSERT_EQ(e2Creature.savedEffects().size(), 1);
    ASSERT_EQ(e2Creature.savedActionQueue().actions.size(), 1);

    EXPECT_TRUE(are->getBool("Unescapable"));
    EXPECT_EQ(are->getUint("StealthXPCurrent"), 30u);
    EXPECT_EQ(are->getString("FutureArea"), "preserve-area");
    EXPECT_EQ(git->getString("FutureGit"), "preserve-git");
    EXPECT_EQ(git->getList("Creature List").size(), 0);
    EXPECT_EQ(git->getList("Door List").size(), 1);
    EXPECT_EQ(git->getList("Placeable List").size(), 1);
    EXPECT_EQ(git->getList("TriggerList").size(), 1);
    EXPECT_EQ(git->getList("Encounter List").size(), 1);
    EXPECT_EQ(git->getList("StoreList").size(), 1);
    EXPECT_EQ(git->getList("WaypointList").size(), 1);
    EXPECT_EQ(git->getList("SoundList").size(), 1);
    ASSERT_EQ(git->getList("CameraList").size(), 1);
    EXPECT_EQ(git->getList("List").size(), 1);
    EXPECT_EQ(git->getList("List").front()->getUint("ObjectId"), worldItem->id());
    auto doorRecord = recordById(*git, "Door List", door->id());
    ASSERT_TRUE(doorRecord);
    EXPECT_EQ(doorRecord->getUint("OpenState"), 1u);
    EXPECT_TRUE(doorRecord->getBool("Locked"));
    EXPECT_EQ(doorRecord->getString("FutureDoor"), "preserve-door");
    auto triggerRecord = recordById(*git, "TriggerList", trigger->id());
    ASSERT_TRUE(triggerRecord);
    EXPECT_EQ(triggerRecord->getUint("CreatorId"), player->id());
    EXPECT_EQ(triggerRecord->getString("FutureTrigger"), "preserve-trigger");
    EXPECT_TRUE(recordById(*git, "Encounter List", encounter->id()));
    EXPECT_TRUE(recordById(*git, "WaypointList", waypoint->id()));
    EXPECT_TRUE(recordById(*git, "StoreList", store->id())->getList("ItemList").empty());
    EXPECT_TRUE(recordById(*git, "SoundList", sound->id())->getBool("Active"));
    const auto &cameraRecord = *git->getList("CameraList").front();
    EXPECT_EQ(cameraRecord.type(), 14u);
    EXPECT_EQ(cameraRecord.getInt("CameraID"), 7);
    EXPECT_FLOAT_EQ(cameraRecord.getVector("Position").z, 6.0f);
    EXPECT_FLOAT_EQ(cameraRecord.getFloat("Pitch"), 12.0f);
    EXPECT_FLOAT_EQ(cameraRecord.getFloat("Height"), 1.5f);
    EXPECT_FLOAT_EQ(cameraRecord.getFloat("FieldOfView"), 55.0f);
    EXPECT_FLOAT_EQ(cameraRecord.getFloat("MicRange"), 8.0f);
    EXPECT_EQ(cameraRecord.getString("FutureCamera"), "preserve-camera");
    EXPECT_EQ(recordById(*git, "Placeable List", placeable->id())->getList("ItemList").size(), 0);

    ByteBuffer archiveBytes(first.snapshot->archiveBytes);
    MemoryInputStream archiveStream(archiveBytes);
    ErfReader archive(archiveStream);
    archive.load();
    EXPECT_EQ(archive.signature(), "MOD V1.0");
    EXPECT_EQ(archive.keys().size(), 3);
}

TEST_F(SnapshotFixture, rewrites_perception_shadow_ids_from_detached_namespace) {
    const auto detachedContext =
        SerializedIdentityContext::detachedRecord("availnpc0.utc");
    auto target = game.newCreature();
    target->assignSerializedObjectIdentity({detachedContext, 77u});
    TestGameModule::addSnapshotObject(*area, target);

    auto encounterRecord = Gff::Builder()
                               .type(7)
                               .field(Gff::Field::newList(
                                   "PerceptionList",
                                   {Gff::Builder()
                                        .type(0)
                                        .field(Gff::Field::newDword(
                                            "ObjectId", 77u))
                                        .field(Gff::Field::newByte(
                                            "PerceptionData", 3u))
                                        .build()}))
                               .build();
    auto encounter = game.newEncounter();
    encounter->deserializeRuntimeState(
        *encounterRecord, detachedContext);
    encounter->captureSaveRecord(
        *encounterRecord,
        detachedContext,
        {SaveRecordOriginKind::ActiveGitObject, "detached-test"});
    TestGameModule::addSnapshotObject(*area, encounter);
    game.resolveSavedObjectReferences();
    ASSERT_EQ(encounter->savedReference("Perception/0"), target);

    auto result = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(result) << result.message;
    auto git = readGff(result.snapshot->gitBytes);
    auto targetRecord = recordById(*git, "Creature List", target->id());
    ASSERT_TRUE(targetRecord);
    auto savedEncounter = recordById(
        *git, "Encounter List", encounter->id());
    ASSERT_TRUE(savedEncounter);
    ASSERT_EQ(savedEncounter->getList("PerceptionList").size(), 1u);
    const auto rewritten = savedEncounter->getList("PerceptionList")
                               .front()
                               ->getUint("ObjectId");
    EXPECT_EQ(rewritten, targetRecord->getUint("ObjectId"));
    EXPECT_NE(rewritten, 77u);
}

TEST_F(SnapshotFixture, authoritative_membership_omits_deleted_shadow_records) {
    auto door = addDoorWithShadow();
    auto present = ModuleSnapshotBuilder(game, "module004").build();
    ASSERT_TRUE(present) << present.message;
    ASSERT_EQ(present.snapshot->git->getList("Door List").size(), 1);

    TestGameModule::markSnapshotObjectDeleted(*area, door->id());
    auto deleted = ModuleSnapshotBuilder(game, "module004").build();
    ASSERT_TRUE(deleted) << deleted.message;
    EXPECT_TRUE(deleted.snapshot->git->getList("Door List").empty());
}

TEST_F(SnapshotFixture, linked_door_helpers_are_derived_not_saved_git_members) {
    auto door = game.newDoor();
    door->setTag("linked_door");
    TestGameModule::configureSnapshotLinkedDoor(
        *door, "destination_module", "Destination_Entry");
    TestGameModule::addSnapshotObject(*area, door);

    auto helper = game.newTrigger();
    TestGameModule::markSnapshotLinkedDoorHelper(*helper);
    TestGameModule::addSnapshotObject(*area, helper);

    auto authored = game.newTrigger();
    authored->setTag("authored_trigger");
    TestGameModule::addSnapshotObject(*area, authored);

    ASSERT_TRUE(helper->isLinkedDoorTransition());
    ASSERT_FALSE(authored->isLinkedDoorTransition());

    // A stale helper in the root shadow must not resurrect when authoritative
    // live membership replaces TriggerList.
    auto staleHelper = Gff::Builder().type(1)
        .field(Gff::Field::newDword("ObjectId", helper->id()))
        .field(Gff::Field::newCExoString("Tag", "stale_helper"))
        .build();
    auto rootShadow = Gff::Builder().type(0xffffffff)
        .field(Gff::Field::newList("TriggerList", {staleHelper}))
        .build();
    game.captureSaveResourceShadow(
        {SaveResourceKind::AreaGit, "tat_m17ab"}, *rootShadow);

    for (int activation = 0; activation < 3; ++activation) {
        SCOPED_TRACE(activation);
        auto saved = ModuleSnapshotBuilder(game, "module_linked").build();
        ASSERT_TRUE(saved) << saved.message;
        const auto &triggers = saved.snapshot->git->getList("TriggerList");
        ASSERT_EQ(triggers.size(), 1u);
        EXPECT_EQ(triggers.front()->getUint("ObjectId"), authored->id());
        EXPECT_FALSE(recordById(*saved.snapshot->git, "TriggerList", helper->id()));

        auto doorRecord = recordById(
            *saved.snapshot->git, "Door List", door->id());
        ASSERT_TRUE(doorRecord);
        EXPECT_EQ(doorRecord->getUint("Appearance"), 0u);
        EXPECT_EQ(doorRecord->getUint("GenericType"), 57u);
        EXPECT_EQ(doorRecord->getUint("LinkedToFlags"), 2u);
        EXPECT_EQ(doorRecord->getString("LinkedTo"), "Destination_Entry");
        EXPECT_EQ(doorRecord->getString("LinkedToModule"), "destination_module");
    }
}

TEST_F(SnapshotFixture, module_item_ids_are_global_deterministic_and_retained) {
    auto ownerA = game.newPlaceable();
    auto ownerB = game.newStore();
    auto creature = game.newCreature();
    auto first = game.newOwnedItem();
    auto second = game.newOwnedItem();
    auto third = game.newOwnedItem();
    auto carried = game.newOwnedItem();
    auto equipped = game.newOwnedItem();
    first->setTag("first");
    second->setTag("second");
    third->setTag("third");
    carried->setTag("carried");
    equipped->setTag("equipped");
    auto retain = [](const std::shared_ptr<Item> &item, uint32_t id,
                     SaveRecordOriginKind kind, const std::string &owner) {
        auto record = Gff::Builder().type(0)
            .field(Gff::Field::newDword("ObjectId", id)).build();
        item->captureSaveRecord(
            *record,
            SerializedIdentityContext::moduleGraph("module005"),
            {kind, owner});
    };
    retain(first, 70, SaveRecordOriginKind::PlaceableItem, "a");
    retain(third, 71, SaveRecordOriginKind::StoreItem, "b");
    retain(carried, 72, SaveRecordOriginKind::ContainedItem, "c");
    retain(equipped, 73, SaveRecordOriginKind::EquippedItem, "c");
    ownerA->addItem(first);
    ownerA->addItem(second);
    ownerB->addItem(third);
    creature->addItem(carried);
    TestGameModule::setSnapshotEquipment(*creature, InventorySlots::rightWeapon, equipped);
    TestGameModule::addSnapshotObject(*area, ownerA);
    TestGameModule::addSnapshotObject(*area, ownerB);
    TestGameModule::addSnapshotObject(*area, creature);

    auto saved = ModuleSnapshotBuilder(game, "module005").build();

    ASSERT_TRUE(saved) << saved.message;
    auto placeable = recordById(*saved.snapshot->git, "Placeable List", ownerA->id());
    auto store = recordById(*saved.snapshot->git, "StoreList", ownerB->id());
    auto savedCreature = recordById(*saved.snapshot->git, "Creature List", creature->id());
    ASSERT_TRUE(placeable);
    ASSERT_TRUE(store);
    ASSERT_TRUE(savedCreature);
    EXPECT_EQ(placeable->getList("ItemList").size(), 2);
    EXPECT_EQ(store->getList("ItemList").size(), 1);
    EXPECT_EQ(savedCreature->getList("ItemList").size(), 1);
    EXPECT_EQ(savedCreature->getList("Equip_ItemList").size(), 1);
    std::set<uint32_t> ids;
    for (const auto &item : placeable->getList("ItemList")) ids.insert(item->getUint("ObjectId"));
    for (const auto &item : store->getList("ItemList")) ids.insert(item->getUint("ObjectId"));
    for (const auto &item : savedCreature->getList("ItemList")) ids.insert(item->getUint("ObjectId"));
    for (const auto &item : savedCreature->getList("Equip_ItemList")) ids.insert(item->getUint("ObjectId"));
    EXPECT_EQ(ids.size(), 5);
    EXPECT_TRUE(ids.count(70));
    EXPECT_TRUE(ids.count(71));
    EXPECT_TRUE(ids.count(72));
    EXPECT_TRUE(ids.count(73));
    EXPECT_LT(*ids.rbegin(), kSavedRuntimeInvalidObjectId);
    EXPECT_EQ(saved.snapshot->ifo->getUint("Mod_NextObjId0"), 74u);

    auto repeated = ModuleSnapshotBuilder(game, "module005").build();
    ASSERT_TRUE(repeated) << repeated.message;
    EXPECT_EQ(repeated.snapshot->archiveBytes, saved.snapshot->archiveBytes);
}

namespace {

// A party member carries items whose saved IDs came from whatever module they
// were last serialized in. Those IDs are meaningless in the module being
// snapshotted, so they must be allocated fresh rather than retained.
struct PartyItemIdFixture : TestWithParam<GameID> {
    PartyItemIdFixture() :
        game(GetParam(), "", engine.options(), engine.services(), console) {
    }

    void SetUp() override {
        area = game.newArea();
        player = game.newCreature();
        TestGameModule::configureModuleSnapshot(
            game, area, player, "module_pid", "module_pid");
        TestGameModule::setSnapshotObjectId(*player, 0x7fffffffu);
        TestGameModule::addSnapshotObject(*area, player);
        captureResourceShadows();
    }

    // Snapshotting reads the module's own records. Capture them as shadows so
    // the builder never falls through to the shared resource mock, whose
    // expectations belong to other suites.
    void captureResourceShadows() {
        auto ifo = Gff::Builder().type(0xffffffff)
            .field(Gff::Field::newDword("Mod_NextObjId0", 50)).build();
        auto are = Gff::Builder().type(0xffffffff).build();
        auto git = Gff::Builder().type(0xffffffff).build();
        game.captureSaveResourceShadow({SaveResourceKind::ModuleIfo, "module_pid"}, *ifo);
        game.captureSaveResourceShadow({SaveResourceKind::AreaAre, "module_pid"}, *are);
        game.captureSaveResourceShadow({SaveResourceKind::AreaGit, "module_pid"}, *git);
    }

    /** Give an item the save-facing identity it held in a previous module. */
    void retain(const std::shared_ptr<Item> &item, uint32_t id,
                SaveRecordOriginKind kind, const std::string &owner) {
        auto record = Gff::Builder().type(0)
            .field(Gff::Field::newDword("ObjectId", id)).build();
        const auto identityContext =
            kind == SaveRecordOriginKind::PlaceableItem ||
                    kind == SaveRecordOriginKind::StoreItem
                ? SerializedIdentityContext::moduleGraph("module_pid")
                : SerializedIdentityContext::detachedRecord(owner);
        item->captureSaveRecord(
            *record,
            identityContext,
            {kind, owner});
    }

    std::shared_ptr<Sound> worldSoundWithId(uint32_t id) {
        auto sound = game.newSound();
        TestGameModule::setSnapshotObjectId(*sound, id);
        TestGameModule::addSnapshotObject(*area, sound);
        return sound;
    }

    /** ObjectIds the snapshot assigned to the player's equipped items. */
    std::vector<uint32_t> playerEquipIds(const resource::Gff &ifo) {
        std::vector<uint32_t> out;
        auto players = ifo.getList("Mod_PlayerList");
        if (players.empty()) return out;
        for (const auto &it : players.front()->getList("Equip_ItemList")) {
            out.push_back(it->getUint("ObjectId"));
        }
        return out;
    }

    std::set<uint32_t> worldIds(const resource::Gff &git) {
        std::set<uint32_t> out;
        for (const char *list : {"Creature List", "Door List", "Placeable List",
                                 "TriggerList", "WaypointList", "SoundList",
                                 "StoreList", "Encounter List"}) {
            for (const auto &o : git.getList(list)) out.insert(o->getUint("ObjectId"));
        }
        return out;
    }

    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
};

} // namespace

// A: the exact K1 Hawk failure - a world sound owns 106, and the PC carries an
// item that was serialized as 106 in a different module.
TEST_P(PartyItemIdFixture, foreign_party_item_id_does_not_collide_with_a_world_object) {
    worldSoundWithId(106);
    auto equipped = game.newOwnedItem();
    equipped->setTag("blaster");
    retain(equipped, 106, SaveRecordOriginKind::EquippedItem, "player");
    TestGameModule::setSnapshotEquipment(*player, InventorySlots::rightWeapon, equipped);

    auto saved = ModuleSnapshotBuilder(game, "module_pid").build();

    ASSERT_TRUE(saved) << saved.message;
    auto ifo = readGff(saved.snapshot->ifoBytes);
    auto ids = playerEquipIds(*ifo);
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_NE(ids.front(), 106u) << "a foreign saved ID must not be retained";
    EXPECT_GE(ids.front(), 2u);
}

TEST_F(SnapshotFixture, translates_authoritative_and_detached_item_136_by_object) {
    auto placeable = game.newPlaceable();
    auto authoritative = game.newOwnedItem();
    auto detached = game.newOwnedItem();
    auto item136 = Gff::Builder().type(0)
        .field(Gff::Field::newDword("ObjectId", 136)).build();
    authoritative->captureSaveRecord(
        *item136,
        SerializedIdentityContext::moduleGraph("tat_m17ab"),
        {SaveRecordOriginKind::PlaceableItem, "module-chest"});
    detached->captureSaveRecord(
        *item136,
        SerializedIdentityContext::detachedRecord("availnpc7.utc"),
        {SaveRecordOriginKind::EquippedItem, "availnpc7.utc"});
    placeable->addItem(authoritative);
    TestGameModule::addSnapshotObject(*area, placeable);
    TestGameModule::setSnapshotEquipment(
        *player, InventorySlots::rightWeapon, detached);

    auto saved = ModuleSnapshotBuilder(game, "tat_m17ab").build();

    ASSERT_TRUE(saved) << saved.message;
    auto placeables = saved.snapshot->git->getList("Placeable List");
    ASSERT_EQ(placeables.size(), 1u);
    auto moduleItems = placeables.front()->getList("ItemList");
    ASSERT_EQ(moduleItems.size(), 1u);
    EXPECT_EQ(moduleItems.front()->getUint("ObjectId"), 136u);
    auto playerRecord = saved.snapshot->ifo->getList("Mod_PlayerList").front();
    auto equipment = playerRecord->getList("Equip_ItemList");
    ASSERT_EQ(equipment.size(), 1u);
    EXPECT_NE(equipment.front()->getUint("ObjectId"), 136u);
    EXPECT_NE(equipment.front()->getUint("ObjectId"),
              moduleItems.front()->getUint("ObjectId"));
}

TEST_F(SnapshotFixture, imports_overlapping_ids_from_two_detached_graphs_once_each) {
    auto first = game.newOwnedItem();
    auto second = game.newOwnedItem();
    auto item136 = Gff::Builder().type(0)
        .field(Gff::Field::newDword("ObjectId", 136)).build();
    first->captureSaveRecord(
        *item136,
        SerializedIdentityContext::detachedRecord("availnpc0.utc"),
        {SaveRecordOriginKind::EquippedItem, "availnpc0.utc"});
    second->captureSaveRecord(
        *item136,
        SerializedIdentityContext::detachedRecord("availnpc7.utc"),
        {SaveRecordOriginKind::EquippedItem, "availnpc7.utc"});
    TestGameModule::setSnapshotEquipment(
        *player, InventorySlots::rightWeapon, first);
    TestGameModule::setSnapshotEquipment(
        *player, InventorySlots::leftWeapon, second);

    auto saved = ModuleSnapshotBuilder(game, "tat_m17ab").build();

    ASSERT_TRUE(saved) << saved.message;
    auto equipment = saved.snapshot->ifo->getList("Mod_PlayerList")
                         .front()->getList("Equip_ItemList");
    ASSERT_EQ(equipment.size(), 2u);
    EXPECT_NE(equipment[0]->getUint("ObjectId"),
              equipment[1]->getUint("ObjectId"));
    EXPECT_NE(equipment[0]->getUint("ObjectId"), 136u);
    EXPECT_NE(equipment[1]->getUint("ObjectId"), 136u);
}

// B: reallocation happens even when nothing collides.
TEST_P(PartyItemIdFixture, party_item_ids_are_allocated_not_retained) {
    auto equipped = game.newOwnedItem();
    retain(equipped, 4321, SaveRecordOriginKind::EquippedItem, "player");
    TestGameModule::setSnapshotEquipment(*player, InventorySlots::rightWeapon, equipped);

    auto saved = ModuleSnapshotBuilder(game, "module_pid").build();

    ASSERT_TRUE(saved) << saved.message;
    auto ids = playerEquipIds(*readGff(saved.snapshot->ifoBytes));
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_NE(ids.front(), 4321u);
}

// C: module-owned items still keep the identity they hold in this module.
TEST_P(PartyItemIdFixture, module_owned_item_retains_its_saved_id) {
    auto placeable = game.newPlaceable();
    TestGameModule::setSnapshotObjectId(*placeable, 300);
    auto stored = game.newOwnedItem();
    retain(stored, 301, SaveRecordOriginKind::PlaceableItem, "chest");
    placeable->addItem(stored);
    TestGameModule::addSnapshotObject(*area, placeable);

    auto saved = ModuleSnapshotBuilder(game, "module_pid").build();

    ASSERT_TRUE(saved) << saved.message;
    auto git = readGff(saved.snapshot->gitBytes);
    auto list = git->getList("Placeable List");
    ASSERT_FALSE(list.empty());
    auto items = list.front()->getList("ItemList");
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front()->getUint("ObjectId"), 301u)
        << "an item this module already owns keeps its saved ID";
}

// D + E: several foreign party items, all overlapping live world IDs.
TEST_P(PartyItemIdFixture, every_party_item_gets_a_unique_non_colliding_id) {
    worldSoundWithId(106);
    worldSoundWithId(107);
    worldSoundWithId(108);

    auto right = game.newOwnedItem();
    auto left = game.newOwnedItem();
    auto body = game.newOwnedItem();
    retain(right, 106, SaveRecordOriginKind::EquippedItem, "player");
    retain(left, 107, SaveRecordOriginKind::EquippedItem, "player");
    retain(body, 108, SaveRecordOriginKind::EquippedItem, "player");
    TestGameModule::setSnapshotEquipment(*player, InventorySlots::rightWeapon, right);
    TestGameModule::setSnapshotEquipment(*player, InventorySlots::leftWeapon, left);
    TestGameModule::setSnapshotEquipment(*player, InventorySlots::body, body);

    auto saved = ModuleSnapshotBuilder(game, "module_pid").build();

    ASSERT_TRUE(saved) << saved.message;
    auto ifo = readGff(saved.snapshot->ifoBytes);
    auto git = readGff(saved.snapshot->gitBytes);
    auto ids = playerEquipIds(*ifo);
    ASSERT_EQ(ids.size(), 3u);

    std::set<uint32_t> unique(ids.begin(), ids.end());
    EXPECT_EQ(unique.size(), ids.size()) << "allocated IDs must be distinct";
    for (auto id : ids) {
        EXPECT_EQ(worldIds(*git).count(id), 0u)
            << "allocated ID " << id << " collides with a world object";
    }
}


// Cameras are presentation records rebuilt from module data, addressed by
// CameraID. Retail never places them in CGameObjectArray, so their runtime
// identity must not consume the module's saved object namespace. Before this
// was fixed, a camera's runtime ID could claim an identity that a legitimate
// module-owned item already held, and snapshotting the source module aborted
// the whole transition.
struct CameraIdFixture : TestWithParam<GameID> {
    CameraIdFixture() :
        game(GetParam(), "", engine.options(), engine.services(), console) {
    }

    void SetUp() override {
        area = game.newArea();
        player = game.newCreature();
        TestGameModule::configureModuleSnapshot(
            game, area, player, "module_cam", "module_cam");
        TestGameModule::setSnapshotObjectId(*player, 0x7ffffffeu);
        TestGameModule::addSnapshotObject(*area, player);

        auto ifo = Gff::Builder().type(0xffffffff)
            .field(Gff::Field::newDword("Mod_NextObjId0", 50)).build();
        auto are = Gff::Builder().type(0xffffffff).build();
        auto git = Gff::Builder().type(0xffffffff).build();
        game.captureSaveResourceShadow({SaveResourceKind::ModuleIfo, "module_cam"}, *ifo);
        game.captureSaveResourceShadow({SaveResourceKind::AreaAre, "module_cam"}, *are);
        game.captureSaveResourceShadow({SaveResourceKind::AreaGit, "module_cam"}, *git);
    }

    /** A camera whose runtime object ID is exactly `runtimeId`. */
    std::shared_ptr<StaticCamera> cameraWithRuntimeId(uint32_t runtimeId, int cameraId) {
        auto camera = game.newStaticCamera();
        TestGameModule::setSnapshotObjectId(*camera, runtimeId);
        TestGameModule::configureSnapshotCamera(
            *camera, cameraId, {1.0f, 2.0f, 3.0f},
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 10.0f, 1.5f, 55.0f, 8.0f);
        TestGameModule::addSnapshotObject(*area, camera);
        return camera;
    }

    /** A module-owned item that already holds `id` in this module. */
    std::shared_ptr<Item> moduleItemWithSavedId(uint32_t id, const std::string &tag) {
        auto placeable = game.newPlaceable();
        TestGameModule::setSnapshotObjectId(*placeable, 900 + id);
        auto item = game.newOwnedItem();
        item->setTag(tag);
        auto record = Gff::Builder().type(0)
            .field(Gff::Field::newDword("ObjectId", id)).build();
        item->captureSaveRecord(
            *record,
            SerializedIdentityContext::moduleGraph("module_cam"),
            {SaveRecordOriginKind::PlaceableItem, "module_cam"});
        placeable->addItem(item);
        TestGameModule::addSnapshotObject(*area, placeable);
        return item;
    }

    std::set<uint32_t> savedWorldIds(const resource::Gff &git) const {
        std::set<uint32_t> out;
        for (const char *list : {"Creature List", "Door List", "Placeable List",
                                 "TriggerList", "WaypointList", "SoundList",
                                 "StoreList", "Encounter List", "List"}) {
            for (const auto &o : git.getList(list)) out.insert(o->getUint("ObjectId"));
        }
        return out;
    }

    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
};

// The exact reproduced failure: a camera runtime ID equal to a legitimate
// module-owned item's saved ID must not abort the snapshot.
TEST_P(CameraIdFixture, camera_runtime_id_may_equal_a_module_item_saved_id) {
    cameraWithRuntimeId(577, 3);
    auto item = moduleItemWithSavedId(577, "g_w_dsrptrfl001");

    auto result = ModuleSnapshotBuilder(game, "module_cam").build();

    ASSERT_TRUE(result) << result.message;
    auto git = readGff(result.snapshot->gitBytes);
    // The item keeps the identity it legitimately owns in this module.
    auto placeables = git->getList("Placeable List");
    ASSERT_FALSE(placeables.empty());
    auto items = placeables.front()->getList("ItemList");
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items.front()->getUint("ObjectId"), 577u);
}

TEST_P(CameraIdFixture, camera_does_not_reserve_an_entry_in_the_saved_namespace) {
    cameraWithRuntimeId(577, 3);

    auto result = ModuleSnapshotBuilder(game, "module_cam").build();

    ASSERT_TRUE(result) << result.message;
    auto git = readGff(result.snapshot->gitBytes);
    // No saved world record claims the camera's runtime identity, and the
    // camera record itself carries no ObjectId at all.
    EXPECT_EQ(savedWorldIds(*git).count(577u), 0u);
    auto cameras = git->getList("CameraList");
    ASSERT_EQ(cameras.size(), 1u);
    EXPECT_FALSE(cameras.front()->has("ObjectId"));
}

TEST_P(CameraIdFixture, camera_survives_the_snapshot_through_its_camera_id) {
    cameraWithRuntimeId(577, 42);

    auto result = ModuleSnapshotBuilder(game, "module_cam").build();

    ASSERT_TRUE(result) << result.message;
    auto cameras = readGff(result.snapshot->gitBytes)->getList("CameraList");
    ASSERT_EQ(cameras.size(), 1u);
    // CameraID is the camera's durable identity, and the retail presentation
    // fields travel with it.
    EXPECT_EQ(cameras.front()->getInt("CameraID"), 42);
    EXPECT_FLOAT_EQ(cameras.front()->getFloat("FieldOfView"), 55.0f);
    EXPECT_FLOAT_EQ(cameras.front()->getFloat("MicRange"), 8.0f);
    EXPECT_FLOAT_EQ(cameras.front()->getFloat("Pitch"), 10.0f);
}

// The runtime and the save-facing namespaces are separate. Several cameras,
// each with its own distinct runtime ID, may each overlap a different saved
// item identity without any of them contending for it.
TEST_P(CameraIdFixture, camera_runtime_ids_never_contend_for_saved_item_identities) {
    cameraWithRuntimeId(600, 1);
    cameraWithRuntimeId(601, 2);
    moduleItemWithSavedId(600, "g_w_blstrpstl001");
    moduleItemWithSavedId(601, "g_w_blstrrfl001");

    auto result = ModuleSnapshotBuilder(game, "module_cam").build();

    ASSERT_TRUE(result) << result.message;
    auto git = readGff(result.snapshot->gitBytes);

    // Both items keep the identities they legitimately own.
    std::set<uint32_t> itemIds;
    for (const auto &placeable : git->getList("Placeable List")) {
        for (const auto &item : placeable->getList("ItemList")) {
            itemIds.insert(item->getUint("ObjectId"));
        }
    }
    EXPECT_EQ(itemIds, (std::set<uint32_t> {600u, 601u}));

    // Both cameras serialize through CameraID alone.
    auto cameras = git->getList("CameraList");
    ASSERT_EQ(cameras.size(), 2u);
    std::set<int> cameraIds;
    for (const auto &camera : cameras) {
        EXPECT_FALSE(camera->has("ObjectId"));
        cameraIds.insert(camera->getInt("CameraID"));
    }
    EXPECT_EQ(cameraIds, (std::set<int> {1, 2}));
}

// Fail-closed behaviour for entities that really do share the namespace must
// be untouched by the camera exclusion.
TEST_P(CameraIdFixture, genuine_saved_object_collision_is_still_rejected) {
    cameraWithRuntimeId(577, 3);
    auto soundA = game.newSound();
    TestGameModule::setSnapshotObjectId(*soundA, 321);
    soundA->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module_cam"), 321});
    TestGameModule::addSnapshotObject(*area, soundA);
    auto soundB = game.newSound();
    TestGameModule::setSnapshotObjectId(*soundB, 321);
    soundB->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module_cam"), 321});
    TestGameModule::addSnapshotObject(*area, soundB);

    auto result = ModuleSnapshotBuilder(game, "module_cam").build();

    EXPECT_FALSE(result);
    EXPECT_THAT(result.message, HasSubstr("collides in module namespace"));
}

// A camera must not open a hole for a foreign item identity either: an item
// carrying another module's ID is still allocated a fresh one (#327).
TEST_P(CameraIdFixture, foreign_item_id_is_still_allocated_not_retained) {
    cameraWithRuntimeId(577, 3);
    auto equipped = game.newOwnedItem();
    equipped->setTag("blaster");
    auto record = Gff::Builder().type(0)
        .field(Gff::Field::newDword("ObjectId", 577)).build();
    equipped->captureSaveRecord(
        *record,
        SerializedIdentityContext::detachedRecord("some_other_module"),
        {SaveRecordOriginKind::EquippedItem, "some_other_module"});
    TestGameModule::setSnapshotEquipment(*player, InventorySlots::rightWeapon, equipped);

    auto result = ModuleSnapshotBuilder(game, "module_cam").build();

    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto players = ifo->getList("Mod_PlayerList");
    ASSERT_FALSE(players.empty());
    auto equip = players.front()->getList("Equip_ItemList");
    ASSERT_EQ(equip.size(), 1u);
    EXPECT_NE(equip.front()->getUint("ObjectId"), 577u)
        << "a party item must not retain a foreign module's identity";
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    CameraIdFixture,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    PartyItemIdFixture,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });

TEST_F(SnapshotFixture, retained_module_item_collision_is_rejected) {
    auto owner = game.newPlaceable();
    auto item = game.newOwnedItem();
    auto record = Gff::Builder().type(0)
        .field(Gff::Field::newDword("ObjectId", owner->id())).build();
    item->captureSaveRecord(
        *record,
        SerializedIdentityContext::moduleGraph("module005"),
        {SaveRecordOriginKind::PlaceableItem, "owner"});
    owner->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module005"), owner->id()});
    owner->addItem(item);
    TestGameModule::addSnapshotObject(*area, owner);

    auto saved = ModuleSnapshotBuilder(game, "module005").build();

    EXPECT_FALSE(saved);
    EXPECT_EQ(saved.error, ModuleSnapshotError::UnsupportedLiveState);
    EXPECT_THAT(saved.message, HasSubstr(
        "authoritative saved object ID collides"));
}

TEST(ModuleSnapshot, k1_reserved_player_id_does_not_drive_cursor_or_duplicate_inventory) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto player = game.newCreature();
    TestGameModule::setSnapshotObjectId(*player, 0x7fffffffu);
    TestGameModule::configureModuleSnapshot(
        game, area, player, "end_m01ab", "end_m01ab");
    TestGameModule::addSnapshotObject(*area, player);
    auto door = game.newDoor();
    TestGameModule::setSnapshotObjectId(*door, 135);
    TestGameModule::addSnapshotObject(*area, door);
    auto shared = game.newOwnedItem();
    shared->setTag("shared");
    player->addItem(shared);

    auto saved = ModuleSnapshotBuilder(game, "module005").build();

    ASSERT_TRUE(saved) << saved.message;
    ASSERT_EQ(saved.snapshot->ifo->getList("Mod_PlayerList").size(), 1);
    auto modulePlayer = saved.snapshot->ifo->getList("Mod_PlayerList").front();
    EXPECT_TRUE(modulePlayer->getList("ItemList").empty());
    EXPECT_TRUE(modulePlayer->getList("Equip_ItemList").empty());
    EXPECT_EQ(saved.snapshot->ifo->getUint("Mod_NextObjId0"), 136u);

    Game reloaded(GameID::KotOR, "", engine.options(), engine.services(), console);
    reloaded.prepareSavedRuntimeNamespace(
        *saved.snapshot->ifo,
        SerializedIdentityContext::moduleGraph("test-module"));
    auto postLoadItem = reloaded.newItem();
    EXPECT_EQ(postLoadItem->id(), 136u);
    EXPECT_EQ(reloaded.getObjectById(136), postLoadItem);
}

TEST(ModuleSnapshot, tsl_reserved_player_id_does_not_drive_cursor) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto player = game.newCreature();
    TestGameModule::configureModuleSnapshot(
        game, area, player, "301nar", "301nar");
    TestGameModule::addSnapshotObject(*area, player);
    TestGameModule::setSnapshotObjectId(*player, 0x7fffffffu);
    auto door = game.newDoor();
    TestGameModule::setSnapshotObjectId(*door, 135);
    TestGameModule::addSnapshotObject(*area, door);

    auto saved = ModuleSnapshotBuilder(game, "module005").build();

    ASSERT_TRUE(saved) << saved.message;
    EXPECT_EQ(saved.snapshot->ifo->getUint("Mod_NextObjId0"), 136u);
    EXPECT_TRUE(saved.snapshot->ifo->getList("Mod_PlayerList").front()
                    ->getList("ItemList").empty());
}
TEST_F(SnapshotFixture, reserved_limbo_party_id_does_not_drive_ordinary_cursor) {
    TestGameModule::setSnapshotObjectId(*player, 0x7fffffffu);
    auto companion = game.newCreature();
    TestGameModule::setSnapshotObjectId(*companion, 0x7ffffffeu);
    TestGameModule::addSnapshotLimboCreature(*game.module(), companion);

    auto saved = ModuleSnapshotBuilder(game, "module006").build();

    ASSERT_TRUE(saved) << saved.message;
    EXPECT_EQ(saved.snapshot->ifo->getUint("Mod_NextObjId0"), 50u);
    ASSERT_EQ(saved.snapshot->ifo->getList("Creature List").size(), 1);
    EXPECT_EQ(saved.snapshot->ifo->getList("Creature List").front()->getUint("ObjectId"),
              0x7ffffffeu);
}

TEST_F(SnapshotFixture, duplicate_reserved_party_ids_are_rejected) {
    TestGameModule::setSnapshotObjectId(*player, 0x7fffffffu);
    player->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module006"), 0x7fffffffu});
    auto companion = game.newCreature();
    TestGameModule::setSnapshotObjectId(*companion, 0x7fffffffu);
    companion->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module006"), 0x7fffffffu});
    TestGameModule::addSnapshotLimboCreature(*game.module(), companion);

    auto saved = ModuleSnapshotBuilder(game, "module006").build();

    EXPECT_FALSE(saved);
    EXPECT_EQ(saved.error, ModuleSnapshotError::UnsupportedLiveState);
    EXPECT_THAT(saved.message, HasSubstr("duplicate reserved ID"));
}
TEST_F(SnapshotFixture, module_player_keeps_equipment_but_not_shared_inventory) {
    auto shared = game.newOwnedItem();
    auto equipped = game.newOwnedItem();
    shared->setTag("shared");
    equipped->setTag("equipped");
    player->addItem(shared);
    TestGameModule::setSnapshotEquipment(
        *player, InventorySlots::rightWeapon, equipped);

    auto saved = ModuleSnapshotBuilder(game, "module006").build();

    ASSERT_TRUE(saved) << saved.message;
    auto modulePlayer = saved.snapshot->ifo->getList("Mod_PlayerList").front();
    EXPECT_TRUE(modulePlayer->getList("ItemList").empty());
    ASSERT_EQ(modulePlayer->getList("Equip_ItemList").size(), 1);
    EXPECT_LT(modulePlayer->getList("Equip_ItemList").front()->getUint("ObjectId"),
              kSavedRuntimeInvalidObjectId);
}
TEST_F(SnapshotFixture, preserves_open2_and_accepts_low_retail_world_ids) {
    auto door = game.newDoor();
    auto placeable = game.newPlaceable();
    TestGameModule::setSnapshotObjectId(*door, 1);
    TestGameModule::setSnapshotObjectId(*placeable, 4);
    TestGameModule::setSnapshotDoorState(*door, DoorState::Opened2);
    TestGameModule::addSnapshotObject(*area, door);
    TestGameModule::addSnapshotObject(*area, placeable);

    auto saved = ModuleSnapshotBuilder(game, "module007").build();

    ASSERT_TRUE(saved) << saved.message;
    auto doorRecord = recordById(*saved.snapshot->git, "Door List", 1);
    ASSERT_TRUE(doorRecord);
    EXPECT_EQ(doorRecord->getUint("OpenState"), 2u);
    EXPECT_TRUE(recordById(*saved.snapshot->git, "Placeable List", 4));
}

TEST_F(SnapshotFixture, rejects_duplicate_authoritative_world_ids) {
    auto first = game.newDoor();
    auto second = game.newPlaceable();
    TestGameModule::setSnapshotObjectId(*first, 77);
    TestGameModule::setSnapshotObjectId(*second, 77);
    first->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module008"), 77});
    second->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module008"), 77});
    TestGameModule::addSnapshotObject(*area, first);
    TestGameModule::addSnapshotObject(*area, second);

    auto saved = ModuleSnapshotBuilder(game, "module008").build();

    EXPECT_FALSE(saved);
    EXPECT_EQ(saved.error, ModuleSnapshotError::UnsupportedLiveState);
    EXPECT_THAT(saved.message, HasSubstr("collides in module namespace"));
}

TEST_F(SnapshotFixture, rejects_world_id_collision_with_structural_area) {
    auto door = game.newDoor();
    TestGameModule::setSnapshotObjectId(*door, area->id());
    area->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module008"), area->id()});
    door->assignSerializedObjectIdentity({
        SerializedIdentityContext::moduleGraph("module008"), area->id()});
    TestGameModule::addSnapshotObject(*area, door);

    auto saved = ModuleSnapshotBuilder(game, "module008").build();

    EXPECT_FALSE(saved);
    EXPECT_EQ(saved.error, ModuleSnapshotError::UnsupportedLiveState);
    EXPECT_THAT(saved.message, HasSubstr("collides in module namespace"));
}

TEST_F(SnapshotFixture, deleted_reference_targets_are_written_as_retail_invalid) {
    auto door = game.newDoor();
    auto trigger = game.newTrigger();
    TestGameModule::addSnapshotObject(*area, door);
    TestGameModule::addSnapshotObject(*area, trigger);
    auto source = Gff::Builder().type(1)
        .field(Gff::Field::newDword("ObjectId", trigger->id()))
        .field(Gff::Field::newDword("CreatorId", door->id())).build();
    trigger->captureSaveRecord(
        *source,
        SerializedIdentityContext::moduleGraph("tat_m17ab"),
        {SaveRecordOriginKind::ActiveGitObject, "tat_m17ab"});
    TestGameModule::deserializeSnapshotRuntimeState(
        *trigger, *source,
        SerializedIdentityContext::moduleGraph("tat_m17ab"));
    game.resolveSavedObjectReferences();

    EffectInstance effect;
    effect.creatorId = door->id();
    effect.subType = static_cast<uint16_t>(DurationType::Permanent);
    ASSERT_TRUE(game.bindEffectCreator(effect));
    player->applyEffect(
        std::make_shared<SavedEffectValue>(effect),
        DurationType::Permanent);
    TestGameModule::markSnapshotObjectDeleted(*area, door->id());

    auto saved = ModuleSnapshotBuilder(game, "module009").build();

    ASSERT_TRUE(saved) << saved.message;
    auto triggerRecord = recordById(
        *saved.snapshot->git, "TriggerList", trigger->id());
    ASSERT_TRUE(triggerRecord);
    EXPECT_EQ(
        triggerRecord->getUint("CreatorId"),
        kSavedRuntimeInvalidObjectId);
    const auto &effects = saved.snapshot->ifo
                              ->getList("Mod_PlayerList")
                              .front()
                              ->getList("EffectList");
    ASSERT_EQ(effects.size(), 1);
    EXPECT_EQ(effects.front()->getUint("CreatorId"), kSavedRuntimeInvalidObjectId);
    EXPECT_TRUE(saved.snapshot->git->getList("Door List").empty());
}

TEST_F(SnapshotFixture, unsupported_live_action_fails_without_mutating_runtime_or_shadows) {
    player->addAction(game.newAction<UnserializableAction>());
    auto actionCount = player->actions().size();
    auto cursor = TestGameModule::nextObjectId(game);
    auto shadowCount = game.saveResourceShadows().size();

    auto result = ModuleSnapshotBuilder(game, "module006").build();

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, ModuleSnapshotError::UnsupportedLiveState);
    EXPECT_FALSE(result.snapshot);
    EXPECT_THAT(result.message, HasSubstr("ownerId="));
    EXPECT_THAT(result.message, HasSubstr("ownerType=1"));
    EXPECT_THAT(result.message, HasSubstr("queueIndex=0"));
    EXPECT_THAT(result.message, HasSubstr("actionType=0"));
    EXPECT_THAT(result.message, HasSubstr("runtimeClass="));
    EXPECT_THAT(result.message, HasSubstr("provenance=runtime-created"));
    EXPECT_EQ(player->actions().size(), actionCount);
    EXPECT_EQ(TestGameModule::nextObjectId(game), cursor);
    EXPECT_EQ(game.saveResourceShadows().size(), shadowCount);
}

TEST_F(SnapshotFixture, play_animation_is_a_supported_transition_snapshot_action) {
    auto action = game.newAction<PlayAnimationAction>(
        AnimationType::LoopingPause, 1.5f, 6.0f, true, true);
    SavedActionRecord provenance;
    provenance.groupActionId = 19;
    action->attachSavedAction(provenance);
    player->addAction(action);

    auto result = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto playerRecord = ifo->getList("Mod_PlayerList").front();
    ASSERT_EQ(playerRecord->getList("ActionList").size(), 1);
    auto saved = SavedActionRecord::fromGff(
        *playerRecord->getList("ActionList").front(),
        snapshotIdentityContext());
    EXPECT_EQ(saved.actionId, 6u);
    EXPECT_EQ(saved.groupActionId, 19);
    ASSERT_EQ(saved.parameters.size(), 5);
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[0].payload).id,
              static_cast<uint32_t>(AnimationType::LoopingPause));
    EXPECT_EQ(std::get<int32_t>(saved.parameters[3].payload), 0);
    EXPECT_TRUE(saved.toRuntimeAction(game));
}

TEST_F(SnapshotFixture, attack_object_is_a_supported_transition_snapshot_action) {
    auto target = game.newCreature();
    TestGameModule::addSnapshotObject(*area, target);
    auto action = game.newAction<AttackObjectAction>(target);
    SavedActionRecord provenance;
    provenance.groupActionId = 29;
    action->attachSavedAction(provenance);
    player->addAction(action);

    auto result = ModuleSnapshotBuilder(game, "module003").build();

    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto playerRecord = ifo->getList("Mod_PlayerList").front();
    ASSERT_EQ(playerRecord->getList("ActionList").size(), 1);
    auto saved = SavedActionRecord::fromGff(
        *playerRecord->getList("ActionList").front(),
        snapshotIdentityContext());
    EXPECT_EQ(saved.actionId, 12u);
    EXPECT_EQ(saved.groupActionId, 29);
    EXPECT_EQ(saved.declaredParameterCount, 10);
    ASSERT_EQ(saved.parameters.size(), 10);
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[1].payload).id,
              target->id());
    game.registerSavedObjectIdentity(
        std::get<SavedObjectReference>(saved.parameters[1].payload).id,
        target,
        SerializedIdentityContext::moduleGraph("module003"));
    ASSERT_TRUE(saved.bindObjectReferences(game));
    EXPECT_TRUE(saved.toRuntimeAction(game));
}

TEST_F(SnapshotFixture, move_to_location_is_a_supported_transition_snapshot_action) {
    auto destination = std::make_shared<Location>(glm::vec3(24.0f, 7.0f, 1.5f), 0.0f);
    MoveToLocationAction::ForcedState state;
    state.areaId = area->id();
    auto action = game.newAction<MoveToLocationAction>(
        destination, false, false, -1.0f, state);
    SavedActionRecord provenance;
    provenance.groupActionId = 33;
    action->attachSavedAction(provenance);
    player->addAction(action);

    auto result = ModuleSnapshotBuilder(game, "module003").build();

    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto playerRecord = ifo->getList("Mod_PlayerList").front();
    ASSERT_EQ(playerRecord->getList("ActionList").size(), 1);
    auto saved = SavedActionRecord::fromGff(
        *playerRecord->getList("ActionList").front(),
        snapshotIdentityContext());
    EXPECT_EQ(saved.actionId, 1u);
    EXPECT_EQ(saved.groupActionId, 33);
    EXPECT_EQ(saved.declaredParameterCount, 13);
    ASSERT_EQ(saved.parameters.size(), 13);
    EXPECT_TRUE(std::get<SavedObjectReference>(saved.parameters[4].payload).isInvalid());
    game.registerSavedObjectIdentity(
        std::get<SavedObjectReference>(saved.parameters[3].payload).id,
        area,
        SerializedIdentityContext::moduleGraph("module003"));
    ASSERT_TRUE(saved.bindObjectReferences(game));
    EXPECT_TRUE(std::dynamic_pointer_cast<MoveToLocationAction>(
        saved.toRuntimeAction(game)));
}

TEST_F(SnapshotFixture, move_to_object_is_a_supported_transition_snapshot_action) {
    auto target = game.newCreature();
    TestGameModule::addSnapshotObject(*area, target);
    auto action = game.newAction<MoveToObjectAction>(target, false, 0.5f);
    SavedActionRecord provenance;
    provenance.groupActionId = 13;
    action->attachSavedAction(provenance);
    player->addAction(action);

    auto result = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto playerRecord = ifo->getList("Mod_PlayerList").front();
    ASSERT_EQ(playerRecord->getList("ActionList").size(), 1);
    auto saved = SavedActionRecord::fromGff(
        *playerRecord->getList("ActionList").front(),
        snapshotIdentityContext());
    EXPECT_EQ(saved.actionId, 17u);
    EXPECT_EQ(saved.groupActionId, 13);
    ASSERT_EQ(saved.parameters.size(), 5);
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[0].payload).id,
              target->id());
    EXPECT_EQ(std::get<int32_t>(saved.parameters[1].payload), 0);
    EXPECT_FLOAT_EQ(std::get<float>(saved.parameters[2].payload), 0.5f);
}

TEST_F(SnapshotFixture, forced_move_to_object_is_a_supported_transition_snapshot_action) {
    auto target = game.newCreature();
    target->setPosition({46.0f, 17.0f, 1.9f});
    TestGameModule::addSnapshotObject(*area, target);
    auto action = game.newAction<MoveToObjectAction>(target, false, 0.5f, true, 30.0f);
    SavedActionRecord provenance;
    provenance.groupActionId = 13;
    action->attachSavedAction(provenance);
    player->addAction(action);

    auto result = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto saved = SavedActionRecord::fromGff(
        *ifo->getList("Mod_PlayerList").front()->getList("ActionList").front(),
        snapshotIdentityContext());
    EXPECT_EQ(saved.actionId, 1u);
    EXPECT_EQ(saved.groupActionId, 13);
    ASSERT_EQ(saved.parameters.size(), 13);
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[3].payload).id, area->id());
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[4].payload).id, target->id());
    EXPECT_EQ(std::get<int32_t>(saved.parameters[5].payload), 4);
    EXPECT_FLOAT_EQ(std::get<float>(saved.parameters[8].payload), 30.0f);
}

TEST_F(SnapshotFixture, pending_do_command_is_a_supported_transition_snapshot_action) {
    auto talentItem = game.newOwnedItem();
    ASSERT_NE(talentItem->id(), 200u);
    talentItem->assignSerializedObjectIdentity(
        {snapshotIdentityContext(), 200u});
    game.registerSavedObjectIdentity(
        200u, talentItem, snapshotIdentityContext());
    TestGameModule::setSnapshotEquipment(*player, 0, talentItem);

    auto program = std::make_shared<script::ScriptProgram>("transition_command");
    program->add(script::Instruction(script::InstructionType::RETN));
    auto state = std::make_shared<script::ExecutionState>();
    state->program = std::move(program);
    state->insOffset = 13;
    state->globals = {
        script::Variable::ofInt(9),
        script::Variable::ofTalent(std::make_shared<Talent>(
            TalentType::Spell, 123, 2, talentItem->id(), 5, 14, 1))};
    state->locals = {script::Variable::ofString("after-talent")};
    auto context = std::make_shared<script::ExecutionContext>();
    context->savedState = std::move(state);
    auto action = game.newAction<DoCommandAction>(std::move(context));
    SavedActionRecord provenance;
    provenance.groupActionId = 18;
    action->attachSavedAction(provenance);
    player->addAction(action);

    auto result = ModuleSnapshotBuilder(game, "module003").build();

    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto actionRecord = ifo->getList("Mod_PlayerList").front()->getList("ActionList").front();
    auto situationRecord = actionRecord->getList("Paramaters").front()->findStruct("Value");
    ASSERT_TRUE(situationRecord);
    auto stackRecord = situationRecord->findStruct("Stack");
    ASSERT_TRUE(stackRecord);
    auto talentRecord = stackRecord->getList("Stack")[1]->findStruct("GameDefinedStrct");
    ASSERT_TRUE(talentRecord);
    auto fieldType = [&](std::string_view label) {
        auto it = std::find_if(
            talentRecord->fields().begin(), talentRecord->fields().end(),
            [&](const auto &field) { return field.label == label; });
        EXPECT_NE(it, talentRecord->fields().end());
        return it == talentRecord->fields().end() ? Gff::FieldType::Byte : it->type;
    };
    EXPECT_EQ(fieldType("ID"), Gff::FieldType::Dword);
    EXPECT_EQ(fieldType("Type"), Gff::FieldType::Dword);
    EXPECT_EQ(fieldType("ItemPropertyInde"), Gff::FieldType::Dword);

    auto saved = SavedActionRecord::fromGff(
        *actionRecord, snapshotIdentityContext());
    EXPECT_EQ(saved.actionId, 37u);
    EXPECT_EQ(saved.groupActionId, 18);
    EXPECT_EQ(saved.declaredParameterCount, 1);
    ASSERT_EQ(saved.parameters.size(), 1);
    EXPECT_EQ(saved.parameters[0].type,
              static_cast<uint32_t>(SavedActionParameterType::ScriptSituation));
    auto &situation = std::get<SerializedScriptSituation>(saved.parameters[0].payload);
    EXPECT_EQ(situation.basePointer, 2);
    EXPECT_EQ(situation.stackPointer, 3);
    ASSERT_EQ(situation.stack.size(), 3);
    ASSERT_EQ(situation.stack[1].type, static_cast<int8_t>(SavedVmStackType::Talent));
    const auto &talent = std::get<SavedTalentValue>(situation.stack[1].payload);
    EXPECT_EQ(talent.id, 123);
    EXPECT_EQ(talent.type, static_cast<int32_t>(TalentType::Spell));
    EXPECT_EQ(talent.multiClass, 2);
    EXPECT_EQ(talent.item.id, 200u);
    EXPECT_NE(talent.item.id, talentItem->id());
    EXPECT_EQ(talent.itemPropertyIndex, 5);
    EXPECT_EQ(talent.casterLevel, 14);
    EXPECT_EQ(talent.metaType, 1);
    EXPECT_EQ(std::get<std::string>(situation.stack[2].payload), "after-talent");
    ASSERT_TRUE(situation.bindObjectReferences(game));
    EXPECT_EQ(talent.item.boundObject(), talentItem);
}

TEST_F(SnapshotFixture, live_effect_continuation_translates_creator_and_all_object_slots) {
    auto creator = game.newCreature();
    TestGameModule::addSnapshotObject(*area, creator);
    std::array<std::shared_ptr<Object>, 4> targets;
    for (auto &target : targets) {
        target = game.newCreature();
        TestGameModule::addSnapshotObject(
            *area, std::static_pointer_cast<Creature>(target));
    }

    creator->assignSerializedObjectIdentity(
        {snapshotIdentityContext(), 300u});
    game.registerSavedObjectIdentity(
        300u, creator, snapshotIdentityContext());
    for (size_t index = 0; index < targets.size(); ++index) {
        const uint32_t savedId = 301u + static_cast<uint32_t>(index);
        targets[index]->assignSerializedObjectIdentity(
            {snapshotIdentityContext(), savedId});
        game.registerSavedObjectIdentity(
            savedId, targets[index], snapshotIdentityContext());
        ASSERT_NE(targets[index]->id(), savedId);
    }

    auto program = std::make_shared<script::ScriptProgram>("effect_command");
    program->add(script::Instruction(script::InstructionType::RETN));
    auto state = std::make_shared<script::ExecutionState>();
    state->program = std::move(program);
    state->insOffset = 13;
    state->globals = {script::Variable::ofEffect(
        std::make_shared<CompleteReferenceEffect>(creator, targets))};
    auto context = std::make_shared<script::ExecutionContext>();
    context->savedState = std::move(state);
    auto action = game.newAction<DoCommandAction>(std::move(context));
    player->addAction(action);

    auto result = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto actionRecord = ifo->getList("Mod_PlayerList")
                            .front()
                            ->getList("ActionList")
                            .front();
    auto saved = SavedActionRecord::fromGff(
        *actionRecord, snapshotIdentityContext());
    auto &situation = std::get<SerializedScriptSituation>(
        saved.parameters.front().payload);
    auto &effect = std::get<EffectInstance>(situation.stack.front().payload);
    EXPECT_EQ(effect.creatorId, 300u);
    EXPECT_EQ(
        effect.objectParameters,
        (std::array<uint32_t, 4> {301u, 302u, 303u, 304u}));

    ASSERT_TRUE(situation.bindObjectReferences(game));
    EXPECT_EQ(effect.boundCreator(), creator);
    for (size_t index = 0; index < targets.size(); ++index) {
        EXPECT_EQ(effect.boundObjectParameter(index), targets[index]);
    }
    auto imported = SavedScriptSituationImporter(
                        game, engine.resourceModule().scripts())
                        .import(situation);
    ASSERT_TRUE(imported) << imported.message;
    auto runtimeEffect = std::dynamic_pointer_cast<SavedEffectValue>(
        imported.continuation->executionState().globals.front().engineType);
    ASSERT_TRUE(runtimeEffect);
    EXPECT_EQ(runtimeEffect->instance().creatorId, creator->id());
    for (size_t index = 0; index < targets.size(); ++index) {
        EXPECT_EQ(
            runtimeEffect->instance().objectParameters[index],
            targets[index]->id());
    }
}

TEST_F(SnapshotFixture, runtime_delays_export_as_retail_timed_events_with_remaining_game_time) {
    TestGameModule::setSnapshotWorldTime(game, 3, 1000, 5);
    auto newDelayedCommand = [&](int value) {
        auto program = std::make_shared<script::ScriptProgram>("delayed_command");
        program->add(script::Instruction(script::InstructionType::RETN));
        auto state = std::make_shared<script::ExecutionState>();
        state->program = std::move(program);
        state->insOffset = 13;
        state->globals = {script::Variable::ofInt(value)};
        auto context = std::make_shared<script::ExecutionContext>();
        context->savedState = std::move(state);
        return game.newAction<DoCommandAction>(std::move(context));
    };
    player->delayAction(newDelayedCommand(9), 10.0f);
    player->update(4.0f);

    auto first = ModuleSnapshotBuilder(game, "module003").build();
    auto second = ModuleSnapshotBuilder(game, "module003").build();

    ASSERT_TRUE(first) << first.message;
    ASSERT_TRUE(second) << second.message;
    EXPECT_EQ(first.snapshot->ifoBytes, second.snapshot->ifoBytes);
    auto ifo = readGff(first.snapshot->ifoBytes);
    auto queue = SavedEventQueue::fromGff(
        *ifo, snapshotIdentityContext());
    ASSERT_EQ(queue.events.size(), 1);
    const auto &event = queue.events.front();
    EXPECT_EQ(event.eventId, static_cast<uint32_t>(SavedEventType::Timed));
    EXPECT_EQ(event.day, 3u);
    // Six simulation seconds. World-time milliseconds are real milliseconds.
    EXPECT_EQ(event.time, 7000u);
    EXPECT_EQ(event.object.id, player->id());
    EXPECT_EQ(event.caller.id, player->id());
    const auto *situation = std::get_if<SerializedScriptSituation>(&event.payload);
    ASSERT_NE(situation, nullptr);
    ASSERT_EQ(situation->stack.size(), 1);
    EXPECT_EQ(std::get<int32_t>(situation->stack.front().payload), 9);
}

TEST_F(SnapshotFixture, structural_module_references_restore_onto_a_different_runtime_id) {
    const auto context =
        SerializedIdentityContext::moduleGraph("tat_m17ab");
    SavedEventRecord event;
    event.day = 8;
    event.time = 9123;
    event.object = SavedObjectReference::fromRuntimeId(game.module()->id());
    event.caller = SavedObjectReference::fromRuntimeId(game.module()->id());
    event.eventId = static_cast<uint32_t>(SavedEventType::CloseObject);
    game.module()->enqueueSaveEvent(event);

    auto saved = ModuleSnapshotBuilder(game, "tat_m17ab").build();

    ASSERT_TRUE(saved) << saved.message;
    uint32_t privateModuleId = 0;
    EXPECT_FALSE(saved.snapshot->ifo->readDword(
        privateModuleId, "ReoneModObjId"));
    auto serialized = SavedEventQueue::fromGff(
        *saved.snapshot->ifo, snapshotIdentityContext());
    ASSERT_EQ(serialized.events.size(), 1u);
    EXPECT_EQ(
        serialized.events.front().object.id,
        kSavedRuntimeModuleObjectId);
    EXPECT_EQ(
        serialized.events.front().caller.id,
        kSavedRuntimeModuleObjectId);

    Game restored(
        GameID::KotOR, "", engine.options(), engine.services(), console);
    restored.prepareSavedRuntimeNamespace(*saved.snapshot->ifo, context);
    auto restoredModule = restored.newSavedModule();
    ASSERT_NE(restoredModule->id(), kSavedRuntimeModuleObjectId);
    TestGameModule::registerSavedModuleReferenceTarget(
        restored, restoredModule, context);
    restoredModule->deserializeSavedEventQueue(
        *saved.snapshot->ifo, context);
    restoredModule->bindSavedEventQueue();
    const auto &events = restoredModule->savedEventQueue().events;
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().object.boundObject(), restoredModule);
    EXPECT_EQ(events.front().caller.boundObject(), restoredModule);
}

TEST_F(SnapshotFixture, recovered_event_payload_references_renumber_symmetrically) {
    const auto sourceContext =
        SerializedIdentityContext::moduleGraph("source-module");
    game.registerSavedObjectIdentity(390u, player, sourceContext);
    std::vector<std::shared_ptr<Creature>> targets;
    for (uint32_t index = 0; index < 5; ++index) {
        auto target = game.newCreature();
        target->setTag("event_ref_" + std::to_string(index));
        TestGameModule::addSnapshotObject(*area, target);
        game.registerSavedObjectIdentity(
            400u + index, target, sourceContext);
        targets.push_back(std::move(target));
    }

    auto aoo = Gff::Builder().type(0x3333)
                   .field(Gff::Field::newDword("Value", 400u)).build();
    auto combat = Gff::Builder().type(0x2222)
                      .field(Gff::Field::newDword("ReactObject", 401u))
                      .field(Gff::Field::newDword("AmmoItem", 402u))
                      .field(Gff::Field::newInt("AttackType", 17)).build();
    auto feedbackObject1 = Gff::Builder().type(0xbaad)
                               .field(Gff::Field::newDword(
                                   "ObjectValue", 403u)).build();
    auto feedbackObject2 = Gff::Builder().type(0xbaad)
                               .field(Gff::Field::newDword(
                                   "ObjectValue", 404u)).build();
    auto feedback = Gff::Builder().type(0xcccc)
                        .field(Gff::Field::newByte("Type", 9))
                        .field(Gff::Field::newList(
                            "ObjectIDList",
                            {feedbackObject1, feedbackObject2})).build();
    auto spell = Gff::Builder().type(0x6666)
                     .field(Gff::Field::newDword("CasterId", 400u))
                     .field(Gff::Field::newDword("TargetId", 401u))
                     .field(Gff::Field::newDword("AreaId", 402u))
                     .field(Gff::Field::newDword("ItemId", 403u)).build();
    auto makeEvent = [&](uint32_t type, std::shared_ptr<Gff> data) {
        return Gff::Builder().type(0xabcd)
            .field(Gff::Field::newDword("Day", 1))
            .field(Gff::Field::newDword("Time", type))
            .field(Gff::Field::newDword("ObjectId", 390u))
            .field(Gff::Field::newDword("CallerId", 390u))
            .field(Gff::Field::newDword("EventId", type))
            .field(Gff::Field::newStruct("EventData", std::move(data)))
            .build();
    };
    for (const auto &[type, data] :
         std::vector<std::pair<uint32_t, std::shared_ptr<Gff>>> {
             {20u, aoo}, {15u, combat}, {22u, feedback}, {19u, spell}}) {
        auto event = SavedEventRecord::fromGff(
            *makeEvent(type, data), sourceContext);
        ASSERT_TRUE(event.bindObjectReferences(game));
        game.module()->enqueueSaveEvent(std::move(event));
    }

    auto saved = ModuleSnapshotBuilder(game, "module003").build();

    ASSERT_TRUE(saved) << saved.message;
    std::map<std::string, uint32_t> idByTag;
    for (const auto &record : saved.snapshot->git->getList("Creature List")) {
        idByTag.emplace(
            record->getString("Tag"), record->getUint("ObjectId"));
    }
    ASSERT_EQ(idByTag.size(), targets.size());
    for (uint32_t index = 0; index < targets.size(); ++index) {
        EXPECT_NE(idByTag.at("event_ref_" + std::to_string(index)),
                  400u + index);
    }
    std::map<uint32_t, std::shared_ptr<Gff>> eventByType;
    for (const auto &record : saved.snapshot->ifo->getList("EventQueue")) {
        eventByType.emplace(record->getUint("EventId"), record);
    }
    ASSERT_EQ(eventByType.size(), 4u);
    EXPECT_EQ(
        eventByType.at(20u)->findStruct("EventData")->getUint("Value"),
        idByTag.at("event_ref_0"));
    const auto savedCombat = eventByType.at(15u)->findStruct("EventData");
    EXPECT_EQ(savedCombat->getUint("ReactObject"),
              idByTag.at("event_ref_1"));
    EXPECT_EQ(savedCombat->getUint("AmmoItem"),
              idByTag.at("event_ref_2"));
    EXPECT_EQ(savedCombat->getInt("AttackType"), 17);
    const auto savedFeedback = eventByType.at(22u)->findStruct("EventData");
    ASSERT_EQ(savedFeedback->getList("ObjectIDList").size(), 2u);
    EXPECT_EQ(savedFeedback->getList("ObjectIDList")[0]->getUint("ObjectValue"),
              idByTag.at("event_ref_3"));
    EXPECT_EQ(savedFeedback->getList("ObjectIDList")[1]->getUint("ObjectValue"),
              idByTag.at("event_ref_4"));
    EXPECT_EQ(savedFeedback->getUint("Type"), 9u);
    const auto savedSpell = eventByType.at(19u)->findStruct("EventData");
    EXPECT_EQ(savedSpell->getUint("CasterId"), idByTag.at("event_ref_0"));
    EXPECT_EQ(savedSpell->getUint("TargetId"), idByTag.at("event_ref_1"));
    EXPECT_EQ(savedSpell->getUint("AreaId"), idByTag.at("event_ref_2"));
    EXPECT_EQ(savedSpell->getUint("ItemId"), idByTag.at("event_ref_3"));
}

TEST_F(SnapshotFixture, runtime_delays_preserve_stable_time_order_and_fail_closed) {
    // One real second before the end of a game day. At Mod_MinPerHour=10 a day
    // is 10 * 60 * 1000 * 24 ms, so the 2 s and 4 s delays cross midnight.
    TestGameModule::setSnapshotWorldTime(game, 2, 10u * 60u * 1000u * 24u - 1000u, 10);
    auto delayed = [&](Object &owner, float seconds, int value) {
        auto program = std::make_shared<script::ScriptProgram>("ordered_delay");
        program->add(script::Instruction(script::InstructionType::RETN));
        auto state = std::make_shared<script::ExecutionState>();
        state->program = std::move(program);
        state->insOffset = 13;
        state->globals = {script::Variable::ofInt(value)};
        auto context = std::make_shared<script::ExecutionContext>();
        context->savedState = std::move(state);
        owner.delayAction(game.newAction<DoCommandAction>(std::move(context)), seconds);
    };
    auto door = addDoorWithShadow();
    delayed(*player, 4.0f, 1);
    delayed(*player, 2.0f, 2);
    delayed(*door, 2.0f, 3);

    auto result = ModuleSnapshotBuilder(game, "module003").build();

    ASSERT_TRUE(result) << result.message;
    auto queue = SavedEventQueue::fromGff(
        *readGff(result.snapshot->ifoBytes), snapshotIdentityContext());
    ASSERT_EQ(queue.events.size(), 3);
    EXPECT_EQ(queue.events[0].day, 3u);
    EXPECT_EQ(queue.events[0].time, 1000u);
    EXPECT_EQ(queue.events[0].object.id, player->id());
    EXPECT_EQ(queue.events[1].time, 1000u);
    EXPECT_EQ(queue.events[1].object.id, door->id());
    EXPECT_EQ(queue.events[2].time, 3000u);

    door->delayAction(
        std::make_shared<UnserializableAction>(game, engine.services()), 3.0f);
    auto rejected = ModuleSnapshotBuilder(game, "module003").build();
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error, ModuleSnapshotError::UnsupportedLiveState);
    EXPECT_THAT(rejected.message, HasSubstr("ownerId=" + std::to_string(door->id())));
    EXPECT_THAT(rejected.message, HasSubstr("delayedIndex=1"));
}

TEST_F(SnapshotFixture, due_delay_is_inert_on_restore_and_delivered_exactly_once) {
    TestGameModule::setSnapshotWorldTime(game, 4, 5000, 2);
    TestGameModule::initSnapshotLocalServices(game);
    auto program = std::make_shared<script::ScriptProgram>("boundary_delay");
    program->add(script::Instruction::newCONSTI(0));
    program->add(script::Instruction(script::InstructionType::RETN));
    auto state = std::make_shared<script::ExecutionState>();
    state->program = std::move(program);
    state->insOffset = 13;
    state->globals = {script::Variable::ofTalent(std::make_shared<Talent>(
        TalentType::Feat, 6, 0, kSavedRuntimeInvalidObjectId, -1, 0xff, 0xff))};
    auto context = std::make_shared<script::ExecutionContext>();
    context->savedState = std::move(state);
    player->delayAction(game.newAction<DoCommandAction>(std::move(context)), 0.0f);
    auto saved = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(saved) << saved.message;

    auto ifo = readGff(saved.snapshot->ifoBytes);
    TestGameModule::clearSnapshotDelayed(*player);
    const auto serialized = SavedEventQueue::fromGff(
        *ifo, snapshotIdentityContext());
    ASSERT_EQ(serialized.events.size(), 1u);
    game.registerSavedObjectIdentity(
        serialized.events.front().object.id,
        player,
        snapshotIdentityContext());
    game.module()->deserializeSavedEventQueue(
        *ifo, snapshotIdentityContext());
    game.module()->bindSavedEventQueue();
    game.module()->publishSavedEventQueue();

    EXPECT_EQ(game.module()->pendingSavedEventCount(), 1u);
    TestGameModule::dispatchSnapshotEvents(*game.module());
    EXPECT_EQ(game.module()->pendingSavedEventCount(), 0u);
    TestGameModule::dispatchSnapshotEvents(*game.module());
    EXPECT_EQ(game.module()->pendingSavedEventCount(), 0u);
}

TEST_F(SnapshotFixture, writes_a_normalized_calendar_pair_split_from_the_absolute_clock) {
    // The runtime clock is absolute milliseconds; the IFO stores a day and a
    // time of day. The split happens here, at the serialization boundary, so
    // every record written is normalized regardless of where the clock stands.
    constexpr uint8_t kMinutesPerHour = 2;
    const uint32_t millisecondsPerDay = kMinutesPerHour * 60u * 1000u * 24u;
    TestGameModule::setSnapshotWorldTime(
        game, 6, millisecondsPerDay - 1000u, kMinutesPerHour);
    TestGameModule::advanceWorldTime(game, 3.0f);

    auto result = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);

    EXPECT_EQ(ifo->getUint("Mod_MinPerHour"), kMinutesPerHour);
    EXPECT_EQ(ifo->getUint("Mod_CalendarDay"), 7u);
    EXPECT_EQ(ifo->getUint("Mod_TimeOfDay"), 2000u);
    EXPECT_LT(ifo->getUint("Mod_TimeOfDay"), millisecondsPerDay);
    // And the pair recomposes to exactly the clock that produced it.
    EXPECT_EQ(static_cast<uint64_t>(ifo->getUint("Mod_CalendarDay")) *
                      millisecondsPerDay +
                  ifo->getUint("Mod_TimeOfDay"),
              game.worldTimeMilliseconds());
}

TEST_F(SnapshotFixture, pending_start_conversation_is_a_supported_transition_snapshot_action) {
    auto target = game.newCreature();
    TestGameModule::addSnapshotObject(*area, target);
    auto action = game.newAction<StartConversationAction>(target, "meeting", false);
    SavedActionRecord provenance;
    provenance.groupActionId = 16;
    action->attachSavedAction(provenance);
    player->addAction(action);

    auto result = ModuleSnapshotBuilder(game, "module003").build();

    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    auto saved = SavedActionRecord::fromGff(
        *ifo->getList("Mod_PlayerList").front()->getList("ActionList").front(),
        snapshotIdentityContext());
    EXPECT_EQ(saved.actionId, 24u);
    EXPECT_EQ(saved.groupActionId, 16);
    EXPECT_EQ(saved.declaredParameterCount, 3);
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[0].payload).id,
              target->id());
    EXPECT_EQ(std::get<std::string>(saved.parameters[1].payload), "meeting");
    EXPECT_EQ(std::get<int32_t>(saved.parameters[2].payload), 0);
}

TEST(ModuleSnapshot, exports_advanced_runtime_script_situations_and_rejects_unsupported_values) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto program = std::make_shared<ScriptProgram>("runtime_save");
    program->add(Instruction::newCONSTI(1));
    uint32_t resume = program->length();
    program->add(Instruction(InstructionType::RETN));
    auto state = std::make_shared<ExecutionState>();
    state->program = program;
    state->insOffset = resume;
    state->globals = {Variable::ofInt(12)};
    state->locals = {
        Variable::ofString("local"),
        Variable::ofTalent(std::make_shared<Talent>(TalentType::Feat, 17)),
        Variable::ofLocation(std::make_shared<Location>(
            glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f)))};
    auto continuation = SavedScriptContinuation::fromRuntime(
        state, "runtime_save", game);
    std::string error;

    auto exported = exportScriptSituation(*continuation, error);

    ASSERT_TRUE(exported) << error;
    EXPECT_EQ(exported->instructionPointer, static_cast<int32_t>(resume - 13));
    EXPECT_EQ(exported->basePointer, 1);
    EXPECT_EQ(exported->stackPointer, 4);
    EXPECT_EQ(exported->codeSize, static_cast<int32_t>(exported->code.size()));
    EXPECT_EQ(exported->stack[2].type, static_cast<int8_t>(SavedVmStackType::Talent));
    EXPECT_EQ(exported->stack[3].type, static_cast<int8_t>(SavedVmStackType::Location));

    state->locals.push_back(Variable::ofVector({1.0f, 2.0f, 3.0f}));
    error.clear();
    EXPECT_FALSE(exportScriptSituation(*continuation, error));
    EXPECT_THAT(error, HasSubstr("unsupported type="));
}

TEST_F(SnapshotFixture, follow_leader_and_later_action_preserve_transition_queue_order) {
    auto follow = game.newAction<FollowLeaderAction>();
    SavedActionRecord followProvenance;
    followProvenance.groupActionId = 40;
    follow->attachSavedAction(followProvenance);
    player->addAction(follow);

    auto later = game.newAction<WaitAction>(3.0f);
    SavedActionRecord laterProvenance;
    laterProvenance.groupActionId = 41;
    later->attachSavedAction(laterProvenance);
    player->addAction(later);

    auto result = ModuleSnapshotBuilder(game, "module003").build();

    ASSERT_TRUE(result) << result.message;
    auto ifo = readGff(result.snapshot->ifoBytes);
    const auto &savedQueue =
        ifo->getList("Mod_PlayerList").front()->getList("ActionList");
    ASSERT_EQ(savedQueue.size(), 2);
    auto savedFollow = SavedActionRecord::fromGff(
        *savedQueue[0], snapshotIdentityContext());
    auto savedLater = SavedActionRecord::fromGff(
        *savedQueue[1], snapshotIdentityContext());
    EXPECT_EQ(savedFollow.actionId, 61u);
    EXPECT_EQ(savedFollow.groupActionId, 40);
    EXPECT_EQ(savedFollow.declaredParameterCount, 0);
    EXPECT_TRUE(savedFollow.parameters.empty());
    EXPECT_EQ(savedLater.actionId, 30u);
    EXPECT_EQ(savedLater.groupActionId, 41);
    EXPECT_TRUE(std::dynamic_pointer_cast<FollowLeaderAction>(
        savedFollow.toRuntimeAction(game)));
    EXPECT_TRUE(std::dynamic_pointer_cast<WaitAction>(
        savedLater.toRuntimeAction(game)));
}

TEST(ModuleSnapshot, exports_retail_zero_location_for_uninitialized_runtime_location) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto program = std::make_shared<ScriptProgram>("k_ai_master");
    program->add(Instruction::newCONSTI(1));
    uint32_t resume = program->length();
    program->add(Instruction(InstructionType::RETN));

    auto state = std::make_shared<ExecutionState>();
    state->program = program;
    state->insOffset = resume;
    state->globals.assign(174, Variable::ofInt(0));
    state->locals = {Variable::ofLocation(nullptr)};
    state->locals.insert(state->locals.end(), 8, Variable::ofInt(0));
    auto context = std::make_shared<ExecutionContext>();
    context->savedState = state;
    auto action = game.newAction<DoCommandAction>(std::move(context));

    for (int attempt = 0; attempt < 3; ++attempt) {
        SCOPED_TRACE(attempt);
        auto exportedAction = action->saveFacingState();

        ASSERT_TRUE(exportedAction);
        ASSERT_EQ(exportedAction->actionId, 37u);
        ASSERT_EQ(exportedAction->parameters.size(), 1u);
        const auto &exported = std::get<SerializedScriptSituation>(
            exportedAction->parameters.front().payload);
        EXPECT_EQ(exported.basePointer, 174);
        EXPECT_EQ(exported.stackPointer, 183);
        ASSERT_EQ(exported.stack.size(), 183u);
        const auto &saved = exported.stack[174];
        EXPECT_EQ(saved.type, static_cast<int8_t>(SavedVmStackType::Location));
        const auto &location = std::get<SavedLocationValue>(saved.payload);
        EXPECT_EQ(location.position, glm::vec3(0.0f));
        EXPECT_EQ(location.orientation, glm::vec3(0.0f));

        // Snapshotting is observational: the live default location remains
        // an uninitialized engine slot for the pending command to consume.
        EXPECT_EQ(state->locals[0].type, VariableType::Location);
        EXPECT_FALSE(state->locals[0].engineType);
    }
}

TEST_F(SnapshotFixture, creature_records_carry_the_creation_script_flag) {
    auto spawned = game.newCreature();
    auto unspawned = game.newCreature();
    spawned->runSpawnScript();
    TestGameModule::addSnapshotObject(*area, spawned);
    TestGameModule::addSnapshotObject(*area, unspawned);

    auto saved = ModuleSnapshotBuilder(game, "tat_m17ab").build();

    ASSERT_TRUE(saved) << saved.message;
    auto spawnedRecord = recordById(*saved.snapshot->git, "Creature List", spawned->id());
    auto unspawnedRecord = recordById(*saved.snapshot->git, "Creature List", unspawned->id());
    ASSERT_TRUE(spawnedRecord);
    ASSERT_TRUE(unspawnedRecord);
    EXPECT_EQ(1, spawnedRecord->getInt("CreatnScrptFird"));
    EXPECT_EQ(0, unspawnedRecord->getInt("CreatnScrptFird"));
}

} // namespace
