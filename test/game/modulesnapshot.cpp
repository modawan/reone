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

#include "reone/game/action/wait.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/playanimation.h"
#include "reone/game/action/docommand.h"
#include "reone/game/action/followleader.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/startconversation.h"
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
    game._worldTimeDay = day;
    game._worldTimeOfDay = time;
    game._minutesPerHour = minutesPerHour;
}

void reone::game::TestGameModule::deserializeSnapshotRuntimeState(
    Object &object, const Gff &gff) {
    object.deserializeRuntimeState(gff);
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
            *source, {SaveRecordOriginKind::ActiveGitObject, "tat_m17ab"});
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
    auto camera = game.newStaticCamera(4.0f / 3.0f);
    TestGameModule::configureSnapshotCamera(
        *camera, 7, {4.0f, 5.0f, 6.0f},
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f), 12.0f, 1.5f, 55.0f, 8.0f);
    camera->captureSaveRecord(
        *cameraSource, {SaveRecordOriginKind::ActiveGitObject, "tat_m17ab"});
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
        *triggerShadow, {SaveRecordOriginKind::ActiveGitObject, "tat_m17ab"});
    TestGameModule::deserializeSnapshotRuntimeState(*trigger, *triggerShadow);
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
    TestGameModule::deserializeSnapshotRuntimeState(e2Module, *ifo);
    EXPECT_TRUE(e2Module.getLocalBoolean(7));
    EXPECT_EQ(e2Module.getLocalNumber(3), 44);
    ASSERT_EQ(ifo->getList("EventQueue").size(), 1);
    EXPECT_EQ(ifo->getList("EventQueue")[0]->getUint("Day"), 8u);
    EXPECT_EQ(ifo->getList("EventQueue")[0]->getUint("Time"), 9123u);
    const auto e2Events = SavedEventQueue::fromGff(*ifo);
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
            *playerRecord->getList("ActionList").front()).parameters.front().payload),
        20.0f);
    auto effect = EffectInstance::fromGff(
        *playerRecord->getList("EffectList").front());
    EXPECT_EQ(effect.expiryDay, 3u);
    EXPECT_EQ(effect.expiryTime, 241000u);
    ASSERT_TRUE(game.remainingEffectDuration(effect));
    EXPECT_FLOAT_EQ(*game.remainingEffectDuration(effect), 20.0f);
    Creature e2Creature(player->id() + 1000, "", game, engine.services());
    TestGameModule::deserializeSnapshotRuntimeState(e2Creature, *playerRecord);
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
        item->captureOwnerLocalSaveRecord(*record, {kind, owner});
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

TEST_F(SnapshotFixture, retained_module_item_collision_is_rejected) {
    auto owner = game.newPlaceable();
    auto item = game.newOwnedItem();
    auto record = Gff::Builder().type(0)
        .field(Gff::Field::newDword("ObjectId", owner->id())).build();
    item->captureOwnerLocalSaveRecord(
        *record, {SaveRecordOriginKind::PlaceableItem, "owner"});
    owner->addItem(item);
    TestGameModule::addSnapshotObject(*area, owner);

    auto saved = ModuleSnapshotBuilder(game, "module005").build();

    EXPECT_FALSE(saved);
    EXPECT_EQ(saved.error, ModuleSnapshotError::UnsupportedLiveState);
    EXPECT_THAT(saved.message, HasSubstr("retained module item ID collides"));
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
    reloaded.prepareSavedRuntimeNamespace(*saved.snapshot->ifo);
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
    auto companion = game.newCreature();
    TestGameModule::setSnapshotObjectId(*companion, 0x7fffffffu);
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
TEST_F(SnapshotFixture, preserves_open2_and_accepts_retail_world_ids_zero_and_one) {
    auto door = game.newDoor();
    auto placeable = game.newPlaceable();
    TestGameModule::setSnapshotObjectId(*door, 0);
    TestGameModule::setSnapshotObjectId(*placeable, 1);
    TestGameModule::setSnapshotDoorState(*door, DoorState::Opened2);
    TestGameModule::addSnapshotObject(*area, door);
    TestGameModule::addSnapshotObject(*area, placeable);

    auto saved = ModuleSnapshotBuilder(game, "module007").build();

    ASSERT_TRUE(saved) << saved.message;
    auto doorRecord = recordById(*saved.snapshot->git, "Door List", 0);
    ASSERT_TRUE(doorRecord);
    EXPECT_EQ(doorRecord->getUint("OpenState"), 2u);
    EXPECT_TRUE(recordById(*saved.snapshot->git, "Placeable List", 1));
}

TEST_F(SnapshotFixture, rejects_duplicate_authoritative_world_ids) {
    auto first = game.newDoor();
    auto second = game.newPlaceable();
    TestGameModule::setSnapshotObjectId(*first, 77);
    TestGameModule::setSnapshotObjectId(*second, 77);
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
        *source, {SaveRecordOriginKind::ActiveGitObject, "tat_m17ab"});
    TestGameModule::deserializeSnapshotRuntimeState(*trigger, *source);
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
        *playerRecord->getList("ActionList").front());
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
        *playerRecord->getList("ActionList").front());
    EXPECT_EQ(saved.actionId, 12u);
    EXPECT_EQ(saved.groupActionId, 29);
    EXPECT_EQ(saved.declaredParameterCount, 10);
    ASSERT_EQ(saved.parameters.size(), 10);
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[1].payload).id,
              target->id());
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
        *playerRecord->getList("ActionList").front());
    EXPECT_EQ(saved.actionId, 1u);
    EXPECT_EQ(saved.groupActionId, 33);
    EXPECT_EQ(saved.declaredParameterCount, 13);
    ASSERT_EQ(saved.parameters.size(), 13);
    EXPECT_TRUE(std::get<SavedObjectReference>(saved.parameters[4].payload).isInvalid());
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
        *playerRecord->getList("ActionList").front());
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
        *ifo->getList("Mod_PlayerList").front()->getList("ActionList").front());
    EXPECT_EQ(saved.actionId, 1u);
    EXPECT_EQ(saved.groupActionId, 13);
    ASSERT_EQ(saved.parameters.size(), 13);
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[3].payload).id, area->id());
    EXPECT_EQ(std::get<SavedObjectReference>(saved.parameters[4].payload).id, target->id());
    EXPECT_EQ(std::get<int32_t>(saved.parameters[5].payload), 4);
    EXPECT_FLOAT_EQ(std::get<float>(saved.parameters[8].payload), 30.0f);
}

TEST_F(SnapshotFixture, pending_do_command_is_a_supported_transition_snapshot_action) {
    auto program = std::make_shared<script::ScriptProgram>("transition_command");
    program->add(script::Instruction(script::InstructionType::RETN));
    auto state = std::make_shared<script::ExecutionState>();
    state->program = std::move(program);
    state->insOffset = 13;
    state->globals = {
        script::Variable::ofInt(9),
        script::Variable::ofTalent(std::make_shared<Talent>(
            TalentType::Spell, 123, 2, kSavedRuntimeInvalidObjectId, 5, 14, 1))};
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

    auto saved = SavedActionRecord::fromGff(*actionRecord);
    EXPECT_EQ(saved.actionId, 37u);
    EXPECT_EQ(saved.groupActionId, 18);
    EXPECT_EQ(saved.declaredParameterCount, 1);
    ASSERT_EQ(saved.parameters.size(), 1);
    EXPECT_EQ(saved.parameters[0].type,
              static_cast<uint32_t>(SavedActionParameterType::ScriptSituation));
    const auto &situation = std::get<SerializedScriptSituation>(saved.parameters[0].payload);
    EXPECT_EQ(situation.basePointer, 2);
    EXPECT_EQ(situation.stackPointer, 3);
    ASSERT_EQ(situation.stack.size(), 3);
    ASSERT_EQ(situation.stack[1].type, static_cast<int8_t>(SavedVmStackType::Talent));
    const auto &talent = std::get<SavedTalentValue>(situation.stack[1].payload);
    EXPECT_EQ(talent.id, 123);
    EXPECT_EQ(talent.type, static_cast<int32_t>(TalentType::Spell));
    EXPECT_EQ(talent.multiClass, 2);
    EXPECT_EQ(talent.item.id, kSavedRuntimeInvalidObjectId);
    EXPECT_EQ(talent.itemPropertyIndex, 5);
    EXPECT_EQ(talent.casterLevel, 14);
    EXPECT_EQ(talent.metaType, 1);
    EXPECT_EQ(std::get<std::string>(situation.stack[2].payload), "after-talent");
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
    auto queue = SavedEventQueue::fromGff(*ifo);
    ASSERT_EQ(queue.events.size(), 1);
    const auto &event = queue.events.front();
    EXPECT_EQ(event.eventId, static_cast<uint32_t>(SavedEventType::Timed));
    EXPECT_EQ(event.day, 3u);
    // Six simulation seconds at five real minutes per game hour.
    EXPECT_EQ(event.time, 73000u);
    EXPECT_EQ(event.object.id, player->id());
    EXPECT_EQ(event.caller.id, player->id());
    const auto *situation = std::get_if<SerializedScriptSituation>(&event.payload);
    ASSERT_NE(situation, nullptr);
    ASSERT_EQ(situation->stack.size(), 1);
    EXPECT_EQ(std::get<int32_t>(situation->stack.front().payload), 9);
}

TEST_F(SnapshotFixture, runtime_delays_preserve_stable_time_order_and_fail_closed) {
    TestGameModule::setSnapshotWorldTime(game, 2, 86390000, 10);
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
    auto queue = SavedEventQueue::fromGff(*readGff(result.snapshot->ifoBytes));
    ASSERT_EQ(queue.events.size(), 3);
    EXPECT_EQ(queue.events[0].day, 3u);
    EXPECT_EQ(queue.events[0].time, 2000u);
    EXPECT_EQ(queue.events[0].object.id, player->id());
    EXPECT_EQ(queue.events[1].time, 2000u);
    EXPECT_EQ(queue.events[1].object.id, door->id());
    EXPECT_EQ(queue.events[2].time, 14000u);

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
    game.module()->deserializeSavedEventQueue(*ifo);
    game.module()->bindSavedEventQueue();
    game.module()->publishSavedEventQueue();

    EXPECT_EQ(game.module()->pendingSavedEventCount(), 1u);
    TestGameModule::dispatchSnapshotEvents(*game.module());
    EXPECT_EQ(game.module()->pendingSavedEventCount(), 0u);
    TestGameModule::dispatchSnapshotEvents(*game.module());
    EXPECT_EQ(game.module()->pendingSavedEventCount(), 0u);
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
        *ifo->getList("Mod_PlayerList").front()->getList("ActionList").front());
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
    auto savedFollow = SavedActionRecord::fromGff(*savedQueue[0]);
    auto savedLater = SavedActionRecord::fromGff(*savedQueue[1]);
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

} // namespace
