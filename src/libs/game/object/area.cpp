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

#include "reone/game/camerastyles.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
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
static constexpr float kLineOfSightFOV = glm::radians(60.0f);

static constexpr float kMaxCollisionDistance = 8.0f;
static constexpr float kMaxCollisionDistance2 = kMaxCollisionDistance * kMaxCollisionDistance;

static glm::vec3 g_defaultAmbientColor {0.2f};
static CameraStyle g_defaultCameraStyle {"", 3.2f, 83.0f, 0.45f, 55.0f};

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
    const GraphicsOptions &opts = _game.options().graphics;
    _cameraAspect = opts.width / static_cast<float>(opts.height);

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

void Area::load(std::string name, const Gff &are, const Gff &git, bool fromSave) {
    _name = std::move(name);

    auto areParsed = resource::generated::parseARE(are);
    auto gitParsed = resource::generated::parseGIT(git);

    loadARE(areParsed);
    loadGIT(gitParsed);
    loadLYT();
    loadVIS();
    loadPTH();
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

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    sceneGraph.setAmbientLightColor(_ambientColor);
}

void Area::loadScripts(const resource::generated::ARE &are) {
    _onEnter = are.OnEnter;
    _onExit = are.OnExit;
    _onHeartbeat = are.OnHeartbeat;
    _onUserDefined = are.OnUserDefined;
}

void Area::loadMap(const resource::generated::ARE &are) {
    _game.map().load(_name, are.Map);
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

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto fogProperties = FogProperties();
    fogProperties.enabled = _fogEnabled;
    fogProperties.nearPlane = _fogNear;
    fogProperties.farPlane = _fogFar;
    fogProperties.color = _fogColor;
    sceneGraph.setFog(fogProperties);
}

void Area::loadGIT(const resource::generated::GIT &git) {
    loadProperties(git);
    loadCreatures(git);
    loadDoors(git);
    loadPlaceables(git);
    loadWaypoints(git);
    loadTriggers(git);
    loadSounds(git);
    loadCameras(git);
    loadEncounters(git);
}

void Area::loadProperties(const resource::generated::GIT &git) {
    int musicIdx = git.AreaProperties.MusicDay;
    if (musicIdx) {
        std::shared_ptr<TwoDA> musicTable(_services.resource.twoDas.get("ambientmusic"));
        _music = musicTable->getString(musicIdx, "resource");
    }
}

void Area::loadCreatures(const resource::generated::GIT &git) {
    for (auto &creatureStruct : git.Creature_List) {
        std::shared_ptr<Creature> creature = _game.newCreature(_sceneName);
        creature->loadFromGIT(creatureStruct);
        landObject(*creature);
        add(creature);
    }
}

void Area::loadDoors(const resource::generated::GIT &git) {
    for (auto &doorStruct : git.Door_List) {
        std::shared_ptr<Door> door = _game.newDoor(_sceneName);
        door->loadFromGIT(doorStruct);
        add(door);
    }
}

void Area::loadPlaceables(const resource::generated::GIT &git) {
    for (auto &placeableStruct : git.Placeable_List) {
        std::shared_ptr<Placeable> placeable = _game.newPlaceable(_sceneName);
        placeable->loadFromGIT(placeableStruct);
        add(placeable);
    }
}

void Area::loadWaypoints(const resource::generated::GIT &git) {
    for (auto &waypointStruct : git.WaypointList) {
        std::shared_ptr<Waypoint> waypoint = _game.newWaypoint(_sceneName);
        waypoint->loadFromGIT(waypointStruct);
        add(waypoint);
    }
}

void Area::loadTriggers(const resource::generated::GIT &git) {
    for (auto &gffs : git.TriggerList) {
        std::shared_ptr<Trigger> trigger = _game.newTrigger(_sceneName);
        trigger->loadFromGIT(gffs);
        add(trigger);
    }
}

void Area::loadSounds(const resource::generated::GIT &git) {
    for (auto &soundStruct : git.SoundList) {
        std::shared_ptr<Sound> sound = _game.newSound(_sceneName);
        sound->loadFromGIT(soundStruct);
        add(sound);
    }
}

void Area::loadCameras(const resource::generated::GIT &git) {
    for (auto &cameraStruct : git.CameraList) {
        std::shared_ptr<StaticCamera> camera = _game.newStaticCamera(_cameraAspect, _sceneName);
        camera->loadFromGIT(cameraStruct);
        add(camera);
    }
}

void Area::loadEncounters(const resource::generated::GIT &git) {
    for (auto &encounterStruct : git.Encounter_List) {
        std::shared_ptr<Encounter> encounter = _game.newEncounter(_sceneName);
        encounter->loadFromGIT(encounterStruct);
        add(encounter);
    }
}

void Area::loadStores(const resource::generated::GIT &git) {
    for (auto &storeStruct : git.StoreList) {
        std::shared_ptr<Store> store = _game.newStore(_sceneName);
        add(store);
    }
}

void Area::loadLYT() {
    auto layout = _services.resource.layouts.get(_name);
    if (!layout) {
        throw ResourceNotFoundException("Area LYT not found: " + _name);
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
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

void Area::loadPTH() {
    std::shared_ptr<Path> path(_services.resource.paths.get(_name));
    if (!path) {
        return;
    }
    std::unordered_map<int, float> pointZ;

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    for (size_t i = 0; i < path->points.size(); ++i) {
        const Path::Point &point = path->points[i];
        Collision collision;
        if (!sceneGraph.testElevation(glm::vec2(point.x, point.y), collision)) {
            warn(str(boost::format("Point %d elevation not found") % i));
            continue;
        }
        pointZ.insert(std::make_pair(static_cast<int>(i), collision.intersection.z));
    }

    _pathfinder.load(path->points, pointZ);
}

void Area::initCameras(const glm::vec3 &entryPosition, float entryFacing) {
    glm::vec3 position(entryPosition);
    position.z += 1.7f;

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    _firstPersonCamera = _game.newFirstPersonCamera(glm::radians(kDefaultFieldOfView), _cameraAspect, _sceneName);
    _firstPersonCamera->load();
    _firstPersonCamera->setPosition(position);
    _firstPersonCamera->setFacing(entryFacing);

    _thirdPersonCamera = _game.newThirdPersonCamera(_camStyleDefault, _cameraAspect, _sceneName);
    _thirdPersonCamera->load();
    _thirdPersonCamera->setTargetPosition(position);
    _thirdPersonCamera->setFacing(entryFacing);

    _dialogCamera = _game.newDialogCamera(_camStyleDefault, _cameraAspect, _sceneName);
    _dialogCamera->load();

    _animatedCamera = _game.newAnimatedCamera(_cameraAspect, _sceneName);
    _animatedCamera->load();
}

void Area::add(const std::shared_ptr<Object> &object) {
    _objects.push_back(object);
    _objectsByType[object->type()].push_back(object);
    _objectsByTag[object->tag()].push_back(object);

    determineObjectRoom(*object);

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

void Area::doDestroyObject(uint32_t objectId) {
    auto object = _game.getObjectById(objectId);
    if (!object) {
        return;
    }
    auto room = object->room();
    if (room) {
        room->removeTenant(object.get());
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

    return objects->second[nth];
}

void Area::landObject(Object &object) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    glm::vec3 position(object.position());
    Collision collision;

    // Test elevation at object position
    if (sceneGraph.testElevation(position, collision)) {
        object.setPosition(collision.intersection);
        return;
    }

    // Test elevations in a circle around object position
    for (int i = 0; i < 4; ++i) {
        float angle = i * glm::half_pi<float>();
        position = object.position() + glm::vec3(glm::sin(angle), glm::cos(angle), 0.0f);

        if (sceneGraph.testElevation(position, collision)) {
            object.setPosition(collision.intersection);
            return;
        }
    }
}

void Area::loadParty(const glm::vec3 &position, float facing, bool fromSave) {
    Party &party = _game.party();

    for (int i = 0; i < party.getSize(); ++i) {
        auto member = party.getMember(i);
        if (!fromSave) {
            member->setPosition(position);
            member->setFacing(facing);
        }
        landObject(*member);
        add(member);
    }
}

void Area::unloadParty() {
    for (auto &member : _game.party().members()) {
        doDestroyObject(member.creature->id());
    }
}

void Area::reloadParty() {
    std::shared_ptr<Creature> player(_game.party().player());
    loadParty(player->position(), player->getFacing());
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

    for (auto &object : _objects) {
        object->update(dt);
    }
    updatePerception(dt);
    updateHeartbeat(dt);
}

bool Area::moveCreature(const std::shared_ptr<Creature> &creature, const glm::vec2 &dir, bool run, float dt) {
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

    glm::vec3 dest(origin);
    dest.x += dir.x * speedDt;
    dest.y += dir.y * speedDt;

    if (sceneGraph.testWalk(origin, dest, creature.get(), collision)) {
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

bool Area::moveCreatureTowards(const std::shared_ptr<Creature> &creature, const glm::vec2 &dest, bool run, float dt) {
    glm::vec2 delta(dest - glm::vec2(creature->position()));
    glm::vec2 dir(glm::normalize(delta));
    return moveCreature(creature, dir, run, dt);
}

bool Area::isObjectSeen(const Creature &subject, const Object &object) const {
    if (!subject.isInLineOfSight(object, kLineOfSightFOV)) {
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
    if (!partyLeader) {
        return;
    }
    _thirdPersonCamera->setFacing(partyLeader->getFacing());
}

void Area::startDialog(const std::shared_ptr<Object> &object, const std::string &resRef) {
    std::string finalResRef(resRef);
    if (resRef.empty()) {
        finalResRef = object->conversation();
    }
    if (resRef.empty()) {
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

void Area::checkTriggersIntersection(const std::shared_ptr<Object> &triggerrer) {
    glm::vec2 position2d(triggerrer->position());

    for (auto &object : _objectsByType[ObjectType::Trigger]) {
        auto trigger = std::static_pointer_cast<Trigger>(object);
        if (trigger->isTenant(triggerrer) || !trigger->isIn(position2d)) {
            continue;
        }
        debug(str(boost::format("Trigger '%s' triggerred by '%s'") % trigger->tag() % triggerrer->tag()));
        trigger->addTenant(triggerrer);

        if (!trigger->linkedToModule().empty()) {
            _game.scheduleModuleTransition(trigger->linkedToModule(), trigger->linkedTo());
            return;
        }
        if (!trigger->getOnEnter().empty()) {
            _game.scriptRunner().run(
                trigger->getOnEnter(),
                {{script::ArgKind::Caller, script::Variable::ofObject(trigger->id())},
                 {script::ArgKind::EnteringObject,
                  script::Variable::ofObject(triggerrer->id())}});
        }
    }
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
        std::shared_ptr<Item> item = _game.newItem();
        item->loadFromBlueprint(blueprintResRef);
        object = std::move(item);
        break;
    }
    case ObjectType::Creature: {
        std::shared_ptr<Creature> creature = _game.newCreature();
        creature->loadFromBlueprint(blueprintResRef);
        creature->setPosition(location->position());
        creature->setFacing(location->facing());
        object = std::move(creature);
        break;
    }
    case ObjectType::Placeable: {
        std::shared_ptr<Placeable> placeable = _game.newPlaceable();
        placeable->loadFromBlueprint(blueprintResRef);
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
    if (_selectedObject) {
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

void Area::selectObject(std::shared_ptr<Object> object) {
    _selectedObject = std::move(object);
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

bool Area::matchesCriterias(const Creature &creature, const SearchCriteriaList &criterias, std::shared_ptr<Object> target) const {
    for (auto &criteria : criterias) {
        switch (criteria.first) {
        case CreatureType::Reputation: {
            auto reputation = static_cast<ReputationType>(criteria.second);
            switch (reputation) {
            case ReputationType::Friend:
                if (!target || !_services.game.reputes.getIsFriend(creature, *std::static_pointer_cast<Creature>(target)))
                    return false;
                break;
            case ReputationType::Enemy:
                if (!target || !_services.game.reputes.getIsEnemy(creature, *std::static_pointer_cast<Creature>(target)))
                    return false;
                break;
            case ReputationType::Neutral:
                if (!target || !_services.game.reputes.getIsNeutral(creature, *std::static_pointer_cast<Creature>(target)))
                    return false;
                break;
            default:
                break;
            }
            break;
        }
        case CreatureType::Perception: {
            if (!target)
                return false;

            bool seen = creature.perception().seen.count(target) > 0;
            bool heard = creature.perception().heard.count(target) > 0;
            bool matches = false;
            auto perception = static_cast<PerceptionType>(criteria.second);
            switch (perception) {
            case PerceptionType::SeenAndHeard:
                matches = seen && heard;
                break;
            case PerceptionType::NotSeenAndNotHeard:
                matches = !seen && !heard;
                break;
            case PerceptionType::HeardAndNotSeen:
                matches = heard && !seen;
                break;
            case PerceptionType::SeenAndNotHeard:
                matches = seen && !heard;
                break;
            case PerceptionType::NotHeard:
                matches = !heard;
                break;
            case PerceptionType::Heard:
                matches = heard;
                break;
            case PerceptionType::NotSeen:
                matches = !seen;
                break;
            case PerceptionType::Seen:
                matches = seen;
                break;
            default:
                break;
            }
            if (!matches)
                return false;
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
            bool wasHeard = creature->perception().heard.count(other) > 0;
            bool wasSeen = creature->perception().seen.count(other) > 0;

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

} // namespace game

} // namespace reone
