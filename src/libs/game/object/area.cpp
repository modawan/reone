/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/game/object/area.h"

#include <array>
#include <cmath>

#include "reone/game/minigame.h"

#include "reone/game/camerastyles.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/door.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/room.h"
#include "reone/game/script/runner.h"
#include "reone/game/surfaces.h"
#include "reone/game/types.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/walkmesh.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/layouts.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/paths.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/provider/visibilities.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/collision.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/grass.h"
#include "reone/scene/node/grasscluster.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/sound.h"
#include "reone/scene/node/trigger.h"
#include "reone/scene/node/walkmesh.h"
#include "reone/scene/types.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

using namespace reone::audio;
using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static constexpr float kDefaultFieldOfView = 75.0f;
static constexpr float kUpdatePerceptionInterval = 1.0f; // seconds
static constexpr float kLineOfSightHeight = 1.7f;        // TODO: make it appearance-based

static constexpr float kMaxCollisionDistance = 8.0f;
static constexpr float kMaxCollisionDistance2 = kMaxCollisionDistance * kMaxCollisionDistance;
static constexpr float kCreatureCollisionEpsilon = 0.01f;

static constexpr std::array<glm::vec3, 2> kPartyFormationOffsets {{
    glm::vec3(1.5f, -0.7f, 0.0f),
    glm::vec3(-1.5f, 0.8f, 0.0f),
}};
static constexpr float kPartyPositionSearchRadius = 10.0f;
static constexpr float kPartyPositionSearchStep = 1.0f;
static constexpr int kPartyPositionSearchDirections = 16;
static constexpr float kPartyMemberSpacing = 1.5f;
static constexpr float kPartyMemberSpacing2 = kPartyMemberSpacing * kPartyMemberSpacing;

static glm::vec3 g_defaultAmbientColor {0.2f};
static CameraStyle g_defaultCameraStyle {"", 3.2f, 83.0f, 0.45f, 55.0f};

static bool sweepCircle(
    const glm::vec2 &origin,
    const glm::vec2 &destination,
    const glm::vec2 &center,
    float radius,
    float &outTime,
    glm::vec2 &outNormal) {
    glm::vec2 movement(destination - origin);
    float movementLength2 = glm::dot(movement, movement);
    if (movementLength2 == 0.0f) {
        return false;
    }

    glm::vec2 offset(origin - center);
    float radius2 = radius * radius;
    float originDistance2 = glm::dot(offset, offset);
    if (originDistance2 <= radius2) {
        if (originDistance2 == 0.0f || glm::dot(movement, offset) >= 0.0f) {
            return false;
        }

        outTime = 0.0f;
        outNormal = glm::normalize(offset);
        return true;
    }

    float projection = glm::dot(offset, movement);
    float discriminant = projection * projection - movementLength2 * (originDistance2 - radius2);
    if (discriminant < 0.0f) {
        return false;
    }

    float time = (-projection - std::sqrt(discriminant)) / movementLength2;
    if (time < 0.0f || time > 1.0f) {
        return false;
    }

    glm::vec2 contact(origin + movement * time);
    outTime = time;
    outNormal = glm::normalize(contact - center);
    return true;
}

Area::Area(
    uint32_t id,
    std::string sceneName,
    Game &game,
    ServicesView &services) :
    Object(
        id,
        ObjectType::Area,
        "",
        game,
        services),
    _sceneName(std::move(sceneName)) {

    init();
    _heartbeatTimer.reset(kHeartbeatInterval);
}

void Area::init() {
    _objectsByType.insert(std::make_pair(ObjectType::Creature, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::Item, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::Trigger, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::Door, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::AreaOfEffect, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::Waypoint, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::Placeable, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::Store, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::Encounter, ObjectList()));
    _objectsByType.insert(std::make_pair(ObjectType::Sound, ObjectList()));
}

void Area::load(
    std::string name,
    const Gff &are,
    const Gff &git,
    const SerializedIdentityContext &identityContext) {
    _name = std::move(name);
    if (identityContext.isSerializedState()) {
        _game.captureSaveResourceShadow(
            {SaveResourceKind::AreaAre, _name}, are);
        _game.captureSaveResourceShadow(
            {SaveResourceKind::AreaGit, _name}, git);
    }

    auto areParsed = resource::generated::parseARE(are);
    auto gitParsed = resource::generated::parseGIT(git);
    deserializeRuntimeState(are, identityContext);

    loadARE(areParsed);
    loadLYT();
    loadGIT(gitParsed, git, identityContext);
    loadVIS();
}

void Area::activate() {
    // Map is presentation state owned by Game, while loaded areas are cached.
    // Restore this area's map whenever a cached module becomes active again.
    _game.map().load(_name, _map);
    applySceneProperties();

    for (auto &pair : _rooms) {
        attachRoomToSceneGraph(*pair.second);
        // Enable room walkmeshes for initial party landing; loadParty recalculates visibility after placement.
        pair.second->setVisible(true);
    }
    for (auto &object : _objects) {
        attachObjectToSceneGraph(object);
    }
}

void Area::loadARE(const resource::generated::ARE &are) {
    _localizedName = _services.resource.strings.getText(are.Name.first);

    loadCameraStyle(are);
    loadAmbientColor(are);
    loadScripts(are);
    loadMap(are);
    loadStealthXP(are);
    loadGrass(are);
    loadFog(are);
    loadMiniGame(are);
}

void Area::loadCameraStyle(const resource::generated::ARE &are) {
    // Area
    int areaStyleIdx = are.CameraStyle;
    std::shared_ptr<CameraStyle> areaStyle(_services.game.cameraStyles.get(areaStyleIdx));
    if (areaStyle) {
        _camStyleDefault = *areaStyle;
    } else {
        _camStyleDefault = g_defaultCameraStyle;
    }

    // Combat
    std::shared_ptr<CameraStyle> combatStyle(_services.game.cameraStyles.get("Combat"));
    if (combatStyle) {
        _camStyleDefault = *combatStyle;
    } else {
        _camStyleCombat = g_defaultCameraStyle;
    }
}

void Area::loadAmbientColor(const resource::generated::ARE &are) {
    _ambientColor = are.DynAmbientColor > 0 ? Gff::colorFromUint32(are.DynAmbientColor) : g_defaultAmbientColor;

    applySceneProperties();
}

void Area::loadScripts(const resource::generated::ARE &are) {
    _onEnter = are.OnEnter;
    _onExit = are.OnExit;
    _onHeartbeat = are.OnHeartbeat;
    _onUserDefined = are.OnUserDefined;
}

void Area::loadMap(const resource::generated::ARE &are) {
    _map = are.Map;
    _game.map().load(_name, _map);
}

void Area::loadStealthXP(const resource::generated::ARE &are) {
    _stealthXPEnabled = are.StealthXPEnabled;
    _stealthXPDecrement = are.StealthXPLoss; // TODO: loss = decrement?
    _maxStealthXP = are.StealthXPMax;
}

void Area::loadGrass(const resource::generated::ARE &are) {
    std::string texName(boost::to_lower_copy(are.Grass_TexName));
    if (!texName.empty()) {
        _grass.texture = _services.resource.textures.get(texName, TextureUsage::MainTex);
    }
    _grass.density = are.Grass_Density;
    _grass.quadSize = are.Grass_QuadSize;
    _grass.ambient = are.Grass_Ambient;
    _grass.diffuse = are.Grass_Diffuse;
    _grass.probabilities[0] = are.Grass_Prob_UL;
    _grass.probabilities[1] = are.Grass_Prob_UR;
    _grass.probabilities[2] = are.Grass_Prob_LL;
    _grass.probabilities[3] = are.Grass_Prob_LR;
}

void Area::loadFog(const resource::generated::ARE &are) {
    _fogEnabled = are.SunFogOn;
    _fogNear = are.SunFogNear;
    _fogFar = are.SunFogFar;
    _fogColor = Gff::colorFromUint32(are.SunFogColor);

    applySceneProperties();
}

void Area::loadMiniGame(const resource::generated::ARE &are) {
    _miniGameSpec = parseMinigameSpec(are);
}

void Area::applySceneProperties() {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    sceneGraph.setAmbientLightColor(_ambientColor);

    auto fogProperties = FogProperties();
    fogProperties.enabled = _fogEnabled;
    fogProperties.nearPlane = _fogNear;
    fogProperties.farPlane = _fogFar;
    fogProperties.color = _fogColor;
    sceneGraph.setFog(fogProperties);
}

void Area::loadGIT(
    const resource::generated::GIT &git,
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    _game.reserveSavedObjectIds(gff, identityContext, SerializedGraphRoot::AreaGit);
    loadProperties(git);
    loadCreatures(gff, identityContext);
    loadDoors(gff, identityContext);
    loadPlaceables(gff, identityContext);
    loadWaypoints(gff, identityContext);
    loadTriggers(gff, identityContext);
    loadSounds(gff, identityContext);
    loadCameras(gff, identityContext);
    loadEncounters(gff, identityContext);
    loadStores(gff, identityContext);
    loadItems(gff, identityContext);
}

void Area::loadProperties(const resource::generated::GIT &git) {
    int musicIdx = git.AreaProperties.MusicDay;
    if (musicIdx) {
        std::shared_ptr<TwoDA> musicTable(_services.resource.twoDas.get("ambientmusic"));
        _music = musicTable->getString(musicIdx, "resource");
    }
}

void Area::loadCreatures(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (const auto &creatureGff : gff.getList("Creature List")) {
        auto creature = _game.newCreature(*creatureGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            creature->captureSaveRecord(*creatureGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        landObject(*creature);
        add(creature);
    }
}

void Area::loadDoors(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &doorGff : gff.getList("Door List")) {
        auto door = _game.newDoor(*doorGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            door->captureSaveRecord(*doorGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(door);
    }
}

void Area::loadPlaceables(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &placeableGff : gff.getList("Placeable List")) {
        auto placeable = _game.newPlaceable(*placeableGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            placeable->captureSaveRecord(*placeableGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(placeable);
    }
}

void Area::loadWaypoints(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &waypointGff : gff.getList("WaypointList")) {
        auto waypoint = _game.newWaypoint(*waypointGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            waypoint->captureSaveRecord(*waypointGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(waypoint);
    }
}

void Area::loadTriggers(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &triggerGff : gff.getList("TriggerList")) {
        auto trigger = _game.newTrigger(*triggerGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            trigger->captureSaveRecord(*triggerGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(trigger);
    }
}

void Area::loadSounds(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &soundGff : gff.getList("SoundList")) {
        auto sound = _game.newSound(*soundGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            sound->captureSaveRecord(*soundGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(sound);
    }
}

void Area::loadCameras(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &cameraGff : gff.getList("CameraList")) {
        std::vector<std::shared_ptr<Object>> noObsolete;
        std::shared_ptr<StaticCamera> camera;
        _game.replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                camera = _game.newStaticCamera(_sceneName);
                camera->deserialize(*cameraGff);
            },
            []() noexcept {});
        if (identityContext.isSerializedState()) {
            camera->captureSaveRecord(*cameraGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(camera);
    }
}

void Area::loadEncounters(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &encounterGff : gff.getList("Encounter List")) {
        auto encounter = _game.newEncounter(*encounterGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            encounter->captureSaveRecord(*encounterGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(encounter);
    }
}

void Area::loadStores(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &storeGff : gff.getList("StoreList")) {
        auto store = _game.newStore(*storeGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            store->captureSaveRecord(*storeGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(store);
    }
}


void Area::loadItems(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (auto &itemGff : gff.getList("List")) {
        auto item = _game.newItem(*itemGff, identityContext);
        if (identityContext.isSerializedState()) {
            item->captureSaveRecord(*itemGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(item);
    }
}
void Area::loadLYT() {
    auto layout = _services.resource.layouts.get(_name);
    if (!layout) {
        throw ResourceNotFoundException("Area LYT not found: " + _name);
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto walkableSurfaces = _services.game.surfaces.getWalkableSurfaces();
    for (auto &lytRoom : layout->rooms) {
        auto model = _services.resource.models.get(lytRoom.name);
        if (!model) {
            continue;
        }

        // Model
        glm::vec3 position(lytRoom.position.x, lytRoom.position.y, lytRoom.position.z);
        std::shared_ptr<ModelSceneNode> modelSceneNode(sceneGraph.newModel(*model, ModelUsage::Room));
        modelSceneNode->setLocalTransform(glm::translate(glm::mat4(1.0f), position));

        // Mark room objects as static when not below "{modelName}a" model node
        std::stack<std::reference_wrapper<ModelNode>> modelNodes;
        modelNodes.push(*model->rootNode());
        while (!modelNodes.empty()) {
            auto &modelNode = modelNodes.top().get();
            modelNodes.pop();
            if (modelNode.name() == model->name() + "a") {
                continue;
            }
            auto sceneNode = modelSceneNode->getNodeByName(modelNode.name());
            if (sceneNode) {
                sceneNode->setStatic(true);
            }
            for (auto &child : modelNode.children()) {
                modelNodes.push(*child);
            }
        }

        for (auto &anim : model->getAnimationNames()) {
            if (boost::starts_with(anim, "animloop")) {
                modelSceneNode->playAnimation(anim, nullptr, AnimationProperties::fromFlags(AnimationFlags::loopOverlay));
            }
        }
        sceneGraph.addRoot(modelSceneNode);

        // Walkmesh
        std::shared_ptr<WalkmeshSceneNode> walkmeshSceneNode;
        auto walkmesh = _services.resource.walkmeshes.get(lytRoom.name, ResType::Wok);
        if (walkmesh) {
            walkmeshSceneNode = sceneGraph.newWalkmesh(*walkmesh);
            sceneGraph.addRoot(walkmeshSceneNode);
            uniwalkLoadRoom(_pathfinder.uni, *walkmesh, walkableSurfaces);
        }

        // Grass
        std::shared_ptr<GrassSceneNode> grassSceneNode;
        auto aabbNode = modelSceneNode->model().getAABBNode();
        if (_grass.texture && aabbNode && _game.options().graphics.grass) {
            auto grassProperties = GrassProperties();
            grassProperties.density = _grass.density;
            grassProperties.quadSize = _grass.quadSize;
            grassProperties.probabilities = _grass.probabilities;
            grassProperties.materials = _services.game.surfaces.getGrassSurfaces();
            grassProperties.texture = _grass.texture.get();
            grassSceneNode = sceneGraph.newGrass(grassProperties, *aabbNode);
            grassSceneNode->setLocalTransform(glm::translate(position) * aabbNode->absoluteTransform());
            sceneGraph.addRoot(grassSceneNode);
        }

        auto room = std::make_unique<Room>(lytRoom.name, position, std::move(modelSceneNode), walkmeshSceneNode, std::move(grassSceneNode));
        if (walkmeshSceneNode) {
            walkmeshSceneNode->setUser(*room);
        }
        _rooms.insert(std::make_pair(room->name(), std::move(room)));
    }

    uniwalkFinalize(_pathfinder.uni);
    // Allow up to 64 concurrent paths.
    _pathfinder.paths.resize(64);
}

void Area::loadVIS() {
    auto visibility = _services.resource.visibilities.get(_name);
    if (!visibility) {
        return;
    }
    _visibility = fixVisibility(*visibility);
}

Visibility Area::fixVisibility(const Visibility &visibility) {
    Visibility result;
    for (auto &pair : visibility) {
        result.insert(pair);
        result.insert(std::make_pair(pair.second, pair.first));
    }
    return result;
}

void Area::initCameras(const glm::vec3 &entryPosition, float entryFacing) {
    glm::vec3 position(entryPosition);
    position.z += 1.7f;

    std::vector<std::shared_ptr<Object>> noObsolete;
    _game.replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            _firstPersonCamera = _game.newFirstPersonCamera(
                glm::radians(kDefaultFieldOfView), _sceneName);
            _firstPersonCamera->load();
            _firstPersonCamera->setPosition(position);
            _firstPersonCamera->setFacing(entryFacing);

            _thirdPersonCamera = _game.newThirdPersonCamera(
                _camStyleDefault, _sceneName);
            _thirdPersonCamera->load();
            _thirdPersonCamera->setTargetPosition(position);
            _thirdPersonCamera->setFacing(entryFacing);

            _dialogCamera = _game.newDialogCamera(
                _camStyleDefault, _sceneName);
            _dialogCamera->load();

            _animatedCamera = _game.newAnimatedCamera(_sceneName);
            _animatedCamera->load();
        },
        []() noexcept {});
}

void Area::add(const std::shared_ptr<Object> &object) {
    _objects.push_back(object);
    _objectsByType[object->type()].push_back(object);
    _objectsByTag[object->tag()].push_back(object);

    determineObjectRoom(*object);
    attachObjectToSceneGraph(object);

    if (auto door = dyn_cast<Door>(object)) {
        if ((door->linkedToFlags() == 1 || door->linkedToFlags() == 2) &&
            !door->linkedToModule().empty() &&
            !door->linkedTo().empty() &&
            !door->linkedTransitionGeometry().empty()) {
            std::vector<std::shared_ptr<Object>> noObsolete;
            std::shared_ptr<Trigger> trigger;
            _game.replaceRuntimeObjectGraph(
                noObsolete,
                [&]() {
                    trigger = _game.newTrigger(_sceneName);
                    trigger->configureLinkedDoorTransition(door);
                },
                []() noexcept {});
            add(trigger);
        }
    }
}

void Area::attachRoomToSceneGraph(Room &room) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    if (room.model()) {
        sceneGraph.addRoot(room.model());
    }
    if (room.walkmesh()) {
        sceneGraph.addRoot(room.walkmesh());
    }
    if (room.grass()) {
        sceneGraph.addRoot(room.grass());
    }
}

void Area::attachObjectToSceneGraph(const std::shared_ptr<Object> &object) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto sceneNode = object->sceneNode();
    if (sceneNode) {
        if (sceneNode->type() == SceneNodeType::Model) {
            sceneGraph.addRoot(std::static_pointer_cast<ModelSceneNode>(sceneNode));
        } else if (sceneNode->type() == SceneNodeType::Sound) {
            sceneGraph.addRoot(std::static_pointer_cast<SoundSceneNode>(sceneNode));
        } else if (sceneNode->type() == SceneNodeType::Trigger) {
            sceneGraph.addRoot(std::static_pointer_cast<TriggerSceneNode>(sceneNode));
        }
    }
    if (object->type() == ObjectType::Placeable) {
        auto placeable = std::static_pointer_cast<Placeable>(object);
        auto walkmesh = placeable->walkmesh();
        if (walkmesh) {
            sceneGraph.addRoot(walkmesh);
        }
    } else if (object->type() == ObjectType::Door) {
        auto door = std::static_pointer_cast<Door>(object);
        auto walkmeshClosed = door->walkmeshClosed();
        if (walkmeshClosed) {
            sceneGraph.addRoot(walkmeshClosed);
        }
        auto walkmeshOpen1 = door->walkmeshOpen1();
        if (walkmeshOpen1) {
            sceneGraph.addRoot(walkmeshOpen1);
        }
        auto walkmeshOpen2 = door->walkmeshOpen2();
        if (walkmeshOpen2) {
            sceneGraph.addRoot(walkmeshOpen2);
        }
    }
}

void Area::determineObjectRoom(Object &object) {
    Room *room = nullptr;

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    Collision collision;
    if (sceneGraph.testElevation(object.position(), collision)) {
        room = dynamic_cast<Room *>(collision.user);
    }

    object.setRoom(room);
}

void Area::doDestroyObjects() {
    for (auto &object : _objectsToDestroy) {
        doDestroyObject(object);
    }
    _objectsToDestroy.clear();
}

void Area::doDestroyObject(uint32_t objectId, bool destroyRuntimeObject) {
    auto object = _game.getObjectById(objectId);
    if (!object) {
        return;
    }

    if (auto door = dyn_cast<Door>(object)) {
        std::vector<uint32_t> linkedTriggerIds;
        for (auto &triggerObject : _objectsByType[ObjectType::Trigger]) {
            auto trigger = std::static_pointer_cast<Trigger>(triggerObject);
            if (trigger->detachLinkedDoorTransition(*door)) {
                linkedTriggerIds.push_back(trigger->id());
            }
        }
        for (auto triggerId : linkedTriggerIds) {
            doDestroyObject(triggerId);
        }
    }

    auto room = object->room();
    if (room) {
        room->removeTenant(object.get());
    }

    // Drop the object from any trigger it was standing inside. A destroyed
    // object never moves, so Trigger::update would otherwise keep it as a tenant
    // indefinitely (leaking it and leaving the trigger stuck in the Inside
    // state). Destruction is not an "exit", so no OnExit is fired.
    for (auto &triggerObject : _objectsByType[ObjectType::Trigger]) {
        static_cast<Trigger &>(*triggerObject).removeTenant(object.get());
    }

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto sceneNode = object->sceneNode();
    if (sceneNode) {
        if (sceneNode->type() == SceneNodeType::Model) {
            sceneGraph.removeRoot(*std::static_pointer_cast<ModelSceneNode>(sceneNode));
        } else if (sceneNode->type() == SceneNodeType::Sound) {
            sceneGraph.removeRoot(*std::static_pointer_cast<SoundSceneNode>(sceneNode));
        } else if (sceneNode->type() == SceneNodeType::Trigger) {
            sceneGraph.removeRoot(*std::static_pointer_cast<TriggerSceneNode>(sceneNode));
        }
    }
    if (object->type() == ObjectType::Placeable) {
        auto placeable = std::static_pointer_cast<Placeable>(object);
        auto walkmesh = placeable->walkmesh();
        if (walkmesh) {
            sceneGraph.removeRoot(*walkmesh);
        }
    } else if (object->type() == ObjectType::Door) {
        auto door = std::static_pointer_cast<Door>(object);
        auto walkmeshOpen1 = door->walkmeshOpen1();
        if (walkmeshOpen1) {
            sceneGraph.removeRoot(*walkmeshOpen1);
        }
        auto walkmeshOpen2 = door->walkmeshOpen2();
        if (walkmeshOpen2) {
            sceneGraph.removeRoot(*walkmeshOpen2);
        }
        auto walkmeshClosed = door->walkmeshClosed();
        if (walkmeshClosed) {
            sceneGraph.removeRoot(*walkmeshClosed);
        }
    }

    auto maybeObject = std::find_if(_objects.begin(), _objects.end(), [&object](auto &o) { return o.get() == object.get(); });
    if (maybeObject != _objects.end()) {
        _objects.erase(maybeObject);
    }
    auto maybeTagObjects = _objectsByTag.find(object->tag());
    if (maybeTagObjects != _objectsByTag.end()) {
        auto &tagObjects = maybeTagObjects->second;
        auto maybeObjectByTag = std::find_if(tagObjects.begin(), tagObjects.end(), [&object](auto &o) { return o.get() == object.get(); });
        if (maybeObjectByTag != tagObjects.end()) {
            tagObjects.erase(maybeObjectByTag);
        }
        if (tagObjects.empty()) {
            _objectsByTag.erase(maybeTagObjects);
        }
    }
    auto &typeObjects = _objectsByType.find(object->type())->second;
    auto maybeObjectByType = std::find_if(typeObjects.begin(), typeObjects.end(), [&object](auto &o) { return o.get() == object.get(); });
    if (maybeObjectByType != typeObjects.end()) {
        typeObjects.erase(maybeObjectByType);
    }
    if (_hilightedObject.get() == object.get()) {
        _hilightedObject.reset();
    }
    if (_selectedObject.get() == object.get()) {
        _selectedObject.reset();
    }
    if (destroyRuntimeObject) {
        _game.destroyRuntimeObjectGraph(object);
    }
}

ObjectList &Area::getObjectsByType(ObjectType type) {
    return _objectsByType.find(type)->second;
}

std::shared_ptr<Object> Area::getObjectByTag(const std::string &tag, int nth) const {
    auto objects = _objectsByTag.find(tag);
    if (objects == _objectsByTag.end())
        return nullptr;

    if (nth >= objects->second.size())
        return nullptr;

    // GetObjectByTag routine requires the array to be partitioned by isDead:
    // all alive objects should be at the front, and all dead objects should be
    // at the back of the array.
    //
    // We do not actually sort the array, but traverse it in two passes with an
    // inverted condition.

    // Search "not dead" objects first.
    size_t i = 0;
    for (const std::shared_ptr<Object> &object : objects->second) {
        if (!object->isDead()) {
            if ((i++) == nth) {
                return object;
            }
        }
    }

    // Search across dead objects.
    for (const std::shared_ptr<Object> &object : objects->second) {
        if (object->isDead()) {
            if ((i++) == nth) {
                return object;
            }
        }
    }

    assert(0 && "inconsistent getObjectByTag");
    return nullptr;
}

bool Area::landObject(Object &object) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    glm::vec3 position(object.position());
    Collision collision;

    // Test elevation at object position
    if (sceneGraph.testElevation(position, collision)) {
        object.setPosition(collision.intersection);
        return true;
    }

    // Test elevations in a circle around object position
    for (int i = 0; i < 4; ++i) {
        float angle = i * glm::half_pi<float>();
        position = object.position() + glm::vec3(glm::sin(angle), glm::cos(angle), 0.0f);

        if (sceneGraph.testElevation(position, collision)) {
            object.setPosition(collision.intersection);
            return true;
        }
    }

    return false;
}

glm::vec3 Area::findPartyPosition(const Creature &member, const glm::vec3 &position) const {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    const auto &creatures = _objectsByType.at(ObjectType::Creature);

    auto tryPosition = [&](const glm::vec3 &candidate, glm::vec3 &result) {
        Collision collision;
        if (!sceneGraph.testElevation(candidate, collision)) {
            return false;
        }
        for (const auto &object : creatures) {
            if (object.get() == &member) {
                continue;
            }
            if (glm::distance2(glm::vec2(collision.intersection), glm::vec2(object->position())) < kPartyMemberSpacing2) {
                return false;
            }
        }
        result = collision.intersection;
        return true;
    };

    glm::vec3 result;
    if (tryPosition(position, result)) {
        return result;
    }

    for (float radius = kPartyPositionSearchStep; radius <= kPartyPositionSearchRadius; radius += kPartyPositionSearchStep) {
        for (int i = 0; i < kPartyPositionSearchDirections; ++i) {
            float angle = i * glm::two_pi<float>() / kPartyPositionSearchDirections;
            glm::vec3 candidate(position.x + radius * glm::sin(angle), position.y + radius * glm::cos(angle), position.z);
            if (tryPosition(candidate, result)) {
                return result;
            }
        }
    }

    return position;
}

void Area::loadPartyMember(const std::shared_ptr<Creature> &member, int index, bool preserveSavedPlacement) {
    bool loaded = std::find(_objects.begin(), _objects.end(), member) != _objects.end();

    // A live party member can cross this boundary while a dialogue still owns
    // its model as a stunt participant. Destination placement is a new
    // presentation lifetime: release the old assignment before applying the
    // entry transform so an authored destination stunt can acquire the same PC.
    if (!preserveSavedPlacement) {
        member->stopStuntMode();
    }

    if (!preserveSavedPlacement && index > 0) {
        auto leader = _game.party().getLeader();
        glm::vec3 position(leader->position());

        if (index <= static_cast<int>(kPartyFormationOffsets.size())) {
            glm::quat rotation(glm::angleAxis(leader->getFacing(), glm::vec3(0.0f, 0.0f, 1.0f)));
            position += rotation * kPartyFormationOffsets[index - 1];
        }

        member->setPosition(findPartyPosition(*member, position));
        member->setFacing(leader->getFacing());
    }

    bool landed = landObject(*member);
    if (!preserveSavedPlacement && index == 0 && !landed) {
        glm::vec3 position(member->position());
        glm::vec3 fallbackPosition(position);
        fallbackPosition.z = scene::kElevationTestZ;

        member->setPosition(fallbackPosition);
        if (!landObject(*member)) {
            member->setPosition(position);
        }
    }

    if (loaded) {
        determineObjectRoom(*member);
        return;
    }

    add(member);
    if (!preserveSavedPlacement) {
        member->runSpawnScript();
    }
}

void Area::retireCreatureAreaRuntime(const std::shared_ptr<Creature> &creature) {
    if (!creature ||
        std::find(_objects.begin(), _objects.end(), creature) == _objects.end()) {
        return;
    }

    auto runtimeObjects = _game.party().runtimeObjects();
    std::set<const Object *> retainedObjects;
    for (const auto &object : runtimeObjects) {
        if (object) retainedObjects.insert(object.get());
    }

    // Creature-side handles must retire while Pathfinder and every referenced
    // outgoing object are alive. Area-owned structural attachments follow,
    // then the raw Room pointer is invalidated before Room destruction.
    creature->retireAreaRuntime(_pathfinder, retainedObjects);
    doDestroyObject(creature->id(), false);
    creature->setRoom(nullptr);
}

void Area::retirePartyMemberAreaRuntime(const std::shared_ptr<Creature> &member) {
    retireCreatureAreaRuntime(member);
}

void Area::loadParty(const glm::vec3 &position, float facing, bool preserveSavedPlacement) {
    Party &party = _game.party();
    auto leader = party.getLeader();

    if (!preserveSavedPlacement) {
        leader->setPosition(position);
        leader->setFacing(facing);
    }
    loadPartyMember(leader, 0, preserveSavedPlacement);

    for (int i = 1; i < party.getSize(); ++i) {
        loadPartyMember(party.getMember(i), i, preserveSavedPlacement);
    }
    int formationIndex = party.getSize();
    for (int puppet : party.persistedState().puppetIds) {
        if (auto creature = party.getAvailablePuppet(puppet, true)) {
            loadPartyMember(creature, formationIndex++, preserveSavedPlacement);
        }
    }
}

void Area::retirePartyAreaRuntime() {
    auto runtimeObjects = _game.party().runtimeObjects();
    for (const auto &object : runtimeObjects) {
        if (object && object->type() == ObjectType::Creature) {
            retireCreatureAreaRuntime(std::static_pointer_cast<Creature>(object));
        }
    }
}

void Area::repositionPartyMember(
    const std::shared_ptr<Creature> &member,
    int index) {

    bool loaded = std::find(_objects.begin(), _objects.end(), member) != _objects.end();
    if (index > 0) {
        auto leader = _game.party().getLeader();
        glm::vec3 position(leader->position());

        if (index <= static_cast<int>(kPartyFormationOffsets.size())) {
            glm::quat rotation(glm::angleAxis(
                leader->getFacing(), glm::vec3(0.0f, 0.0f, 1.0f)));
            position += rotation * kPartyFormationOffsets[index - 1];
        }

        member->setPosition(findPartyPosition(*member, position));
        member->setFacing(leader->getFacing());
    }

    bool landed = landObject(*member);
    if (index == 0 && !landed) {
        glm::vec3 position(member->position());
        glm::vec3 fallbackPosition(position);
        fallbackPosition.z = scene::kElevationTestZ;

        member->setPosition(fallbackPosition);
        if (!landObject(*member)) {
            member->setPosition(position);
        }
    }

    if (loaded) {
        determineObjectRoom(*member);
        return;
    }

    add(member);
    member->runSpawnScript();
}

void Area::placeControlledCreature(
    const std::shared_ptr<Creature> &creature,
    const glm::vec3 &position,
    float facing) {

    // Retail SwitchPlayerCharacter transfers control in the existing Area.
    // Only the incoming actor inherits the outgoing leader's transform;
    // unrelated followers and the parked actor retain their current runtime
    // state and placement.
    creature->setPosition(position);
    creature->setFacing(facing);
    repositionPartyMember(creature, 0);
}

void Area::repositionParty(const glm::vec3 &position, float facing) {
    // This is a same-Area operation. It deliberately does not call
    // retireCreatureAreaRuntime: action/delay/effect and Area-lifetime state
    // remain authoritative while control or party composition changes.
    Party &party = _game.party();
    auto leader = party.getLeader();
    if (!leader) return;

    leader->setPosition(position);
    leader->setFacing(facing);
    repositionPartyMember(leader, 0);
    for (int i = 1; i < party.getSize(); ++i) {
        repositionPartyMember(party.getMember(i), i);
    }
    int formationIndex = party.getSize();
    for (int puppet : party.persistedState().puppetIds) {
        if (auto creature = party.getAvailablePuppet(puppet, true)) {
            repositionPartyMember(creature, formationIndex++);
        }
    }
}

void Area::repositionParty() {
    auto leader = _game.party().getLeader();
    if (!leader) return;
    repositionParty(leader->position(), leader->getFacing());
}

bool Area::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    default:
        return false;
    }
}

bool Area::handleKeyDown(const input::KeyEvent &event) {
    return false;
}

void Area::update(float dt) {
    doDestroyObjects();
    updateVisibility();
    updateObjectSelection();

    if (_game.isPaused()) {
        return;
    }
    Object::update(dt);

    // Update can create new objects, so iterate with indices.
    for (size_t i = 0; i < _objects.size(); ++i) {
        _objects[i]->update(dt);
    }
    updateLeaderTriggerOccupancy();
    updatePerception(dt);
    updateMessageBus();
    updateHeartbeat(dt);
}

bool Area::moveCreature(const std::shared_ptr<Creature> &creature, const glm::vec2 &dir, bool run, float dt,
                        float maxDistance) {
    static glm::vec3 up {0.0f, 0.0f, 1.0f};
    static glm::vec3 zOffset {0.0f, 0.0f, 0.1f};

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    Collision collision;

    // Set creature facing

    float facing = -glm::atan(dir.x, dir.y);
    creature->setFacing(facing);

    // Test obstacle between origin and destination

    glm::vec3 origin(creature->position());
    origin.z += 0.1f;

    float speed = run ? creature->runSpeed() : creature->walkSpeed();
    float speedDt = speed * dt;

    if (speedDt > maxDistance) {
        speedDt = maxDistance;
    }

    glm::vec3 dest(origin);
    dest.x += dir.x * speedDt;
    dest.y += dir.y * speedDt;

    bool obstructed = sceneGraph.testWalk(origin, dest, creature.get(), collision);

    // Remember a door that obstructs the intended direction of travel, so that
    // navigation can raise the blocked event and GetBlockingDoor can report it.
    // This is taken from the test against the intended direction: the slide
    // below may still salvage some sideways motion, but the door did block
    // where the creature wanted to go.
    auto *blockingDoor = obstructed ? dynamic_cast<Door *>(collision.user) : nullptr;
    if (blockingDoor) {
        creature->setBlockingDoor(blockingDoor->id());
    } else {
        creature->clearBlockingDoor();
    }

    if (obstructed) {
        // Try moving along the surface
        glm::vec2 right(glm::normalize(glm::vec2(glm::cross(up, collision.normal))));
        glm::vec2 newDir(glm::normalize(right * glm::dot(dir, right)));

        dest = origin;
        dest.x += newDir.x * speedDt;
        dest.y += newDir.y * speedDt;

        if (sceneGraph.testWalk(origin, dest, creature.get(), collision)) {
            return false;
        }
    }

    CreatureCollision creatureCollision;
    if (findCreatureCollision(*creature, origin, dest, creatureCollision)) {
        glm::vec2 movement(glm::vec2(dest) - glm::vec2(origin));
        glm::vec2 contact(glm::vec2(origin) + movement * creatureCollision.time);
        glm::vec2 remaining(movement * (1.0f - creatureCollision.time));

        float inward = glm::dot(remaining, creatureCollision.normal);
        if (inward < 0.0f) {
            remaining -= creatureCollision.normal * inward;
        }

        glm::vec3 slideOrigin(contact.x, contact.y, origin.z);
        dest = slideOrigin;

        if (glm::dot(remaining, remaining) > 0.0f) {
            glm::vec3 slideDest(slideOrigin.x + remaining.x, slideOrigin.y + remaining.y, slideOrigin.z);
            CreatureCollision slideCollision;
            if (!sceneGraph.testWalk(slideOrigin, slideDest, creature.get(), collision) &&
                !findCreatureCollision(*creature, slideOrigin, slideDest, slideCollision, creatureCollision.creature)) {
                dest = slideDest;
            }
        }

        if (glm::distance2(glm::vec2(origin), glm::vec2(dest)) == 0.0f) {
            return false;
        }
    }

    // Test elevation at destination

    if (!sceneGraph.testElevation(dest, collision)) {
        return false;
    }

    auto userRoom = dynamic_cast<Room *>(collision.user);
    auto prevRoom = creature->room();

    creature->setRoom(userRoom);
    creature->setPosition(glm::vec3(dest.x, dest.y, collision.intersection.z));
    creature->setWalkmeshMaterial(collision.material);

    if (creature == _game.party().getLeader()) {
        onPartyLeaderMoved(userRoom != prevRoom);
    }

    checkTriggersIntersection(creature);

    return true;
}

bool Area::findCreatureCollision(
    const Creature &creature,
    const glm::vec3 &origin,
    const glm::vec3 &destination,
    CreatureCollision &outCollision,
    const Creature *ignoredCreature) const {
    bool found = false;
    outCollision.time = 1.0f;

    for (const auto &object : _objectsByType.at(ObjectType::Creature)) {
        const auto &other = static_cast<const Creature &>(*object);
        if (&other == &creature || &other == ignoredCreature || other.isDead()) {
            continue;
        }

        float radius = creature.creaturePersonalSpace() + other.creaturePersonalSpace() + kCreatureCollisionEpsilon;
        float time;
        glm::vec2 normal;
        if (sweepCircle(glm::vec2(origin), glm::vec2(destination), glm::vec2(other.position()), radius, time, normal) &&
            (!found || time < outCollision.time)) {
            found = true;
            outCollision.creature = &other;
            outCollision.time = time;
            outCollision.normal = normal;
        }
    }

    return found;
}

bool Area::isObjectSeen(const Creature &subject, const Object &object) const {
    if (!object.visible()) {
        return false;
    }

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    glm::vec3 origin(subject.position());
    origin.z += kLineOfSightHeight;

    glm::vec3 dest(object.position());
    dest.z += kLineOfSightHeight;

    Collision collision;
    if (sceneGraph.testLineOfSight(origin, dest, collision)) {
        return collision.user == &object ||
               subject.getSquareDistanceTo(object) < glm::distance2(origin, collision.intersection);
    }

    return true;
}

void Area::runSpawnScripts() {
    for (auto &creature : _objectsByType[ObjectType::Creature]) {
        static_cast<Creature &>(*creature).runSpawnScript();
    }
}

void Area::runOnEnterScript() {
    if (_onEnter.empty())
        return;

    auto player = _game.party().player();
    if (!player)
        return;

    _game.scriptRunner().run(
        _onEnter,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::EnteringObject, script::Variable::ofObject(player->id())}});
}

void Area::runOnExitScript() {
    if (_onExit.empty())
        return;

    auto player = _game.party().player();
    if (!player)
        return;

    _game.scriptRunner().run(
        _onExit,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::ExitingObject, script::Variable::ofObject(player->id())}});
}

void Area::destroyObject(const Object &object) {
    _objectsToDestroy.insert(object.id());
}

glm::vec3 Area::getSelectableScreenCoords(const std::shared_ptr<Object> &object, const glm::mat4 &projection, const glm::mat4 &view) const {
    static glm::vec4 viewport(0.0f, 0.0f, 1.0f, 1.0f);

    glm::vec3 position(object->getSelectablePosition());

    return glm::project(position, view, projection, viewport);
}

void Area::update3rdPersonCameraFacing() {
    auto partyLeader = _game.party().getLeader();
    if (!partyLeader || !_thirdPersonCamera) {
        return;
    }
    _thirdPersonCamera->setFacing(partyLeader->getFacing());
}

void Area::startDialog(const std::shared_ptr<Object> &object, const std::string &resRef) {
    std::string finalResRef(resRef);
    if (resRef.empty()) {
        finalResRef = object->conversation();
    }
    if (finalResRef.empty()) {
        return;
    }
    _game.startDialog(object, finalResRef);
}

void Area::onPartyLeaderMoved(bool roomChanged) {
    auto partyLeader = _game.party().getLeader();
    if (!partyLeader) {
        return;
    }
    if (roomChanged) {
        updateRoomVisibility();
    }
    update3rdPersonCameraTarget();
}

void Area::updateRoomVisibility() {
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    Room *leaderRoom = partyLeader ? partyLeader->room() : nullptr;
    bool allVisible = _game.cameraType() != CameraType::ThirdPerson || !leaderRoom;

    if (allVisible) {
        for (auto &room : _rooms) {
            room.second->setVisible(true);
        }
    } else {
        auto adjRoomNames = _visibility.equal_range(leaderRoom->name());
        for (auto &room : _rooms) {
            // Room is visible if either of the following is true:
            // 1. party leader is not in a room
            // 2. this room is the party leaders room
            // 3. this room is adjacent to the party leaders room
            bool visible = !leaderRoom || room.second.get() == leaderRoom;
            if (!visible) {
                for (auto adjRoom = adjRoomNames.first; adjRoom != adjRoomNames.second; adjRoom++) {
                    if (adjRoom->second == room.first) {
                        visible = true;
                        break;
                    }
                }
            }
            room.second->setVisible(visible);
        }
    }
}

void Area::update3rdPersonCameraTarget() {
    std::shared_ptr<Object> partyLeader(_game.party().getLeader());
    if (!partyLeader) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(partyLeader->sceneNode());
    if (!model) {
        return;
    }
    auto cameraHook = model->getNodeByName("camerahook");
    if (cameraHook) {
        _thirdPersonCamera->setTargetPosition(cameraHook->origin());
    } else {
        _thirdPersonCamera->setTargetPosition(model->getWorldCenterOfAABB());
    }
}

void Area::updateVisibility() {
    if (_game.cameraType() != CameraType::ThirdPerson) {
        updateRoomVisibility();
    }
}

void Area::checkTriggersIntersection(const std::shared_ptr<Object> &triggerrer, bool fireTransitions) {
    glm::vec2 position2d(triggerrer->position());

    for (auto &object : _objectsByType[ObjectType::Trigger]) {
        auto trigger = std::static_pointer_cast<Trigger>(object);
        if (!trigger->isActive()) {
            trigger->removeTenant(triggerrer.get());
            continue;
        }
        bool inside = trigger->isIn(position2d);
        trigger->markDebugTested(inside);
        if (trigger->isTenant(triggerrer) || !inside) {
            continue;
        }
        bool transition = !trigger->linkedToModule().empty();
        if (transition && !fireTransitions) {
            // Leave module-transition triggers to movement-based firing so a
            // creature placed inside one is not immediately warped out.
            continue;
        }
        debug(str(boost::format("trigger: onenter tag=%s script=%s entering=%s") %
                  trigger->tag() %
                  (trigger->getOnEnter().empty() ? std::string("<none>") : trigger->getOnEnter()) %
                  triggerrer->tag()));

        if (transition && !trigger->acceptsTransitionActivator(triggerrer)) {
            continue;
        }
        trigger->addTenant(triggerrer);
        trigger->markDebugEntered();

        if (transition) {
            _game.scheduleModuleTransition(trigger->linkedToModule(), trigger->linkedTo());
            return;
        }
    }
}

void Area::updateLeaderTriggerOccupancy() {
    auto leader = _game.party().getLeader();
    if (!leader) {
        return;
    }
    // Fire occupancy-based OnEnter for script triggers (transitions excluded),
    // so authored module-entry/cutscene triggers run even when the leader is
    // placed inside them rather than walking across the boundary.
    checkTriggersIntersection(leader, /*fireTransitions=*/false);
}

void Area::updateHeartbeat(float dt) {
    _heartbeatTimer.update(dt);
    if (_heartbeatTimer.elapsed()) {
        if (!_onHeartbeat.empty()) {
            _game.scriptRunner().run(_onHeartbeat, _id);
        }
        for (auto &object : _objects) {
            std::string heartbeat(object->getOnHeartbeat());
            if (!heartbeat.empty()) {
                _game.scriptRunner().run(heartbeat, object->id());
            }
        }
        _heartbeatTimer.reset(kHeartbeatInterval);
    }
}

Camera *Area::getCamera(CameraType type) {
    switch (type) {
    case CameraType::FirstPerson:
        return _firstPersonCamera.get();
    case CameraType::ThirdPerson:
        return _thirdPersonCamera.get();
    case CameraType::Static:
        return _staticCamera;
    case CameraType::Animated:
        return _animatedCamera.get();
    case CameraType::Dialog:
        return _dialogCamera.get();
    default:
        throw std::invalid_argument("Invalid camera type: " + std::to_string(static_cast<int>(type)));
    }
}

void Area::setStaticCamera(int cameraId) {
    for (auto &object : _objectsByType[ObjectType::Camera]) {
        auto camera = static_cast<Camera *>(object.get());
        if (camera->cameraId() == cameraId) {
            _staticCamera = static_cast<StaticCamera *>(camera);
            break;
        }
    }
}

void Area::setThirdPartyCameraStyle(CameraStyleType type) {
    switch (type) {
    case CameraStyleType::Combat:
        _thirdPersonCamera->setStyle(_camStyleCombat);
        break;
    default:
        _thirdPersonCamera->setStyle(_camStyleDefault);
        break;
    }
}

void Area::setStealthXPEnabled(bool value) {
    _stealthXPEnabled = value;
}

void Area::setMaxStealthXP(int value) {
    _maxStealthXP = value;
}

void Area::setCurrentStealthXP(int value) {
    _currentStealthXP = value;
}

void Area::setStealthXPDecrement(int value) {
    _stealthXPDecrement = value;
}

void Area::setUnescapable(bool value) {
    _unescapable = value;
}

std::shared_ptr<Object> Area::createObject(ObjectType type, const std::string &blueprintResRef, const std::shared_ptr<Location> &location) {
    std::shared_ptr<Object> object;
    switch (type) {
    case ObjectType::Item: {
        std::shared_ptr<Item> item =
            _game.newItemFromBlueprint(blueprintResRef);
        object = std::move(item);
        break;
    }
    case ObjectType::Creature: {
        std::shared_ptr<Creature> creature =
            _game.newCreatureFromBlueprint(blueprintResRef);
        creature->setPosition(location->position());
        creature->setFacing(location->facing());
        object = std::move(creature);
        break;
    }
    case ObjectType::Placeable: {
        std::shared_ptr<Placeable> placeable =
            _game.newPlaceableFromBlueprint(blueprintResRef);
        object = std::move(placeable);
        break;
    }
    default:
        warn("Unsupported object type: " + std::to_string(static_cast<int>(type)));
        break;
    }
    if (!object) {
        return nullptr;
    }

    if (location) {
        object->setPosition(location->position());
        object->setFacing(location->facing());
    }

    add(object);

    auto creature = std::dynamic_pointer_cast<Creature>(object);
    if (creature) {
        creature->runSpawnScript();
    }

    return object;
}

void Area::updateObjectSelection() {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto camera = _game.getActiveCamera();
    if (!camera) {
        return;
    }
    auto cameraPos = camera->sceneNode()->origin();

    if (_hilightedObject) {
        if (!_hilightedObject->isSelectable()) {
            _hilightedObject.reset();
        } else {
            Collision collision;
            auto objectPos = _hilightedObject->getSelectablePosition();
            if (glm::distance2(cameraPos, objectPos) > kSelectionDistance2 || (sceneGraph.testLineOfSight(cameraPos, objectPos, collision) && collision.user != _hilightedObject.get())) {
                _hilightedObject.reset();
            }
        }
    }
    if (_selectedObject && !_forceSelection) {
        if (!_selectedObject->isSelectable()) {
            _selectedObject.reset();
        } else {
            Collision collision;
            auto objectPos = _selectedObject->getSelectablePosition();
            if (glm::distance2(cameraPos, objectPos) > kSelectionDistance2 || (sceneGraph.testLineOfSight(cameraPos, objectPos, collision) && collision.user != _selectedObject.get())) {
                _selectedObject.reset();
            }
        }
    }
}

void Area::hilightObject(std::shared_ptr<Object> object) {
    _hilightedObject = std::move(object);
}

void Area::selectObject(std::shared_ptr<Object> object, bool force) {
    _selectedObject = std::move(object);
    _forceSelection = force;
}

std::shared_ptr<Object> Area::getNearestObject(const glm::vec3 &origin, int nth, const std::function<bool(const std::shared_ptr<Object> &)> &predicate) {
    std::vector<std::pair<std::shared_ptr<Object>, float>> candidates;

    for (auto &object : _objects) {
        if (predicate(object)) {
            candidates.push_back(std::make_pair(object, object->getSquareDistanceTo(origin)));
        }
    }
    sort(candidates.begin(), candidates.end(), [](auto &left, auto &right) { return left.second < right.second; });

    int candidateCount = static_cast<int>(candidates.size());
    if (nth >= candidateCount) {
        debug(str(boost::format("getNearestObject: nth is out of bounds: %d/%d") % nth % candidateCount));
        return nullptr;
    }

    return candidates[nth].first;
}

std::shared_ptr<Creature> Area::getNearestCreature(const std::shared_ptr<Object> &target, const SearchCriteriaList &criterias, int nth) {
    std::vector<std::pair<std::shared_ptr<Creature>, float>> candidates;

    for (auto &object : getObjectsByType(ObjectType::Creature)) {
        auto creature = std::static_pointer_cast<Creature>(object);
        if (matchesCriterias(*creature, criterias, target)) {
            float distance2 = creature->getSquareDistanceTo(*target);
            candidates.push_back(std::make_pair(std::move(creature), distance2));
        }
    }

    sort(candidates.begin(), candidates.end(), [](auto &left, auto &right) {
        return left.second < right.second;
    });

    return nth < candidates.size() ? candidates[nth].first : nullptr;
}

// The criteria describe the candidate's standing with the creature the search
// is centred on, so that creature is the source of every disposition query.
static bool matchesReputation(const Creature &candidate, const Object *target,
                              ReputationType reputation, IReputes &reputes) {
    if (!target || target->type() != ObjectType::Creature) {
        return false;
    }
    const Creature &searching = static_cast<const Creature &>(*target);

    switch (reputation) {
    case ReputationType::Friend:
        return reputes.getIsFriend(searching, candidate);
    case ReputationType::Enemy: {
        // Do not consider dead enemies as enemies. Scripts use
        // GetNearestCreature to find a new target, and targeting dead bodies is
        // a poor tactic.
        return !candidate.isDead() && reputes.getIsEnemy(searching, candidate);
    }
    case ReputationType::Neutral:
        return reputes.getIsNeutral(searching, candidate);
    }
    return false;
}

static bool matchesPerception(const Creature &creature, const Object *target,
                              PerceptionType perception) {
    if (!target || target->type() != ObjectType::Creature) {
        return false;
    }
    const Creature &targetCreature = static_cast<const Creature &>(*target);

    bool seen = targetCreature.perception().sees(creature.id());
    bool heard = targetCreature.perception().hears(creature.id());

    switch (perception) {
    case PerceptionType::SeenAndHeard:
        return seen && heard;
    case PerceptionType::NotSeenAndNotHeard:
        return !seen && !heard;
    case PerceptionType::HeardAndNotSeen:
        return heard && !seen;
    case PerceptionType::SeenAndNotHeard:
        return seen && !heard;
    case PerceptionType::NotHeard:
        return !heard;
    case PerceptionType::Heard:
        return heard;
    case PerceptionType::NotSeen:
        return !seen;
    case PerceptionType::Seen:
        return seen;
    }
    return false;
}

bool Area::matchesCriterias(const Creature &creature, const SearchCriteriaList &criterias, std::shared_ptr<Object> target) const {
    if (!target || target->type() != ObjectType::Creature) {
        // Reputation and Perception checks need a target
        return false;
    }

    Creature &targetCreature = *std::static_pointer_cast<Creature>(target);

    for (auto &criteria : criterias) {
        switch (criteria.first) {
        case CreatureType::Reputation: {
            auto reputation = static_cast<ReputationType>(criteria.second);
            if (!matchesReputation(creature, target.get(), reputation, _services.game.reputes)) {
                return false;
            }
            break;
        }
        case CreatureType::Perception: {
            bool matches = false;
            auto perception = static_cast<PerceptionType>(criteria.second);
            if (!matchesPerception(creature, target.get(), perception)) {
                return false;
            }
            break;
        }
        default:
            // TODO: implement other criterias
            break;
        }
    }

    return true;
}

std::shared_ptr<Creature> Area::getNearestCreatureToLocation(const Location &location, const SearchCriteriaList &criterias, int nth) {
    std::vector<std::pair<std::shared_ptr<Creature>, float>> candidates;

    for (auto &object : getObjectsByType(ObjectType::Creature)) {
        auto creature = std::static_pointer_cast<Creature>(object);
        if (matchesCriterias(*creature, criterias)) {
            float distance2 = creature->getSquareDistanceTo(location.position());
            candidates.push_back(std::make_pair(std::move(creature), distance2));
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](auto &left, auto &right) {
        return left.second < right.second;
    });

    return nth < candidates.size() ? candidates[nth].first : nullptr;
}

void Area::updatePerception(float dt) {
    _perceptionTimer.update(dt);
    if (_perceptionTimer.elapsed()) {
        doUpdatePerception();
        _perceptionTimer.reset(kUpdatePerceptionInterval);
    }
}

void Area::doUpdatePerception() {
    // For each creature, determine a list of creatures it sees
    ObjectList &creatures = getObjectsByType(ObjectType::Creature);
    for (auto &object : creatures) {
        // Skip dead creatures
        if (object->isDead())
            continue;

        auto creature = std::static_pointer_cast<Creature>(object);
        float hearingRange2 = creature->perception().hearingRange * creature->perception().hearingRange;
        float sightRange2 = creature->perception().sightRange * creature->perception().sightRange;

        for (auto &other : creatures) {
            // Skip self
            if (other == object)
                continue;

            bool heard = false;
            bool seen = false;

            float distance2 = creature->getSquareDistanceTo(*other);
            if (distance2 <= hearingRange2) {
                heard = true;
            }
            if (distance2 <= sightRange2) {
                seen = isObjectSeen(*creature, *other);
            }

            // Hearing
            bool wasHeard = creature->perception().hears(other->id());
            bool wasSeen = creature->perception().sees(other->id());

            if (wasHeard == heard && wasSeen == seen) {
                continue; // no change in perception
            }

            if (wasHeard != heard) {
                debug(str(boost::format("%s %s %s") % other->tag() % (heard ? "heard by" : "inaudible by") % creature->tag()), LogChannel::Perception);
                creature->setObjectHeard(other, heard);
            }

            if (wasSeen != seen) {
                debug(str(boost::format("%s %s %s") % other->tag() % (seen ? "seen by" : "vanished from") % creature->tag()), LogChannel::Perception);
                creature->setObjectSeen(other, seen);
            }

            creature->runOnNotice(*other, heard, seen);
        }
    }
}

void Area::updateMessageBus() {
    _messageBus.update([this](uint32_t speakerId, uint32_t listenerId,
                              int32_t number, TalkVolume volume) {
        auto listener = _game.getObjectById(listenerId);
        if (listener->type() != ObjectType::Creature) {
            return;
        }
        Creature &creature = static_cast<Creature &>(*listener);

        bool heard = creature.perception().hears(speakerId);
        if (!creature.isListening() || !heard) {
            return;
        }
        creature.runDialogueScript(speakerId, number);
    });
}

Object *Area::getObjectAt(int x, int y) const {
    auto partyLeader = _game.party().getLeader();
    if (!partyLeader) {
        return nullptr;
    }
    auto &scene = _services.scene.graphs.get(kSceneMain);
    auto model = scene.pickModelAt(x, y, partyLeader.get());
    if (!model) {
        return nullptr;
    }
    return dynamic_cast<Object *>(model->user());
}

std::vector<TransitionPortal> Area::transitionPresentationPortals() const {
    std::vector<TransitionPortal> portals;
    auto maybeTriggers = _objectsByType.find(ObjectType::Trigger);
    if (maybeTriggers == _objectsByType.end()) {
        return portals;
    }
    for (auto &object : maybeTriggers->second) {
        auto trigger = std::static_pointer_cast<Trigger>(object);
        if (trigger->linkedToModule().empty() || !trigger->isActive()) {
            continue;
        }
        const auto &geometry = trigger->geometry();
        if (geometry.size() < 3) {
            continue;
        }
        TransitionPortal portal;
        portal.objectId = trigger->id();
        portal.destination = trigger->transitionDestin();
        portal.points.reserve(geometry.size());
        for (const auto &vertex : geometry) {
            portal.points.push_back(glm::vec3(trigger->transform() * glm::vec4(vertex, 1.0f)));
        }
        portals.push_back(std::move(portal));
    }
    return portals;
}

scene::ISceneGraph &Area::graph() {
    return _services.scene.graphs.get(_sceneName);
}

} // namespace game

} // namespace reone
