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

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action/appear.h"
#include "reone/game/camerastyles.h"
#include "reone/game/combattables.h"
#include "reone/game/d20/class.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/effect/visual.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/minigame.h"
#include "reone/game/object/areaofeffect.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/room.h"
#include "reone/game/script/runner.h"
#include "reone/game/surfaces.h"
#include "reone/game/twodautil.h"
#include "reone/game/types.h"
#include "reone/game/visualeffects.h"
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
#include "reone/system/exception/validation.h"
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
// Perception passes: the controlled creature looks around five times a
// second, other creatures every four seconds.
static constexpr float kLeaderPerceptionPass = 0.2f;
static constexpr float kPerceptionPass = 4.0f;
static constexpr float kPerceptionEyeHeight = 1.5f;
// Squared thickness of a door or other object both sounds run into.
static constexpr float kSharedObstacleThickness2 = 0.16f;
// Listen penalties: for a way around the walls, and for each whole metre of wall.
static constexpr int kSoundDetourPenalty = 2;
static constexpr int kSoundWallPenaltyPerMetre = 5;
static constexpr float kLineOfSightHeight = 1.7f;        // TODO: make it appearance-based
// Every surface material, for lines that any face stops.
static const std::set<uint32_t> kEverySurface = [] {
    std::set<uint32_t> materials;
    for (uint32_t material = 0; material < Walkmesh::kSurfaceMaterialCount; ++material) {
        materials.insert(material);
    }
    return materials;
}();

static constexpr float kMaxCollisionDistance = 8.0f;
static constexpr float kMaxCollisionDistance2 = kMaxCollisionDistance * kMaxCollisionDistance;
static constexpr float kCreatureCollisionEpsilon = 0.01f;

// Party members are placed within this radius of their formation spot.
static constexpr float kPartyPlacementRadius = 10.0f;
// A creature placed by the area file or created by a script stands at the
// first safe spot this far around its position that it can walk to.
static constexpr float kCreaturePlacementRadius = 20.0f;
// The leader enters an area on the first safe spot this near its entry point,
// or on the entry point itself when there is none.
static constexpr float kEntryPlacementRadius = 20.0f;
// A party member or puppet spawned by a script stands at the first safe spot
// this near its location.
static constexpr float kPartySpawnPlacementRadius = 20.0f;
// A follower over no room goes back to the first safe spot this near its
// follow point.
static constexpr float kFollowerRecoveryRadius = 5.0f;
// A safe spot's walkmesh check reaches this far below the feet.
static constexpr float kSafeLocationFloorBand = 0.1f;
// A safe spot's room is looked for this far from it.
static constexpr glm::vec3 kSafeLocationRoomOffset {-0.01f, -0.01f, -0.1f};
// The ground height in a room with no face under the point.
static constexpr float kNoRoomFaceHeight = -9.0f;
// Door walls are looked for round the position a safe spot is searched from
// along a diagonal reaching this far either way in x and y, and along the way
// to a spot from this far below the lower of its ends.
static constexpr float kSafeLocationDoorProbe = 0.01f;
static constexpr float kSafeLocationDoorBand = 0.1f;
// The open-spot search stops at rings this far out, and takes no ring spot
// farther than this from the centre.
static constexpr float kOpenSpotSearchExtent = 20.0f;
static constexpr float kOpenSpotMaxDistance = 30.0f;
// Creatures in the way of a corpse bag are looked for this far around its
// collision box, and each is placed within this radius of its pushed spot.
static constexpr float kBudgeMargin = 3.0f;
static constexpr float kBudgePlacementRadius = 10.0f;

static glm::vec3 g_defaultAmbientColor {0.2f};
static CameraStyle g_defaultCameraStyle {"", 3.2f, 83.0f, 0.45f, 55.0f};
static constexpr int kCombatCameraStyle = 8;

// ARE flags
static constexpr uint32_t kInteriorAreaFlag = 0x1;
static constexpr uint32_t kUndergroundAreaFlag = 0x2;

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
    // The area's locals are kept in its GIT.
    deserializeRuntimeState(git, identityContext);

    _unescapable = areParsed.Unescapable != 0;
    _transitDisabled = areParsed.DisableTransit != 0;
    _playerRestrictMode = are.getBool("RestrictMode");
    _transitionPending = are.getBool("TransPending");
    _transitionPendingNextId = are.getUint("TransPendNextID");
    _transitionPendingCurrentId = are.getUint("TransPendCurrID");
    loadARE(areParsed);
    loadLYT();
    loadGIT(git, identityContext);
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
    _services.scene.graphs.get(_sceneName).setRoomWalkmeshes(_layoutWalkmeshes);
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
    loadHearing(are);
    loadGrass(are);
    loadFog(are);
    loadMiniGame(are);
    _roomForceRatings.clear();
    if (_game.isTSL()) {
        for (const auto &room : are.Rooms) {
            _roomForceRatings.emplace(
                boost::to_lower_copy(room.RoomName), room.ForceRating);
        }
    }
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

    // Combat uses a fixed style row.
    auto combatStyle = _services.game.cameraStyles.get(kCombatCameraStyle);
    if (combatStyle) {
        _camStyleCombat = *combatStyle;
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

void Area::setPlayerRestrictMode(bool value) {
    if (value && !_playerRestrictMode) {
        _game.party().endStealth();
    }
    _playerRestrictMode = value;
}

void Area::loadHearing(const resource::generated::ARE &are) {
    _interior = (are.Flags & kInteriorAreaFlag) != 0;
    _underground = (are.Flags & kUndergroundAreaFlag) != 0;
    _modListenCheck = are.ModListenCheck;
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
    _sunFogColor = are.SunFogColor;

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
    fogProperties.color = fogColor();
    sceneGraph.setFog(fogProperties);
}

void Area::loadGIT(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    _game.reserveSavedObjectIds(gff, identityContext, SerializedGraphRoot::AreaGit);
    loadProperties(gff);
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
    loadAreaEffects(gff, identityContext);
}

void Area::loadProperties(const resource::Gff &git) {
    // A field the area file leaves out keeps its current value. The restrict
    // mode and TSL's disabled transit are kept from the area file.
    auto properties = git.findStruct("AreaProperties");
    if (!properties) {
        return;
    }
    properties->readBool(_unescapable, "Unescapable");
    properties->readBool(_transitionPending, "TransPending");
    properties->readByte(_transitionPendingNextId, "TransPendNextID");
    properties->readByte(_transitionPendingCurrentId, "TransPendCurrID");
    properties->readDword(_sunFogColor, "SunFogColor");
    auto &audio = _ambientAudio;
    properties->readInt(audio.musicDelay, "MusicDelay");
    properties->readInt(audio.musicDay, "MusicDay");
    properties->readInt(audio.musicNight, "MusicNight");
    properties->readInt(audio.musicBattle, "MusicBattle");
    properties->readInt(audio.ambientSoundDay, "AmbientSndDay");
    properties->readInt(audio.ambientSoundNight, "AmbientSndNight");
    properties->readInt(audio.ambientSoundDayVolume, "AmbientSndDayVol");
    properties->readInt(audio.ambientSoundNightVolume, "AmbientSndNitVol");
}

void Area::loadCreatures(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    for (const auto &creatureGff : gff.getList("Creature List")) {
        // An instance placed from a template has none when its template is
        // missing, and is left out.
        if (!identityContext.isSerializedState()) {
            std::string templateResRef;
            if (!creatureGff->readResRef(templateResRef, "TemplateResRef") ||
                !Creature::findTemplate(_services.resource.gffs, templateResRef)) {
                continue;
            }
        }
        auto creature = _game.newCreature(*creatureGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            creature->captureSaveRecord(*creatureGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        const glm::vec3 position {creature->position()};
        creature->setPosition(computeSafeLocation(position, kCreaturePlacementRadius, *creature, true).value_or(position));
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
        // A loaded waypoint stands on the ground.
        landObject(*waypoint);
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
        // A module's area places an item's template where its entry says and
        // nothing more of the entry; an item without a template is left out.
        if (!identityContext.isSerializedState()) {
            std::string templateResRef;
            if (!itemGff->readResRef(templateResRef, "TemplateResRef") ||
                !_services.resource.gffs.get(templateResRef, ResType::Uti)) {
                continue;
            }
            auto item = _game.newItemFromBlueprint(templateResRef);
            item->deserializePlacement(*itemGff);
            add(item);
            continue;
        }
        auto item = _game.newItem(*itemGff, identityContext);
        if (identityContext.isSerializedState()) {
            item->captureSaveRecord(*itemGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(item);
    }
}

void Area::loadAreaEffects(const resource::Gff &gff, const SerializedIdentityContext &identityContext) {
    // Each placed area of effect notes the creatures around it again; a
    // restored game records them without signalling.
    for (auto &areaEffectGff : gff.getList("AreaEffectList")) {
        if (areaEffectGff->type() != AreaOfEffect::kSaveStructType) {
            continue;
        }
        auto areaOfEffect = _game.newAreaOfEffect(*areaEffectGff, identityContext, _sceneName);
        if (identityContext.isSerializedState()) {
            areaOfEffect->captureSaveRecord(*areaEffectGff, identityContext, {SaveRecordOriginKind::ActiveGitObject, _name});
        }
        add(areaOfEffect);
        areaOfEffect->fixCorners();
        areaOfEffect->scanOccupants(!identityContext.isSerializedState());
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
        _layoutWalkmeshes.emplace_back();
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
        _layoutWalkmeshes.back() = walkmeshSceneNode;
        std::string roomName = room->name();
        auto entry = _rooms.emplace(roomName, std::move(room)).first;
        _roomOrder.push_back(entry->second.get());
    }

    uniwalkFinalize(_pathfinder.uni);
    // Allow up to 64 concurrent paths.
    _pathfinder.paths.resize(64);
    // Rooms are found under points from the moment they are loaded, and the
    // models in the scene are drawn by the rooms they stand in while the area
    // lasts.
    sceneGraph.setRoomWalkmeshes(_layoutWalkmeshes);
    sceneGraph.setRoomTest(_roomTest);
}

void Area::loadVIS() {
    auto visibility = _services.resource.visibilities.get(_name);
    if (!visibility) {
        return;
    }
    _visibility = fixVisibility(*visibility);
    // With its visibility the area is first shown from its first room.
    _visibilityGraph = true;
    _shownRoom = _roomOrder.empty() ? nullptr : _roomOrder.front();
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

            _dialogCamera = _game.newDialogCamera(_sceneName);
            _dialogCamera->load();

            _animatedCamera = _game.newAnimatedCamera(_sceneName);
            _animatedCamera->load();
        },
        []() noexcept {});
}

Area::~Area() {
    for (Object *object : _objectsByX) {
        if (object->_spatialArea == this) {
            object->_spatialArea = nullptr;
        }
    }
    // Corpses, fading bodies and released effect models go with the area.
    if (_fadingBodies.empty() && _corpses.empty() && _corpseBagBodies.empty() && _releasedEffectModels.empty() &&
        _presentedVisuals.empty() && _hitSparks.empty())
        return;
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    for (auto &body : _fadingBodies) {
        sceneGraph.removeRoot(*body.model);
    }
    for (auto &body : _corpses) {
        sceneGraph.removeRoot(*body.model);
    }
    for (auto &body : _corpseBagBodies) {
        sceneGraph.removeRoot(*body.model);
    }
    for (auto &model : _releasedEffectModels) {
        sceneGraph.removeRoot(*model);
    }
    for (auto &model : _presentedVisuals) {
        sceneGraph.removeRoot(*model);
    }
    for (auto &spark : _hitSparks) {
        sceneGraph.removeRoot(*spark.model);
    }
}

void Area::addToSpatialIndex(Object &object) {
    auto position = std::lower_bound(
        _objectsByX.begin(), _objectsByX.end(), object.position().x,
        [](const Object *entry, float x) { return entry->position().x < x; });
    // An object joins before those already at the same X.
    _objectsByX.insert(position, &object);
    object._spatialArea = this;
}

void Area::removeFromSpatialIndex(Object &object) {
    auto position = std::find(_objectsByX.begin(), _objectsByX.end(), &object);
    if (position != _objectsByX.end()) {
        _objectsByX.erase(position);
    }
    if (object._spatialArea == this) {
        object._spatialArea = nullptr;
    }
    // Removal does not compensate the last successful query index.
}

void Area::updateObjectSpatialIndex(Object &object) {
    auto position = std::find(_objectsByX.begin(), _objectsByX.end(), &object);
    if (position == _objectsByX.end()) {
        return;
    }
    // Movement swaps only across strictly smaller/larger X values;
    // equal-X entries retain their current order, unlike remove-and-reinsert.
    while (position + 1 != _objectsByX.end() &&
           object.position().x > (*(position + 1))->position().x) {
        std::iter_swap(position, position + 1);
        ++position;
    }
    while (position != _objectsByX.begin() &&
           object.position().x < (*(position - 1))->position().x) {
        std::iter_swap(position, position - 1);
        --position;
    }
}

std::optional<std::reference_wrapper<Object>> Area::getObjectInShape(
    bool first, float minX, float maxX,
    const std::function<bool(const Object &)> &matches) {
    size_t index = _shapeQueryIndex ? *_shapeQueryIndex + 1 : 0;
    if (first) {
        auto begin = std::lower_bound(
            _objectsByX.begin(), _objectsByX.end(), minX,
            [](const Object *entry, float x) { return entry->position().x < x; });
        index = static_cast<size_t>(begin - _objectsByX.begin());
    }
    for (; index < _objectsByX.size(); ++index) {
        Object *object = _objectsByX[index];
        if (object->position().x > maxX) {
            break;
        }
        if (matches(*object)) {
            _shapeQueryIndex = index;
            return *object;
        }
    }
    // Failed First/Next calls leave the last successful index unchanged.
    return std::nullopt;
}

std::optional<std::reference_wrapper<Object>> Area::getObjectInPersistentObject(
    const Object &persistent, bool first, int objectFilter, PersistentZone zone) {
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    std::function<bool(const glm::vec3 &)> inside;
    auto extendOverOutline = [&](const glm::vec3 &origin, const std::vector<glm::vec3> &outline) {
        for (const auto &point : outline) {
            minX = std::min(minX, origin.x + point.x);
            maxX = std::max(maxX, origin.x + point.x);
        }
    };
    const auto *encounter = dyn_cast<Encounter>(&persistent);
    if (encounter) {
        extendOverOutline(encounter->position(), encounter->geometry());
        inside = [encounter](const glm::vec3 &point) { return encounter->contains(point); };
    } else if (const auto *areaOfEffect = dyn_cast<AreaOfEffect>(&persistent)) {
        const glm::vec3 center {areaOfEffect->center()};
        minX = center.x - areaOfEffect->boundingRadius();
        maxX = center.x + areaOfEffect->boundingRadius();
        inside = [areaOfEffect](const glm::vec3 &point) { return areaOfEffect->contains(point); };
    } else if (const auto *trigger = dyn_cast<Trigger>(&persistent)) {
        extendOverOutline(trigger->position(), trigger->geometry());
        inside = [trigger](const glm::vec3 &point) { return trigger->isIn(point); };
    } else {
        return std::nullopt;
    }
    if (zone != PersistentZone::Active) {
        return std::nullopt;
    }

    size_t &cursor = _persistentQueryIndex[persistent.id()];
    size_t index = encounter ? cursor + 1 : cursor;
    if (first) {
        auto begin = std::lower_bound(
            _objectsByX.begin(), _objectsByX.end(), minX,
            [](const Object *entry, float x) { return entry->position().x < x; });
        index = static_cast<size_t>(begin - _objectsByX.begin());
    }
    for (; index < _objectsByX.size(); ++index) {
        Object *object = _objectsByX[index];
        if (objectFilter != static_cast<int>(ObjectType::All) && static_cast<int>(object->type()) != objectFilter) {
            continue;
        }
        if (object->position().x > maxX) {
            if (!encounter) {
                cursor = 0;
            }
            return std::nullopt;
        }
        if (encounter) {
            if (!inside(object->position())) {
                return std::nullopt;
            }
        } else {
            const auto *creature = dyn_cast<Creature>(object);
            if (!inside(object->position()) || object->isDead() ||
                (creature && creature->isTemporarilyDead()))
                continue;
        }
        cursor = index + 1;
        return *object;
    }
    return std::nullopt;
}

// TSL: in 261tel every creature but a droid carries the permanent breath
// visual 7003; entering any other module removes it from a non-droid.
static void updateBreathVisual(Game &game, const std::shared_ptr<Object> &object) {
    auto *creature = dyn_cast<Creature>(object.get());
    auto module = game.module();
    if (!creature || !game.isTSL() || !module || creature->racialType() == RacialType::Droid) {
        return;
    }
    static constexpr int kBreathVisual = 7003;
    static const std::string kBreathModule {"261tel"};
    auto isBreath = [](const EffectInstance &effect) {
        return effect.type() == EffectType::Visual && effect.integerParameter(0) == kBreathVisual;
    };
    const auto &effects = creature->effects();
    auto found = std::find_if(effects.begin(), effects.end(), isBreath);
    if (!boost::iequals(module->name(), kBreathModule)) {
        if (found != effects.end()) {
            creature->removeEffectsById(found->id);
        }
        return;
    }
    // Saved effects not yet restored already carry the visual.
    const auto &saved = creature->savedEffects();
    if (found != effects.end() ||
        (creature->isRestoringSavedRuntime() && std::any_of(saved.begin(), saved.end(), isBreath)))
        return;
    auto visual = std::make_shared<VisualEffectMarkerEffect>(kBreathVisual);
    auto instance = visual->saveFacingInstance();
    instance.effect = visual;
    instance.creatorId = creature->id();
    game.bindEffectCreator(instance);
    instance.setDuration(DurationType::Permanent, 0.0f);
    instance.restoring = false;
    creature->applyEffect(std::move(instance));
}

void Area::add(const std::shared_ptr<Object> &object) {
    if (!object || !_game.isRuntimeObjectAttachable(*object) ||
        (object->_spatialArea && object->_spatialArea != this)) {
        throw ValidationException(
            "Area can only own a published or staged runtime object");
    }
    if (std::any_of(
            _objects.begin(), _objects.end(),
            [&object](const auto &existing) {
                return existing.get() == object.get();
            })) {
        throw ValidationException("Runtime object is already owned by this Area");
    }

    try {
        _objects.push_back(object);
        addToSpatialIndex(*object);
        _objectsByType[object->type()].push_back(object);
        _objectsByTag[object->tag()].push_back(object);

        determineObjectRoom(*object);
        attachObjectToSceneGraph(object);
        updateBreathVisual(_game, object);

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
                try {
                    add(trigger);
                } catch (...) {
                    if (trigger->isRuntimeLive()) {
                        _game.destroyRuntimeObjectGraph(trigger);
                    }
                    throw;
                }
            }
        }
    } catch (...) {
        auto owned = std::find_if(
            _objects.begin(), _objects.end(),
            [&object](const auto &existing) {
                return existing.get() == object.get();
            });
        if (owned != _objects.end()) {
            // Area ownership is published only by a successful return. Undo
            // any partially installed indexes/tenancy/presentation while
            // leaving semantic object lifetime with the caller.
            detachObjectRuntime(object);
        }
        throw;
    }

    // A creature brings the areas of effect it carries.
    if (auto creature = dyn_cast<Creature>(object)) {
        creature->placeCarriedAreasOfEffect(*this);
    }
}

std::shared_ptr<Trigger> Area::spawnMine(
    int trapType,
    const glm::vec3 &position,
    const std::shared_ptr<Object> &creator,
    Faction faction,
    int detectDC,
    int disarmDC,
    int ownerDemolitionsSkill) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Trigger> trigger;
    _game.replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            trigger = _game.newTrigger(_sceneName);
            trigger->initMine(*this, trapType, position, creator, faction, detectDC, disarmDC, ownerDemolitionsSkill);
        },
        []() noexcept {});
    try {
        add(trigger);
    } catch (...) {
        if (trigger->isRuntimeLive()) {
            _game.destroyRuntimeObjectGraph(trigger);
        }
        throw;
    }
    return trigger;
}

std::shared_ptr<AreaOfEffect> Area::spawnAreaOfEffect(
    const EffectInstance &effect,
    const glm::vec3 &position,
    float facing,
    const std::shared_ptr<Creature> &carrier) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<AreaOfEffect> areaOfEffect;
    _game.replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            areaOfEffect = _game.newAreaOfEffect(_sceneName);
            areaOfEffect->setCreator(effect.boundCreator());
            areaOfEffect->loadAreaEffect(effect.integerParameter(0));
            // A carried area of effect lasts as long as its effect.
            if (!carrier) {
                areaOfEffect->setDuration(effect.durationType(), effect.duration);
            }
            areaOfEffect->setFacing(facing);
            areaOfEffect->setEffectSpellId(effect.spellId);
            areaOfEffect->overrideScripts(
                effect.stringParameters[0], effect.stringParameters[1], effect.stringParameters[2]);
            if (carrier) {
                areaOfEffect->setCarrier(carrier);
            }
            areaOfEffect->setPosition(position);
        },
        []() noexcept {});
    try {
        add(areaOfEffect);
    } catch (...) {
        if (areaOfEffect->isRuntimeLive()) {
            _game.destroyRuntimeObjectGraph(areaOfEffect);
        }
        throw;
    }
    areaOfEffect->fixCorners();
    areaOfEffect->scanOccupants();
    return areaOfEffect;
}

void Area::applyEffectAtLocation(EffectInstance effect, const Location &location) {
    switch (effect.type()) {
    case EffectType::AreaOfEffect:
        spawnAreaOfEffect(effect, location.position(), objectFacingFromScript(location.facing()), nullptr);
        break;
    case EffectType::Visual:
        // A visual at a point plays once there, whatever its duration.
        presentVisualAt(effect.integerParameter(0), location.position());
        break;
    case EffectType::LinkEffects:
        // The first member, then the second, at the same point. A link
        // restored from a saved value carries no members.
        if (auto link = std::dynamic_pointer_cast<LinkEffectsEffect>(effect.effect)) {
            if (link->childEffect()) {
                applyEffectAtLocation(effect.linkedChild(link->childEffect()), location);
            }
            if (link->parentEffect()) {
                applyEffectAtLocation(effect.linkedChild(link->parentEffect()), location);
            }
        }
        break;
    default:
        break;
    }
}

int Area::playerPartyMineCount() const {
    int count = 0;
    for (const auto &object : _objectsByType.at(ObjectType::Trigger)) {
        const auto &trigger = static_cast<const Trigger &>(*object);
        if (trigger.isTrap() && trigger.isSetByPlayerParty()) {
            ++count;
        }
    }
    return count;
}

bool Area::playerCanSetMines() const {
    static constexpr int kPlayerPartyMineLimit = 15;
    return playerPartyMineCount() < kPlayerPartyMineLimit;
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
    if (object->type() == ObjectType::Trigger) {
        if (auto trapModel = static_cast<Trigger &>(*object).trapModel()) {
            sceneGraph.addRoot(trapModel);
        }
    } else if (object->type() == ObjectType::Placeable) {
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
    auto walkmesh = _services.scene.graphs.get(_sceneName).findRoomWalkmesh(object.position());
    object.setRoom(walkmesh ? dynamic_cast<Room *>(walkmesh->user()) : nullptr);
}

// Room surfaces are looked for from this high above a point down to as far
// below it.
static constexpr float kRoomSurfaceReach = 1000.0f;
// The Trigger surface material.
static constexpr uint32_t kTriggerSurfaceMaterial = 30;

// Casts down through a room's walkmesh from a point to twice kRoomSurfaceReach
// below it, meeting only faces of the given materials seen from above.
static graphics::Raycast raycastRoomDown(const Room &room, const std::set<uint32_t> &materials, const glm::vec3 &top) {
    auto mesh = room.walkmesh();
    auto localOrigin = glm::vec3(mesh->absoluteTransformInverse() * glm::vec4(top, 1.0f));
    return mesh->walkmesh().raycast(
        materials, localOrigin, glm::vec3(0.0f, 0.0f, -1.0f), 2.0f * kRoomSurfaceReach, /*ignoreBackface=*/true);
}

bool Area::testRoomSurface(const glm::vec2 &point, Collision &outCollision) const {
    // Every surface material but Trigger counts here, walkable or not.
    static const std::set<uint32_t> surfaces = [] {
        std::set<uint32_t> all;
        for (uint32_t material = 0; material < graphics::Walkmesh::kSurfaceMaterialCount; ++material) {
            if (material != kTriggerSurfaceMaterial) {
                all.insert(material);
            }
        }
        return all;
    }();
    std::optional<float> nearest;
    for (const Room *room : _roomOrder) {
        auto mesh = room->walkmesh();
        if (!mesh) {
            continue;
        }
        auto raycast = raycastRoomDown(*room, surfaces, glm::vec3 {point, kRoomSurfaceReach});
        if (raycast.fail != graphics::RAYCAST_OK || (nearest && raycast.distance >= *nearest)) {
            continue;
        }
        nearest = raycast.distance;
        outCollision.user = mesh->user();
        outCollision.intersection = glm::vec3 {point, kRoomSurfaceReach - raycast.distance};
        outCollision.normal = mesh->absoluteTransform() * glm::vec4(mesh->walkmesh().normals[raycast.face], 0.0f);
        outCollision.material = static_cast<int>(mesh->walkmesh().materials[raycast.face]);
    }
    return nearest.has_value();
}

int Area::getRoomForceRating(const glm::vec3 &position) const {
    if (!_game.isTSL() || _roomForceRatings.empty()) {
        return 0;
    }
    // A position over no room, or over a room without an ARE entry, has no
    // rating.
    auto walkmesh = _services.scene.graphs.get(_sceneName).findRoomWalkmesh(position);
    if (!walkmesh) {
        return 0;
    }
    const auto &room = static_cast<const Room &>(*walkmesh->user());
    auto found = _roomForceRatings.find(boost::to_lower_copy(room.name()));
    return found != _roomForceRatings.end() ? found->second : 0;
}

int Area::getSurfaceMaterial(const glm::vec3 &position) const {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto room = sceneGraph.findRoomWalkmesh(position);
    if (!room) {
        return 0;
    }
    const auto face = sceneGraph.findRoomFace(*room, position, scene::RoomFaceProbe::Any);
    return face ? static_cast<int>(face->material) : 0;
}

void Area::doDestroyObjects() {
    for (auto &object : _objectsToDestroy) {
        doDestroyObject(object);
    }
    _objectsToDestroy.clear();
}

void Area::detachObjectRuntime(const std::shared_ptr<Object> &object) {
    removeFromSpatialIndex(*object);
    auto room = object->room();
    if (room) {
        object->setRoom(nullptr);
    }

    // Drop the object from any trigger it was standing inside. Detachment is
    // not an authored exit, so no OnExit is fired.
    for (auto &triggerObject : _objectsByType[ObjectType::Trigger]) {
        static_cast<Trigger &>(*triggerObject).removeTenant(object.get());
    }
    for (auto &encounterObject : _objectsByType[ObjectType::Encounter]) {
        static_cast<Encounter &>(*encounterObject).forgetOccupant(object->id());
    }
    for (auto &areaEffectObject : _objectsByType[ObjectType::AreaOfEffect]) {
        static_cast<AreaOfEffect &>(*areaEffectObject).forgetOccupant(object->id());
    }
    _persistentQueryIndex.erase(object->id());

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
    if (object->type() == ObjectType::Trigger) {
        if (auto trapModel = static_cast<Trigger &>(*object).trapModel()) {
            sceneGraph.removeRoot(*trapModel);
        }
    } else if (object->type() == ObjectType::Placeable) {
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

    auto maybeObject = std::find_if(
        _objects.begin(), _objects.end(),
        [&object](auto &candidate) {
            return candidate.get() == object.get();
        });
    if (maybeObject != _objects.end()) {
        _objects.erase(maybeObject);
    }
    auto maybeTagObjects = _objectsByTag.find(object->tag());
    if (maybeTagObjects != _objectsByTag.end()) {
        auto &tagObjects = maybeTagObjects->second;
        auto maybeObjectByTag = std::find_if(
            tagObjects.begin(), tagObjects.end(),
            [&object](auto &candidate) {
                return candidate.get() == object.get();
            });
        if (maybeObjectByTag != tagObjects.end()) {
            tagObjects.erase(maybeObjectByTag);
        }
        if (tagObjects.empty()) {
            _objectsByTag.erase(maybeTagObjects);
        }
    }
    auto maybeTypeObjects = _objectsByType.find(object->type());
    if (maybeTypeObjects != _objectsByType.end()) {
        auto &typeObjects = maybeTypeObjects->second;
        auto maybeObjectByType = std::find_if(
            typeObjects.begin(), typeObjects.end(),
            [&object](auto &candidate) {
                return candidate.get() == object.get();
            });
        if (maybeObjectByType != typeObjects.end()) {
            typeObjects.erase(maybeObjectByType);
        }
    }
    if (_hilightedObject.get() == object.get()) {
        _hilightedObject.reset();
    }
    if (_selectedObject.get() == object.get()) {
        _selectedObject.reset();
    }
}

bool Area::releaseObject(const std::shared_ptr<Object> &object) {
    if (!object) {
        return false;
    }
    auto owned = std::find_if(
        _objects.begin(), _objects.end(),
        [&object](const auto &candidate) {
            return candidate.get() == object.get();
        });
    if (owned == _objects.end()) {
        return false;
    }

    detachObjectRuntime(object);
    return true;
}

// An object is resident exactly while it is in the area's spatial index.
bool Area::isObjectResident(const Object &object) const {
    return object._spatialArea == this;
}

bool Area::isObjectPendingDestruction(const Object &object) const {
    auto live = _game.getObjectById(object.id());
    return live.get() == &object &&
           _objectsToDestroy.find(object.id()) != _objectsToDestroy.end();
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

    // A destroyed encounter creature no longer counts against its wave.
    if (auto creature = dyn_cast<Creature>(object); creature && creature->isEncounterCreature() && !creature->hasLeftEncounter()) {
        creature->leaveEncounter();
    }
    // An area of effect that goes runs OnExit at once for those still inside.
    if (auto areaOfEffect = dyn_cast<AreaOfEffect>(object)) {
        areaOfEffect->releaseOccupants();
    }
    // Only a body standing in the area stays behind it.
    const bool resident = isObjectResident(*object);
    detachObjectRuntime(object);
    if (destroyRuntimeObject) {
        // A live creature destroyed where it stood leaves the area.
        if (auto creature = dyn_cast<Creature>(object); creature && resident && !creature->isDead()) {
            signalExited(*creature);
        }
        if (resident) {
            releaseDestroyedBody(object);
        }
        // A destroyed bag no longer takes its corpse's picks.
        if (auto placeable = dyn_cast<Placeable>(object); placeable && placeable->isBodyBag()) {
            for (auto &body : _corpses) {
                if (body.bodyBagId != objectId) {
                    continue;
                }
                body.bodyBagId = script::kObjectInvalid;
                body.model->setPickable(false);
            }
        }
        _game.destroyRuntimeObjectGraph(object);
    }
}

static constexpr size_t kMaxCorpses = 3;
static constexpr float kBodyFadeSeconds = 2.0f;
static constexpr float kEvictedCorpseDelay = 1.0f;
static constexpr float kEvictedCorpseHold = 45.0f;

// Only the appearance's bag row decides whether a creature's body becomes its
// corpse bag.
static bool isCorpseBagAppearance(const ICombatTables &tables, const Creature &creature) {
    const auto row = creature.appearanceBodyBagRow();
    return row && tables.bodyBag(*row).corpse;
}

void Area::releaseDestroyedBody(const std::shared_ptr<Object> &object) {
    auto model = std::dynamic_pointer_cast<ModelSceneNode>(object->sceneNode());
    // A kept corpse is tested before the no-fade removal.
    if (!model || (!object->keepsCorpse() && object->deletesWithoutFade())) {
        return;
    }
    // The body goes on without the object's visual effects.
    object->endEffectPresentations();
    auto *creature = dyn_cast<Creature>(object.get());
    const uint32_t bodyBagId = creature ? creature->spawnedBodyBag() : script::kObjectInvalid;
    const bool linked = object->keepsCorpse() && bodyBagId != script::kObjectInvalid;
    model->setPickable(false);
    _services.scene.graphs.get(_sceneName).addRoot(model);
    ReleasedBody body {object, std::move(model)};
    body.bodyBagId = bodyBagId;
    if (linked && isCorpseBagAppearance(_services.game.combatTables, *creature)) {
        _corpseBagBodies.push_back(std::move(body));
        return;
    }
    if (object->keepsCorpse()) {
        body.model->setPickable(linked);
        _corpses.push_back(std::move(body));
        if (_corpses.size() > kMaxCorpses) {
            auto oldest = std::move(_corpses.front());
            _corpses.pop_front();
            // The bag of a corpse that starts to fade shows at once.
            if (auto bag = _game.getObjectById<Placeable>(oldest.bodyBagId)) {
                bag->setBodyBagVisible(true);
            }
            oldest.model->setPickable(false);
            oldest.delay = kEvictedCorpseDelay;
            oldest.hold = kEvictedCorpseHold;
            _fadingBodies.push_back(std::move(oldest));
        }
        return;
    }
    body.delay = static_cast<float>(object->fadeOutTime()) / 1000.0f;
    _fadingBodies.push_back(std::move(body));
}

bool Area::hasCorpseWithBodyBag(uint32_t bagId) const {
    return std::any_of(_corpses.begin(), _corpses.end(), [bagId](const ReleasedBody &body) {
        return body.bodyBagId == bagId;
    });
}

std::shared_ptr<ModelSceneNode> Area::takeCorpseBagBody(uint32_t bagId) {
    auto found = std::find_if(_corpseBagBodies.begin(), _corpseBagBodies.end(), [bagId](const ReleasedBody &body) {
        return body.bodyBagId == bagId;
    });
    if (found == _corpseBagBodies.end()) {
        return nullptr;
    }
    auto model = std::move(found->model);
    _corpseBagBodies.erase(found);
    _services.scene.graphs.get(_sceneName).removeRoot(*model);
    return model;
}

void Area::releaseEffectModel(std::shared_ptr<ModelSceneNode> model) {
    if (model) {
        _releasedEffectModels.push_back(std::move(model));
    }
}

void Area::presentVisualAt(int visualEffectId, const glm::vec3 &position) {
    const auto found = _services.game.visualEffects.get(visualEffectId);
    if (!found) {
        return;
    }
    const VisualEffectDesc &desc = *found;
    if (desc.locationModel) {
        auto &sceneGraph = _services.scene.graphs.get(_sceneName);
        auto model = sceneGraph.newModel(*desc.locationModel, ModelUsage::Projectile);
        sceneGraph.addRoot(model);
        model->setLocalTransform(glm::translate(position));
        model->playAnimation("impact");
        _presentedVisuals.push_back(std::move(model));
    }
    if (desc.soundImpact) {
        _services.audio.mixer.play(desc.soundImpact, audio::AudioType::Sound, 1.0f, false, position);
    }
}

static constexpr size_t kMaxHitSparksPerCreature = 12;
// The spark is tipped one radian about the world's X axis.
static constexpr float kHitSparkTilt = 1.0f;
// A blow shorter than this has no direction.
static constexpr float kShortestBlow = 1e-9f;

// The spark turns about the vertical by the angle from the target's facing to
// the blow, counter-clockwise, and keeps that turn against the world's axes
// whichever way the target faces.
void Area::presentHitSpark(const Creature &target, int visualEffectId, const glm::vec3 &direction,
                           const std::string &hook) {
    // A creature without a body model, or whose model lacks the hook node,
    // shows no spark; nor does a visual effect without an impact model.
    auto body = std::static_pointer_cast<ModelSceneNode>(target.sceneNode());
    if (!body) {
        return;
    }
    const auto shown = std::count_if(_hitSparks.begin(), _hitSparks.end(), [&body](const HitSpark &spark) {
        return spark.body.lock() == body;
    });
    if (static_cast<size_t>(shown) >= kMaxHitSparksPerCreature) {
        return;
    }
    const VisualEffectDesc &desc = *_services.game.visualEffects.get(visualEffectId);
    auto *node = body->getNodeByName(hook);
    if (!desc.impactModel || !node) {
        return;
    }
    const float facing = target.getFacing();
    const glm::vec3 front {-std::sin(facing), std::cos(facing), 0.0f};
    // A blow with no length counts as one along the world's X axis.
    const float length = glm::length(direction);
    const glm::vec3 blow = length < kShortestBlow ? glm::vec3 {1.0f, 0.0f, 0.0f} : direction / length;
    float angle = std::acos(std::min(glm::dot(blow, front), 1.0f));
    if (front.y * blow.x - front.x * blow.y >= 0.0f) {
        angle = -angle;
    }
    HitSpark spark;
    spark.body = body;
    spark.hook = hook;
    spark.orientation = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f)) *
                        glm::angleAxis(kHitSparkTilt, glm::vec3(1.0f, 0.0f, 0.0f));
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    spark.model = sceneGraph.newModel(*desc.impactModel, ModelUsage::Projectile);
    sceneGraph.addRoot(spark.model);
    spark.model->setLocalTransform(glm::translate(node->origin()) * glm::mat4_cast(spark.orientation));
    spark.model->playAnimation("impact");
    _hitSparks.push_back(std::move(spark));
}

void Area::updateReleasedPresentation(float dt) {
    if (_fadingBodies.empty() && _corpses.empty() && _corpseBagBodies.empty() && _releasedEffectModels.empty() &&
        _presentedVisuals.empty() && _hitSparks.empty())
        return;
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    for (auto it = _fadingBodies.begin(); it != _fadingBodies.end();) {
        it->elapsed += dt;
        const float alpha = std::clamp(1.0f - (it->elapsed - it->delay) / kBodyFadeSeconds, 0.0f, 1.0f);
        it->model->setFadeAlpha(alpha);
        if (alpha == 0.0f && it->elapsed >= it->delay + it->hold) {
            sceneGraph.removeRoot(*it->model);
            it = _fadingBodies.erase(it);
        } else {
            ++it;
        }
    }
    // A released effect model goes once its emitter has no live particles.
    for (auto it = _releasedEffectModels.begin(); it != _releasedEffectModels.end();) {
        auto *emitter = (*it)->getNodeByName("OmenEmitter01");
        const bool live = emitter && std::any_of(emitter->children().begin(), emitter->children().end(),
                                                 [](const SceneNode *child) { return child->type() == SceneNodeType::Particle; });
        if (live) {
            ++it;
        } else {
            sceneGraph.removeRoot(**it);
            it = _releasedEffectModels.erase(it);
        }
    }
    for (auto it = _presentedVisuals.begin(); it != _presentedVisuals.end();) {
        if ((*it)->isAnimationFinished()) {
            sceneGraph.removeRoot(**it);
            it = _presentedVisuals.erase(it);
        } else {
            ++it;
        }
    }
    // A spark keeps to its node while the body shows it.
    for (auto it = _hitSparks.begin(); it != _hitSparks.end();) {
        if (it->model->isAnimationFinished()) {
            sceneGraph.removeRoot(*it->model);
            it = _hitSparks.erase(it);
            continue;
        }
        // A spark whose body has gone, or whose rebuilt body lacks the node,
        // stays where it is.
        auto body = it->body.lock();
        auto *node = body ? body->getNodeByName(it->hook) : nullptr;
        if (node) {
            it->model->setLocalTransform(glm::translate(node->origin()) * glm::mat4_cast(it->orientation));
        }
        ++it;
    }
}

ObjectList &Area::getObjectsByType(ObjectType type) {
    return _objectsByType.find(type)->second;
}

std::shared_ptr<Creature> Area::getFirstFactionMember(Faction faction, int pc) {
    return findFactionMember(faction, pc, 0);
}

std::shared_ptr<Creature> Area::getNextFactionMember(Faction faction, int pc) {
    auto cursor = _factionMemberCursors.find(faction);
    return findFactionMember(faction, pc, cursor != _factionMemberCursors.end() ? cursor->second : 0);
}

std::shared_ptr<Creature> Area::findFactionMember(Faction faction, int pc, size_t from) {
    std::vector<std::shared_ptr<Creature>> members;
    for (const auto &object : getObjectsByType(ObjectType::Creature)) {
        auto creature = std::static_pointer_cast<Creature>(object);
        if (creature->faction() == faction) {
            members.push_back(std::move(creature));
        }
    }
    std::stable_sort(members.begin(), members.end(), [](const auto &left, const auto &right) {
        return left->factionJoinOrder() < right->factionJoinOrder();
    });
    for (size_t index = from; index < members.size(); ++index) {
        if (members[index]->isPCValue() != pc) {
            continue;
        }
        _factionMemberCursors[faction] = index + 1;
        return members[index];
    }
    return nullptr;
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

void Area::landObject(Object &object) {
    const glm::vec3 position {object.position()};
    object.setPosition(glm::vec3(position.x, position.y, groundHeight(position, GroundFace::Locating)));
}

void Area::groundCreature(Creature &creature) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    const glm::vec3 position {creature.position()};
    std::shared_ptr<scene::WalkmeshSceneNode> walkmesh;
    const int hint = creature.groundRoomHint();
    if (_game.isTSL() && hint >= 0 && hint < static_cast<int>(_layoutWalkmeshes.size()) && _layoutWalkmeshes[hint] &&
        sceneGraph.findRoomFace(*_layoutWalkmeshes[hint], position, scene::RoomFaceProbe::Locating)) {
        walkmesh = _layoutWalkmeshes[hint];
    } else {
        walkmesh = sceneGraph.findRoomWalkmesh(position);
        const auto found = std::find(_layoutWalkmeshes.begin(), _layoutWalkmeshes.end(), walkmesh);
        if (walkmesh && found != _layoutWalkmeshes.end()) {
            creature.setGroundRoomHint(static_cast<int>(found - _layoutWalkmeshes.begin()));
        }
    }
    glm::vec3 grounded {position.x, position.y, 0.0f};
    if (walkmesh) {
        const auto face = sceneGraph.findRoomFace(*walkmesh, position, scene::RoomFaceProbe::Locating);
        grounded.z = face ? face->height : kNoRoomFaceHeight;
    } else {
        // A follower off the rooms goes back to its follow point.
        const auto &party = _game.party();
        const auto &members = party.members();
        const auto leader = party.getLeader();
        if (leader.get() != &creature && !_game.isDialogUnderWay()) {
            for (size_t slot = 0; slot < members.size() && slot < static_cast<size_t>(kPartyFollowSlots); ++slot) {
                if (members[slot].creature.get() != &creature) {
                    continue;
                }
                const glm::vec3 point {party.followPoint(static_cast<int>(slot))};
                if (auto spot = computeSafeLocation(point, kFollowerRecoveryRadius, creature, true)) {
                    grounded = glm::vec3(spot->x, spot->y, groundHeight(position, GroundFace::Locating));
                } else {
                    grounded = point;
                }
                break;
            }
        }
    }
    if (grounded == position) {
        return;
    }
    creature.setPosition(grounded);
    if (glm::vec2(grounded) != glm::vec2(position)) {
        determineObjectRoom(creature);
    }
}

glm::vec3 Area::partySpawnPosition(const glm::vec3 &location, const Creature &creature) const {
    if (!hasRoomAt(location)) {
        return glm::vec3(0.0f);
    }
    return computeSafeLocation(location, kPartySpawnPlacementRadius, creature, true).value_or(location);
}

bool Area::isPositionWalkable(const glm::vec3 &position) const {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto room = sceneGraph.findRoomWalkmesh(position);
    if (!room) {
        return false;
    }
    const auto face = sceneGraph.findRoomFace(*room, position, scene::RoomFaceProbe::WalkCheck);
    return _services.game.surfaces.isWalkable(face ? static_cast<int>(face->material) : 0);
}

float Area::groundHeight(const glm::vec3 &position, GroundFace face) const {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto room = sceneGraph.findRoomWalkmesh(position);
    if (!room) {
        return 0.0f;
    }
    const auto probe = face == GroundFace::Locating ? scene::RoomFaceProbe::Locating : scene::RoomFaceProbe::Any;
    const auto found = sceneGraph.findRoomFace(*room, position, probe);
    return found ? found->height : kNoRoomFaceHeight;
}

glm::vec3 Area::groundNormal(const glm::vec3 &position) const {
    static constexpr glm::vec3 kUp {0.0f, 0.0f, 1.0f};
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto room = sceneGraph.findRoomWalkmesh(position);
    if (!room) {
        return kUp;
    }
    const auto found = sceneGraph.findRoomFace(*room, position, scene::RoomFaceProbe::Locating);
    return found ? found->normal : kUp;
}

bool Area::hasRoomAt(const glm::vec3 &position) const {
    return _services.scene.graphs.get(_sceneName).findRoomWalkmesh(position) != nullptr;
}

// Blocking faces are the non-walkable ones that walking is checked against,
// in the rooms and in doors and placeables alike.
static std::set<uint32_t> blockingSurfaces(const ISurfaces &surfaces) {
    const auto walkable = surfaces.getWalkableSurfaces();
    std::set<uint32_t> blocking;
    for (uint32_t material : surfaces.getWalkcheckSurfaces()) {
        if (walkable.count(material) == 0) {
            blocking.insert(material);
        }
    }
    return blocking;
}

// Whether an enabled walkmesh has a blocking face within radius of the level
// segment between two points, between two heights.
static bool hasBlockingFaceWithin(const std::shared_ptr<scene::WalkmeshSceneNode> &node, const std::set<uint32_t> &blocking,
                                  const glm::vec3 &from, const glm::vec3 &to, float radius, float minZ, float maxZ) {
    if (!node || !node->isEnabled()) {
        return false;
    }
    const glm::vec3 localFrom {node->absoluteTransformInverse() * glm::vec4(from, 1.0f)};
    const glm::vec3 localTo {node->absoluteTransformInverse() * glm::vec4(to, 1.0f)};
    const float shift = localFrom.z - from.z;
    return node->walkmesh().hasFaceWithin(blocking, localFrom, localTo, radius, minZ + shift, maxZ + shift);
}

// Whether a wall of a door as it stands lies within radius of the level
// segment between two points, between two heights.
static bool hasDoorWallWithin(const Door &door, const std::set<uint32_t> &blocking,
                              const glm::vec3 &from, const glm::vec3 &to, float radius, float minZ, float maxZ) {
    for (const auto &node : {door.walkmeshOpen1(), door.walkmeshOpen2(), door.walkmeshClosed()}) {
        if (hasBlockingFaceWithin(node, blocking, from, to, radius, minZ, maxZ)) {
            return true;
        }
    }
    return false;
}

bool Area::hasDoorWallAlong(const glm::vec3 &from, const glm::vec3 &to, const Creature &creature, float minZ, float maxZ) const {
    const auto blocking = blockingSurfaces(_services.game.surfaces);
    const float radius = creature.personalSpace();
    for (const auto &object : _objectsByType.at(ObjectType::Door)) {
        if (hasDoorWallWithin(static_cast<const Door &>(*object), blocking, from, to, radius, minZ, maxZ)) {
            return true;
        }
    }
    return false;
}

std::optional<std::reference_wrapper<const Object>> Area::findStaticWallNear(
    const glm::vec3 &point, const Creature &creature, float minZ, float maxZ) const {
    const auto blocking = blockingSurfaces(_services.game.surfaces);
    const float radius = creature.personalSpace();
    for (const auto &object : _objectsByType.at(ObjectType::Door)) {
        if (hasDoorWallWithin(static_cast<const Door &>(*object), blocking, point, point, radius, minZ, maxZ)) {
            return std::cref(*object);
        }
    }
    for (const auto &object : _objectsByType.at(ObjectType::Placeable)) {
        if (hasBlockingFaceWithin(static_cast<const Placeable &>(*object).walkmesh(), blocking, point, point, radius, minZ, maxZ)) {
            return std::cref(*object);
        }
    }
    return std::nullopt;
}

bool Area::isSafeLocationPoint(const glm::vec3 &point, const Creature &creature) const {
    if (!hasRoomAt(point + kSafeLocationRoomOffset)) {
        return false;
    }

    const auto blocking = blockingSurfaces(_services.game.surfaces);
    const float radius = creature.personalSpace();
    const float minZ = point.z - kSafeLocationFloorBand;
    const float maxZ = point.z + creature.collisionHeight();
    auto blocked = [&](const std::shared_ptr<scene::WalkmeshSceneNode> &node) {
        return hasBlockingFaceWithin(node, blocking, point, point, radius, minZ, maxZ);
    };
    for (const auto &[name, room] : _rooms) {
        if (blocked(room->walkmesh())) {
            return false;
        }
    }
    for (const auto &object : _objectsByType.at(ObjectType::Door)) {
        const auto &door = static_cast<const Door &>(*object);
        if (blocked(door.walkmeshOpen1()) || blocked(door.walkmeshOpen2()) || blocked(door.walkmeshClosed())) {
            return false;
        }
    }
    for (const auto &object : _objectsByType.at(ObjectType::Placeable)) {
        if (blocked(static_cast<const Placeable &>(*object).walkmesh())) {
            return false;
        }
    }

    const float ownRadius = creature.creaturePersonalSpace() + kCreatureCollisionEpsilon;
    for (const auto &object : _objectsByType.at(ObjectType::Creature)) {
        const auto &other = static_cast<const Creature &>(*object);
        if (&other == &creature || other.isDead() || other.isTemporarilyDead()) {
            continue;
        }
        const float reach = ownRadius + other.creaturePersonalSpace();
        const glm::vec2 offset {glm::vec2(other.position()) - glm::vec2(point)};
        if (glm::dot(offset, offset) < reach * reach) {
            return false;
        }
    }
    return true;
}

std::optional<glm::vec3> Area::searchSquareRings(
    const glm::vec3 &center,
    float height,
    float halfWidth,
    float step,
    const std::function<bool(float)> &continues,
    const std::function<std::optional<glm::vec3>(const glm::vec3 &, bool)> &accept) const {
    auto spotAt = [&](float x, float y, bool row) -> std::optional<glm::vec3> {
        height = groundHeight(glm::vec3(x, y, height), GroundFace::Top);
        return accept(glm::vec3(x, y, height), row);
    };
    do {
        const float minX = center.x - halfWidth;
        const float maxX = center.x + halfWidth;
        const float minY = center.y - halfWidth;
        const float maxY = center.y + halfWidth;
        for (float x = minX; x <= maxX; x += step) {
            for (float y : {minY, maxY}) {
                if (auto spot = spotAt(x, y, true)) {
                    return spot;
                }
            }
        }
        for (float x : {minX, maxX}) {
            for (float y = minY + step; y <= maxY - step; y += step) {
                if (auto spot = spotAt(x, y, false)) {
                    return spot;
                }
            }
        }
        halfWidth += step;
    } while (continues(halfWidth));
    return std::nullopt;
}

std::optional<glm::vec3> Area::computeSafeLocation(
    const glm::vec3 &position, float radius, const Creature &creature, bool clearLine) const {
    const glm::vec3 grounded {position.x, position.y, groundHeight(position, GroundFace::Locating)};
    if (isSafeLocationPoint(grounded, creature)) {
        return grounded;
    }
    // Reachability only counts from a position on walkable ground, clear of
    // the walls of any door.
    const glm::vec3 probe {kSafeLocationDoorProbe, kSafeLocationDoorProbe, 0.0f};
    if (!isPositionWalkable(grounded) || hasDoorWallAlong(grounded - probe, grounded + probe, creature, grounded.z, grounded.z + creature.collisionHeight())) {
        clearLine = false;
    }

    const float step = creature.personalSpace();
    float halfWidth;
    if (radius > 0.0f) {
        if (!(radius > 1.0f)) {
            return std::nullopt;
        }
        halfWidth = 1.0f;
    } else {
        halfWidth = step;
    }
    const float limit = std::max(step, -radius);
    // Row spots must be reachable unless only a creature is in the way;
    // column spots need only a way that stays within the rooms. Neither way
    // may pass a door wall.
    return searchSquareRings(position, grounded.z, halfWidth, step, [&](float width) { return radius > 0.0f ? radius > width : limit >= width; }, [&](const glm::vec3 &spot, bool row) -> std::optional<glm::vec3> {
            if (!isSafeLocationPoint(spot, creature)) {
                return std::nullopt;
            }
            if (!clearLine) {
                return spot;
            }
            const DirectLine line = testDirectLine(creature, spot, position).result;
            if (row ? line != DirectLine::Clear && line != DirectLine::CreatureBlocked : line == DirectLine::LeavesRooms) {
                return std::nullopt;
            }
            const float minZ = std::min(spot.z, position.z) - kSafeLocationDoorBand;
            const float maxZ = std::max(spot.z, position.z) + creature.collisionHeight();
            if (hasDoorWallAlong(spot, position, creature, minZ, maxZ)) {
                return std::nullopt;
            }
            return spot; });
}

std::optional<glm::vec3> Area::computeSafeLocationInDirection(
    const glm::vec3 &base, const glm::vec3 &direction, float radius, const Creature &creature, bool clearLine) const {
    static constexpr float kProbe = 0.01f;
    static constexpr float kRowRounding = 0.01f;
    // Spots stand at the top ground height, their rooms looked for around the
    // height of the first spot for it, and around base's height for the rows.
    const auto spotAt = [this](const glm::vec3 &point) {
        return glm::vec3(point.x, point.y, groundHeight(point, GroundFace::Top));
    };
    const glm::vec3 first {spotAt(base + direction)};
    if (isSafeLocationPoint(first, creature)) {
        return first;
    }
    const glm::vec3 probe {kProbe, kProbe, 0.0f};
    if (!isPositionWalkable(first) || testDirectLine(creature, first - probe, first + probe).result == DirectLine::Blocked) {
        clearLine = false;
    }

    const float step = creature.personalSpace();
    const glm::vec2 along {glm::normalize(glm::vec2(direction))};
    const glm::vec2 across {along.y, -along.x};
    for (float width = step; radius > width; width += step) {
        const int row = static_cast<int>((width + kRowRounding) / step);
        for (int i = -row; i <= row; ++i) {
            const glm::vec3 spot {spotAt(glm::vec3(glm::vec2(base) + static_cast<float>(row) * along + static_cast<float>(i) * across, base.z))};
            if (!isSafeLocationPoint(spot, creature)) {
                continue;
            }
            if (clearLine && testDirectLine(creature, base, spot).result == DirectLine::Blocked) {
                continue;
            }
            return spot;
        }
    }
    return computeSafeLocation(base, radius, creature, clearLine);
}

std::optional<glm::vec3> Area::findOpenSpotInSight(const glm::vec3 &center, const Creature &creature) const {
    const glm::vec3 grounded {center.x, center.y, groundHeight(center, GroundFace::Top)};
    if (isSafeLocationPoint(grounded, creature)) {
        return grounded;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    return searchSquareRings(center, grounded.z, 1.0f, creature.personalSpace(), [](float width) { return width < kOpenSpotSearchExtent; }, [&](const glm::vec3 &spot, bool) -> std::optional<glm::vec3> {
            if (!isSafeLocationPoint(spot, creature)) {
                return std::nullopt;
            }
            if (glm::distance(center, spot) > kOpenSpotMaxDistance) {
                return std::nullopt;
            }
            Collision collision;
            if (sceneGraph.testLineOfSight(center, spot, collision)) {
                return std::nullopt;
            }
            return spot; });
}

// Candidates lie strictly within three metres of the box across y and short
// of three metres past its far x edge; along x they start at the centre's
// offset into the box, less three metres. They are taken in order of x and
// moved one at a time, so each placement sees the creatures moved before it.
// A creature at the very centre is pushed along +x.
void Area::budgeCreatures(const glm::vec3 &center, const graphics::AABB &bounds,
                          std::optional<std::reference_wrapper<const Door>> shutDoor) {
    static constexpr float kDirectionEpsilon = 1e-9f;
    const glm::vec3 &min = bounds.min();
    const glm::vec3 &max = bounds.max();
    const float firstX = center.x - min.x - kBudgeMargin;
    std::vector<std::shared_ptr<Creature>> candidates;
    for (const auto &object : _objectsByType.at(ObjectType::Creature)) {
        const glm::vec3 &position = object->position();
        if (position.x < firstX || position.x >= max.x + kBudgeMargin) {
            continue;
        }
        if (position.y <= min.y - kBudgeMargin || position.y >= max.y + kBudgeMargin) {
            continue;
        }
        candidates.push_back(std::static_pointer_cast<Creature>(object));
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a->position().x < b->position().x;
    });
    for (const auto &creature : candidates) {
        const glm::vec3 position {creature->position()};
        if (isSafeLocationPoint(position, *creature)) {
            continue;
        }
        const glm::vec3 offset {position - center};
        const float length = glm::length(offset);
        const glm::vec3 away {length < kDirectionEpsilon ? glm::vec3(1.0f, 0.0f, 0.0f) : offset / length};
        const glm::vec3 start {shutDoor ? shutDoor->get().nearestActionPoint(position, false) : position + away};
        if (auto spot = computeSafeLocation(start, kBudgePlacementRadius, *creature, false)) {
            creature->setPosition(*spot);
            determineObjectRoom(*creature);
        }
    }
}

void Area::updateSubAreaOccupancy(const std::shared_ptr<Creature> &creature, bool announce) {
    for (const auto &object : _objectsByType[ObjectType::Encounter]) {
        static_cast<Encounter &>(*object).updateOccupancy(creature, announce);
    }
    for (const auto &object : _objectsByType[ObjectType::AreaOfEffect]) {
        static_cast<AreaOfEffect &>(*object).updateOccupancy(creature, announce);
    }
}

void Area::jumpCarriedAreaEffects(const Creature &carrier) {
    for (const auto &object : _objectsByType[ObjectType::AreaOfEffect]) {
        auto &areaOfEffect = static_cast<AreaOfEffect &>(*object);
        if (areaOfEffect.carrier().get() == &carrier) {
            areaOfEffect.jumpToCarrier();
        }
    }
}

std::optional<std::reference_wrapper<const Creature>> Area::findBlockingCreature(const Creature &mover, const glm::vec3 &from, const glm::vec3 &to) const {
    CreatureCollision collision;
    findCreatureCollision(mover, from, to, collision);
    return collision.creature;
}

Area::DirectLineTest Area::testDirectLine(
    const Creature &mover,
    const glm::vec3 &from,
    const glm::vec3 &to,
    std::optional<std::reference_wrapper<const Creature>> ignored) const {
    // In TSL a walk is followed through no more than this many rooms.
    static constexpr int kMaxRoomsOnWalk = 100;
    // Door and placeable walls are looked for this far below and above the
    // heights the walk spans in the room it leaves the rooms from.
    static constexpr float kExitWallReach = 1000.0f;
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    DirectLineTest test;
    std::vector<float> crossings;
    Collision roomExit;
    const bool leavesRooms = sceneGraph.testRoomWalk(from, to, mover.personalSpace(), mover.collisionHeight(),
                                                     _game.isTSL() ? std::make_optional(kMaxRoomsOnWalk) : std::nullopt,
                                                     crossings, roomExit);
    if (leavesRooms && !roomExit.user) {
        test.result = DirectLine::LeavesRooms;
        test.stopPoint = from;
        test.normal = roomExit.normal;
        return test;
    }
    // The walk is settled room by room: what stops it in an earlier room
    // counts before anything in a later one, and in each room walls count
    // before creatures. Only what stands before the walk runs out of the
    // rooms counts.
    const glm::vec3 line {to - from};
    const auto fractionOf = [&](const glm::vec3 &point) {
        return glm::dot(point - from, line) / glm::dot(line, line);
    };
    const auto roomStart = [&](float fraction) {
        float start = 0.0f;
        for (float crossing : crossings) {
            if (crossing < fraction) {
                start = crossing;
            }
        }
        return start;
    };
    const auto creatureBefore = [&](const glm::vec3 &end) {
        CreatureCollision creatureCollision;
        if (!findCreatureCollision(mover, from, end, creatureCollision, ignored)) {
            return false;
        }
        test.result = DirectLine::CreatureBlocked;
        test.blocker = creatureCollision.creature;
        test.stopPoint = from + (end - from) * creatureCollision.time;
        test.normal = glm::vec3 {creatureCollision.normal, 0.0f};
        return true;
    };
    const auto creatureInEarlierRoom = [&](float fraction) {
        const float start = roomStart(fraction);
        return start > 0.0f && creatureBefore(from + line * start);
    };
    const glm::vec3 reach {leavesRooms ? roomExit.intersection : to};
    Collision collision;
    if (findWalkLineWall(mover, from, reach, collision)) {
        if (creatureInEarlierRoom(fractionOf(collision.intersection))) {
            return test;
        }
        test.result = DirectLine::Blocked;
        test.stopPoint = collision.intersection;
        if (auto door = dynamic_cast<const Door *>(collision.user)) {
            test.door = *door;
        }
        test.normal = collision.normal;
        return test;
    }
    if (leavesRooms) {
        // Where the walk runs out of the rooms, a door or placeable wall
        // there stops it first.
        const float exit = fractionOf(reach);
        const glm::vec3 entry {from + line * roomStart(exit)};
        const float minZ = std::min(entry.z, reach.z) - kExitWallReach;
        const float maxZ = std::max(entry.z, reach.z) + kExitWallReach;
        if (auto object = findStaticWallNear(reach, mover, minZ, maxZ)) {
            if (creatureInEarlierRoom(exit)) {
                return test;
            }
            test.result = DirectLine::Blocked;
            test.stopPoint = reach;
            if (object->get().type() == ObjectType::Door) {
                test.door = static_cast<const Door &>(object->get());
            }
            test.normal = roomExit.normal;
            return test;
        }
    }
    if (creatureBefore(reach)) {
        return test;
    }
    if (leavesRooms) {
        test.result = DirectLine::LeavesRooms;
        test.stopPoint = reach;
        test.normal = roomExit.normal;
    }
    return test;
}

bool Area::isWalkLineBlocked(const Creature &mover, const glm::vec3 &from, const glm::vec3 &to) const {
    Collision collision;
    return findWalkLineWall(mover, from, to, collision);
}

bool Area::findWalkLineWall(const Creature &mover, const glm::vec3 &from, const glm::vec3 &to, Collision &outCollision) const {
    // Walls are tested just above the ground, as walking tests them.
    const glm::vec3 lift {0.0f, 0.0f, 0.1f};
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    if (!sceneGraph.testWalk(from + lift, to + lift, &mover, outCollision)) {
        return false;
    }
    outCollision.intersection -= lift;
    return true;
}

glm::vec3 Area::randomDestination(const Creature &mover, int range) const {
    static constexpr int kTries = 22;
    static constexpr float kShrink = 0.75f;
    static constexpr float kShortestShrunk = 2.5f;
    static constexpr float kShortest = 1.0f;
    static constexpr int kRandMax = 0x7fff;

    const glm::vec3 &origin = mover.position();
    const int span = 2 * range;
    const int dx = randomInt(0, kRandMax) % span - range;
    const int dy = randomInt(0, kRandMax) % span - range;
    glm::vec3 target {origin.x + static_cast<float>(dx), origin.y + static_cast<float>(dy), 0.0f};
    target.z = groundHeight(target, GroundFace::Locating);

    glm::vec3 direction {target - origin};
    const float fullDistance = glm::length(direction);
    if (fullDistance > 0.0f) {
        direction /= fullDistance;
    }
    float distance = fullDistance;
    for (int attempt = 0; attempt < kTries; ++attempt) {
        if (testDirectLine(mover, origin, target).result == DirectLine::Clear) {
            return target;
        }
        if (kShrink * distance >= kShortestShrunk) {
            distance *= kShrink;
        } else {
            // The new direction is a quarter turn either way, keeping only its
            // level part, or straight back; the distance starts over.
            switch (randomInt(0, kRandMax) % 3) {
            case 1:
                direction = glm::vec3(-direction.y, direction.x, 0.0f);
                break;
            case 2:
                direction = glm::vec3(direction.y, -direction.x, 0.0f);
                break;
            default:
                direction = -direction;
                break;
            }
            distance = fullDistance;
        }
        target = origin + direction * distance;
        if (distance < kShortest) {
            return origin;
        }
    }
    return origin;
}

std::optional<glm::vec3> Area::randomWalkPoint(const Creature &walker, const glm::vec3 &home) const {
    static constexpr int kTries = 22;
    static constexpr int kSpan = 15;
    static constexpr int kReach = 7;
    static constexpr float kShrink = 0.75f;
    static constexpr float kShortestShare = 0.3f;
    static constexpr float kShortest = 1.0f;
    static constexpr int kRandMax = 0x7fff;

    const int dx = randomInt(0, kRandMax) % kSpan - kReach;
    const int dy = randomInt(0, kRandMax) % kSpan - kReach;
    glm::vec3 target {home.x + static_cast<float>(dx), home.y + static_cast<float>(dy), 0.0f};
    target.z = groundHeight(target, GroundFace::Locating);

    const glm::vec3 &origin = walker.position();
    glm::vec3 direction {target - origin};
    const float fullDistance = glm::length(direction);
    if (fullDistance > 0.0f) {
        direction /= fullDistance;
    }
    const float shortestShrunk = kShortestShare * fullDistance;
    float distance = fullDistance;
    for (int attempt = 0; attempt < kTries; ++attempt) {
        if (testDirectLine(walker, origin, target).result == DirectLine::Clear) {
            return target;
        }
        if (kShrink * distance >= shortestShrunk) {
            distance *= kShrink;
        } else {
            // The level part of the same bearing, with the distance started over.
            direction.z = 0.0f;
            distance = fullDistance;
        }
        target = origin + direction * distance;
        if (distance < kShortest) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

glm::vec3 Area::computeAwayPoint(const Creature &mover, const glm::vec3 &threat, float distance) const {
    // Direction weights, from toward the threat round to straight away.
    static constexpr std::array<float, 8> kWeights {1.0f, 4.0f, 16.0f, 64.0f, 256.0f, 64.0f, 16.0f, 4.0f};
    // A blocked way is halved while the half is still at least this fraction.
    static constexpr float kSmallestHalf = 0.005f;

    const glm::vec3 &from = mover.position();
    const float standing2 = glm::distance2(from, threat);
    glm::vec2 towards {threat.x - from.x, threat.y - from.y};
    towards = glm::length(towards) < 1e-9f ? glm::vec2(1.0f, 0.0f) : glm::normalize(towards);

    glm::vec3 best {from};
    float bestScore = 0.0f;
    for (int direction = 0; direction < 8; ++direction) {
        const float angle = static_cast<float>(direction) * glm::quarter_pi<float>();
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const glm::vec3 offset(glm::vec2(towards.x * cosine + towards.y * sine,
                                         towards.y * cosine - towards.x * sine) *
                                   distance,
                               0.0f);
        glm::vec3 point {from + offset};
        // A point still within reach of the threat is pushed out twice as far.
        if (glm::distance2(threat, point) < distance * distance) {
            point += offset;
        }

        if (testDirectLine(mover, from, point).result != DirectLine::Clear) {
            float fraction = 1.0f;
            bool clear = false;
            do {
                point = from + offset * fraction;
                clear = testDirectLine(mover, from, point).result == DirectLine::Clear;
                fraction *= 0.5f;
            } while (!clear && fraction >= kSmallestHalf);
            if (!clear || glm::distance2(threat, point) < standing2) {
                continue;
            }
        }

        const float score = glm::distance(point, threat) * kWeights[direction];
        if (score >= bestScore) {
            best = point;
            bestScore = score;
        }
    }
    return best;
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

        if (index < kPartyFollowSlots) {
            glm::quat rotation(glm::angleAxis(leader->getFacing(), glm::vec3(0.0f, 0.0f, 1.0f)));
            position += rotation * _game.party().formationOffset(index);
        }

        member->setPosition(computeSafeLocation(position, kPartyPlacementRadius, *member, false).value_or(position));
        member->setFacing(leader->getFacing());
    }

    landObject(*member);

    if (loaded) {
        determineObjectRoom(*member);
        return;
    }

    add(member);
}

void Area::retireCreatureAreaRuntime(const std::shared_ptr<Creature> &creature) {
    if (!creature ||
        std::find(_objects.begin(), _objects.end(), creature) == _objects.end()) {
        return;
    }

    auto runtimeObjects = _game.party().runtimeObjects();
    std::set<uint32_t> retainedObjectIds;
    for (const auto &object : runtimeObjects) {
        if (object)
            retainedObjectIds.insert(object->id());
    }

    // Creature-side handles must retire while Pathfinder and every referenced
    // outgoing object are alive. Area-owned structural attachments follow,
    // then the raw Room pointer is invalidated before Room destruction.
    creature->retireAreaRuntime(_pathfinder, retainedObjectIds);
    doDestroyObject(creature->id(), false);
    creature->setRoom(nullptr);
}

void Area::retirePartyMemberAreaRuntime(const std::shared_ptr<Creature> &member) {
    const bool exits = isObjectResident(*member) && !member->isDead();
    retireCreatureAreaRuntime(member);
    if (exits) {
        signalExited(*member);
    }
}

void Area::loadParty(const glm::vec3 &position, float facing, bool preserveSavedPlacement) {
    Party &party = _game.party();
    auto leader = party.getLeader();
    // A party left with no one under control and no follower places no one.
    if (!leader) {
        return;
    }

    // A party brought into the world anew, not restored from a saved game,
    // spawns each companion the player does not control: one that lay dying
    // gets up, and each joins the player's faction and, once placed, catches
    // up with the party's experience. A puppet only gets up.
    const bool spawnsAnew = !_game.isLoadingFromSaveGame();
    const auto spawnsCompanion = [&](const Party::Member &member) {
        return spawnsAnew && member.creature && party.isCompanion(member);
    };
    const auto revive = [this](Creature &creature) {
        if (creature.currentHitPoints() > 0) {
            return;
        }
        creature.setRaiseable(true);
        creature.applyEffect(_game.newEffect<ResurrectionEffect>(0), DurationType::Instant);
    };
    for (const auto &member : party.members()) {
        if (!spawnsCompanion(member)) {
            continue;
        }
        revive(*member.creature);
        party.spawnIntoPlayerFaction(*member.creature);
    }

    if (!preserveSavedPlacement) {
        leader->setPosition(computeSafeLocation(position, kEntryPlacementRadius, *leader, true).value_or(position));
        leader->setFacing(facing);
    }
    loadPartyMember(leader, 0, preserveSavedPlacement);
    // The player arrives in the encounter areas and areas of effect it stands
    // in; a restored game records them without signalling.
    updateSubAreaOccupancy(leader, !preserveSavedPlacement);
    // Entering the area starts the trail over at the leader.
    party.resetFollowPath(*this, leader->position(), leader->getFacing(), false);

    // A place in the party that names nothing holds no one to place. Each
    // following companion and each puppet the party brings into the area
    // drops its actions, commandable or not; the leader, the player
    // character and a companion taken under control keep theirs.
    for (int i = 1; i < party.getSize(); ++i) {
        const auto &member = party.members()[i];
        if (!member.creature) {
            continue;
        }
        if (member.npc != kNpcPlayer && member.npc != party.controlledNpc()) {
            member.creature->clearAllActions(true, true);
        }
        loadPartyMember(member.creature, i, preserveSavedPlacement);
    }
    for (const auto &member : party.members()) {
        if (spawnsCompanion(member)) {
            party.catchUpExperience(member.npc, *member.creature);
        }
    }
    int formationIndex = party.getSize();
    for (int puppet : party.persistedState().puppetIds) {
        if (auto creature = party.getAvailablePuppet(puppet, true)) {
            if (spawnsAnew) {
                revive(*creature);
            }
            creature->clearAllActions(true, true);
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

        if (index < kPartyFollowSlots) {
            glm::quat rotation(glm::angleAxis(
                leader->getFacing(), glm::vec3(0.0f, 0.0f, 1.0f)));
            position += rotation * _game.party().formationOffset(index);
        }

        member->setPosition(computeSafeLocation(position, kPartyPlacementRadius, *member, false).value_or(position));
        member->setFacing(leader->getFacing());
    }

    landObject(*member);

    if (loaded) {
        determineObjectRoom(*member);
        return;
    }

    add(member);
}

void Area::placeControlledCreature(
    const std::shared_ptr<Creature> &creature,
    const glm::vec3 &position,
    float facing) {

    // An actor taking control from outside the area arrives where the one
    // giving up control stood, facing its way, and is announced to the area.
    creature->setPosition(position);
    creature->setFacing(facing);
    repositionPartyMember(creature, 0);
    signalEntered(*creature);
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
        if (auto member = party.getMember(i)) {
            repositionPartyMember(member, i);
        }
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
    updateReleasedPresentation(dt);
    updateObjectSelection();

    if (_game.isPaused()) {
        return;
    }
    // A movie started by an object holds the objects after it until the
    // movie ends.
    auto held = [this]() { return static_cast<bool>(_game.movie()); };
    Object::update(dt);
    if (held()) {
        return;
    }

    // A creature placed since the last update runs its creation script first,
    // even when a time stop holds it.
    runSpawnScripts();
    if (held()) {
        return;
    }
    // Creatures look around before they act.
    updatePerception(dt);
    // Update can create new objects, so iterate with indices. Objects held by
    // a time stop stand still.
    for (size_t i = 0; i < _objects.size() && !held(); ++i) {
        if (_game.isFrozenByTimeStop(*_objects[i])) {
            continue;
        }
        _objects[i]->update(dt);
    }
    if (held()) {
        return;
    }
    updateLeaderTriggerOccupancy();
}

void Area::updateHeartbeat() {
    updateStampedHeartbeat(_onHeartbeat);
}

void Area::runObjectActions() {
    // A movie started by an object holds the objects after it.
    auto held = [this]() { return static_cast<bool>(_game.movie()); };
    runActions();
    if (held()) {
        return;
    }
    runSpawnScripts();
    // Actions can create new objects, so iterate with indices. Objects held
    // by a time stop stand still.
    for (size_t i = 0; i < _objects.size() && !held(); ++i) {
        if (_game.isFrozenByTimeStop(*_objects[i])) {
            continue;
        }
        _objects[i]->runActions();
    }
}

bool Area::moveCreature(const std::shared_ptr<Creature> &creature, const glm::vec2 &dir, bool run, float dt,
                        float maxDistance, MoveFacing facing) {
    if (!creature || creature->isMovementRestricted()) {
        return false;
    }
    float speed = run ? creature->runSpeed() : creature->walkSpeed();
    return moveCreatureByDistance(creature, dir, std::min(speed * dt, maxDistance), facing);
}

bool Area::moveCreatureByDistance(const std::shared_ptr<Creature> &creature,
                                  const glm::vec2 &dir, float speedDt, MoveFacing facing) {
    static glm::vec3 up {0.0f, 0.0f, 1.0f};
    static glm::vec3 zOffset {0.0f, 0.0f, 0.1f};

    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    Collision collision;

    // Test obstacle between origin and destination

    glm::vec3 origin(creature->position());
    origin.z += 0.1f;

    glm::vec3 dest(origin);
    dest.x += dir.x * speedDt;
    dest.y += dir.y * speedDt;

    bool obstructed = sceneGraph.testWalk(origin, dest, creature.get(), collision);

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

    return stepCreatureTo(creature, glm::vec3(glm::vec2(dest), creature->position().z), facing);
}

bool Area::stepCreatureTo(const std::shared_ptr<Creature> &creature, const glm::vec3 &point, MoveFacing facing) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    const auto walkmesh = sceneGraph.findRoomWalkmesh(point);
    if (!walkmesh) {
        return false;
    }
    const auto face = sceneGraph.findRoomFace(*walkmesh, point, scene::RoomFaceProbe::Locating);
    // A creature never steps onto ground it cannot walk on.
    if (!_services.game.surfaces.isWalkable(face ? static_cast<int>(face->material) : 0)) {
        return false;
    }

    const glm::vec3 origin {creature->position()};
    const glm::vec3 dest {point.x, point.y, face ? face->height : kNoRoomFaceHeight};
    auto userRoom = dynamic_cast<Room *>(walkmesh->user());
    auto prevRoom = creature->room();

    creature->setRoom(userRoom);
    creature->setPosition(dest);
    // A step turns the creature along the way it actually went, unless
    // asked to keep its facing.
    const glm::vec2 step {glm::vec2(dest) - glm::vec2(origin)};
    if (facing == MoveFacing::Turn && step != glm::vec2(0.0f)) {
        creature->turnAlongStep(step);
    }

    if (creature == _game.party().getLeader()) {
        _game.party().recordLeaderStep(*this, creature->position(), creature->getFacing());
        onPartyLeaderMoved(userRoom != prevRoom);
    }

    checkTriggersIntersection(creature);
    updateSubAreaOccupancy(creature);

    return true;
}

bool Area::findCreatureCollision(
    const Creature &creature,
    const glm::vec3 &origin,
    const glm::vec3 &destination,
    CreatureCollision &outCollision,
    std::optional<std::reference_wrapper<const Creature>> ignoredCreature) const {
    bool found = false;
    outCollision.time = 1.0f;

    for (const auto &object : _objectsByType.at(ObjectType::Creature)) {
        const auto &other = static_cast<const Creature &>(*object);
        if (&other == &creature || (ignoredCreature && &other == &ignoredCreature->get()) || other.isDead() || other.isTemporarilyDead()) {
            continue;
        }

        float radius = creature.creaturePersonalSpace() + other.creaturePersonalSpace() + kCreatureCollisionEpsilon;
        float time;
        glm::vec2 normal;
        if (sweepCircle(glm::vec2(origin), glm::vec2(destination), glm::vec2(other.position()), radius, time, normal) &&
            (!found || time < outCollision.time)) {
            found = true;
            outCollision.creature = other;
            outCollision.time = time;
            outCollision.normal = normal;
        }
    }

    return found;
}

// The line of sight runs between the two at eye height. The walls and floors
// of every room stop it, whatever their material, and so does every closed
// door but the one looked at, however far away. Open doors, placeables and
// creatures do not.
bool Area::isObjectSeen(const Creature &subject, const Object &object) const {
    if (!object.visible()) {
        return false;
    }
    const auto *creature = dyn_cast<Creature>(&object);
    if (creature && creature->isInvisibleTo(subject)) {
        return false;
    }

    glm::vec3 origin(subject.position());
    origin.z += kLineOfSightHeight;

    glm::vec3 dest(object.position());
    dest.z += kLineOfSightHeight;

    const float length = glm::distance(origin, dest);
    const glm::vec3 dir {(dest - origin) / length};

    for (const auto &room : _rooms) {
        const auto &walkmesh = room.second->walkmesh();
        if (walkmesh && !walkmesh->walkmesh().raycast(kEverySurface, origin, dir, length, /*ignoreBackface=*/false).fail) {
            return false;
        }
    }
    for (const auto &candidate : _objectsByType.at(ObjectType::Door)) {
        const auto &walkmesh = static_cast<const Door &>(*candidate).walkmeshClosed();
        if (candidate.get() == &object || !walkmesh || !walkmesh->isEnabled()) {
            continue;
        }
        const glm::vec3 originLocal {walkmesh->absoluteTransformInverse() * glm::vec4(origin, 1.0f)};
        const glm::vec3 dirLocal {walkmesh->absoluteTransformInverse() * glm::vec4(dir, 0.0f)};
        if (!walkmesh->walkmesh().raycast(kEverySurface, originLocal, dirLocal, length, /*ignoreBackface=*/false).fail) {
            return false;
        }
    }

    return true;
}

void Area::runSpawnScripts() {
    // Creation scripts can create creatures, so iterate with indices. A movie
    // started by one holds the rest until it ends.
    auto &creatures = _objectsByType[ObjectType::Creature];
    for (size_t i = 0; i < creatures.size() && !_game.movie(); ++i) {
        auto creature = std::static_pointer_cast<Creature>(creatures[i]);
        creature->runSpawnScript();
    }
}

// The event carries whether a saved game was being loaded when it was made.
void Area::signalEntered(Creature &creature) {
    static constexpr int kEnteredEvent = static_cast<int>(SavedSignalEventType::Entered);
    _game.queueScriptEvent(*this, creature,
                           Event(kEnteredEvent, {static_cast<int32_t>(_game.isLoadingFromSaveGame())}, {}, {}, {}));
}

void Area::receiveEnteredSignal(const std::shared_ptr<Object> &entering, bool loadFromSaveGame) {
    _lastEnteredId = entering ? entering->id() : script::kObjectInvalid;
    if (_onEnter.empty()) {
        return;
    }

    struct LoadFromSaveScope {
        Game &game;
        bool live;
        ~LoadFromSaveScope() { game.setLoadingFromSaveGame(live); }
    } scope {_game, _game.isLoadingFromSaveGame()};
    _game.setLoadingFromSaveGame(loadFromSaveGame);
    _game.scriptRunner().run(_onEnter, _id);
}

void Area::signalExited(Creature &creature) {
    static constexpr int kExitedEvent = static_cast<int>(SavedSignalEventType::Exited);
    _game.queueScriptEvent(*this, creature, Event(kExitedEvent, {}, {}, {}, {}));
}

void Area::receiveExitedSignal(const std::shared_ptr<Object> &exiting) {
    _lastLeftId = exiting ? exiting->id() : script::kObjectInvalid;
    if (_onExit.empty()) {
        return;
    }
    _game.scriptRunner().run(_onExit, _id);
}

void Area::runOnExitScript() {
    auto player = _game.party().player();
    if (!player)
        return;

    _lastLeftId = player->id();
    if (_onExit.empty())
        return;

    _game.scriptRunner().run(_onExit, _id);
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
    _game.startDialog(object, finalResRef, {}, _game.party().player());
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
    if (_visibilityGraph) {
        if (auto position = roomViewPosition()) {
            auto walkmesh = _services.scene.graphs.get(_sceneName).findRoomWalkmesh(*position);
            if (auto room = walkmesh ? dynamic_cast<Room *>(walkmesh->user()) : nullptr) {
                _shownRoom = room;
            }
        }
    }
    _cullingRooms = _visibilityGraph && _shownRoom;
    // In TSL a listed room must be in view or have background geometry.
    const Camera *camera = _game.getActiveCamera();
    auto shownWhenListed = [this, camera](const Room &room) {
        return !_game.isTSL() || room.hasBackgroundGeometry() || !camera || camera->cameraSceneNode()->camera()->isInFrustum(room.box());
    };
    for (auto &room : _rooms) {
        bool visible = !_cullingRooms || room.second.get() == _shownRoom;
        if (!visible) {
            auto seen = _visibility.equal_range(_shownRoom->name());
            visible = std::any_of(seen.first, seen.second, [&room](const auto &pair) { return pair.second == room.first; }) &&
                      shownWhenListed(*room.second);
        }
        room.second->setVisible(visible);
    }
}

std::optional<glm::vec3> Area::roomViewPosition() const {
    auto leader = _game.party().getLeader();
    if (!leader) {
        return std::nullopt;
    }
    // A conversation under way with a camera of its own looks from that camera.
    const CameraType cameraType = _game.cameraType();
    if (_game.isDialogUnderWay() && (cameraType == CameraType::Dialog || cameraType == CameraType::Static || cameraType == CameraType::Animated)) {
        if (auto camera = _game.getActiveCamera()) {
            return glm::vec3(camera->sceneNode()->absoluteTransform()[3]);
        }
    }
    return leader->position();
}

bool Area::isModelInShownRoom(const ModelSceneNode &model) const {
    // A model whose subclassification has this flag is in every room.
    static constexpr int kInEveryRoom = 1;
    if (!_cullingRooms || (model.model().subclassification() & kInEveryRoom) != 0) {
        return true;
    }
    const glm::mat4 &transform = model.absoluteTransform();
    const glm::vec3 centre {(model.model().classification() & MdlClassification::character) != 0
                                ? glm::vec3(transform[3])
                                : glm::vec3(transform * glm::vec4(0.5f * (model.model().aabb().min() + model.model().aabb().max()), 1.0f))};
    bool held = false;
    for (const auto &room : _rooms) {
        if (!room.second->boxHolds(glm::vec2(centre))) {
            continue;
        }
        if (room.second->isVisible()) {
            return true;
        }
        held = true;
    }
    return !held;
}

void Area::update3rdPersonCameraTarget() {
    auto partyLeader = _game.party().getLeader();
    if (!partyLeader || !_thirdPersonCamera) {
        return;
    }
    // The combat camera hangs over the leader at the camera hook's height and
    // checks its view from the leader's head height.
    if (_thirdPersonCamera->isCombat()) {
        const glm::vec3 position {partyLeader->position()};
        _thirdPersonCamera->setCombatAnchor(
            position + glm::vec3(0.0f, 0.0f, partyLeader->cameraHookHeight()),
            position + glm::vec3(0.0f, 0.0f, partyLeader->headHeight()));
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

void Area::checkTriggersIntersection(const std::shared_ptr<Object> &triggerrer, bool fireTransitions) {
    // Entering scripts may add triggers (a spawned mine), which are left out;
    // triggers are destroyed only between updates.
    const ObjectList &triggers = _objectsByType[ObjectType::Trigger];
    const size_t count = triggers.size();
    for (size_t i = 0; i < count; ++i) {
        auto trigger = std::static_pointer_cast<Trigger>(triggers[i]);
        if (!trigger->isActive()) {
            trigger->removeTenant(triggerrer.get());
            continue;
        }
        bool inside = trigger->isIn(triggerrer->position());
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

void Area::setThirdPersonCombat(bool combat) {
    if (!_thirdPersonCamera || _thirdPersonCamera->isCombat() == combat) {
        return;
    }
    _thirdPersonCamera->setStyle(combat ? _camStyleCombat : _camStyleDefault);
    _thirdPersonCamera->setCombat(combat);
    update3rdPersonCameraTarget();
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

// The area owns its music and ambient sound settings. The player is told of
// each change and follows it while it plays this area.

void Area::playMusic(bool play) {
    _ambientAudio.musicPlaying = play;
    _game.areaMusic().playMusic(*this, play);
}

// Any delay is kept and saved, zero or less included; the player ignores a
// delay of zero or less.
void Area::setMusicDelay(int delay) {
    if (_ambientAudio.musicDelay == delay) {
        return;
    }
    _ambientAudio.musicDelay = delay;
    _game.areaMusic().musicDelayChanged(*this);
}

// Setting the day track restarts it even when it is unchanged.
void Area::setMusicDayTrack(int track, bool playsThroughMovies) {
    const int previous = _ambientAudio.musicDay;
    _ambientAudio.musicDay = track;
    _game.areaMusic().musicDayTrackChanged(*this, previous, playsThroughMovies);
}

void Area::setMusicNightTrack(int track, bool playsThroughMovies) {
    const int previous = _ambientAudio.musicNight;
    if (previous == track) {
        return;
    }
    _ambientAudio.musicNight = track;
    _game.areaMusic().musicNightTrackChanged(*this, previous, playsThroughMovies);
}

void Area::playBattleMusic(bool play) {
    if (_ambientAudio.battleMusicPlaying == play) {
        return;
    }
    _ambientAudio.battleMusicPlaying = play;
    _game.areaMusic().playBattleMusic(*this, play);
}

void Area::setBattleMusicTrack(int track) {
    const int previous = _ambientAudio.musicBattle;
    if (previous == track) {
        return;
    }
    _ambientAudio.musicBattle = track;
    _game.areaMusic().battleMusicTrackChanged(*this, previous);
}

void Area::playAmbientSound(bool play) {
    _ambientAudio.ambientSoundPlaying = play;
    _game.areaMusic().playAmbientSound(*this, play);
}

void Area::setAmbientSoundDayTrack(int track) {
    if (_ambientAudio.ambientSoundDay == track) {
        return;
    }
    _ambientAudio.ambientSoundDay = track;
    _game.areaMusic().ambientDayTrackChanged(*this);
}

void Area::setAmbientSoundNightTrack(int track) {
    if (_ambientAudio.ambientSoundNight == track) {
        return;
    }
    _ambientAudio.ambientSoundNight = track;
    _game.areaMusic().ambientNightTrackChanged(*this);
}

// Volumes outside 0..100 are ignored.
static bool isAmbientVolume(int volume) {
    static constexpr int kMaxAmbientVolume = 100;
    return volume >= 0 && volume <= kMaxAmbientVolume;
}

void Area::setAmbientSoundDayVolume(int volume) {
    if (!isAmbientVolume(volume) || _ambientAudio.ambientSoundDayVolume == volume) {
        return;
    }
    _ambientAudio.ambientSoundDayVolume = volume;
    _game.areaMusic().ambientDayVolumeChanged(*this);
}

void Area::setAmbientSoundNightVolume(int volume) {
    if (!isAmbientVolume(volume)) {
        return;
    }
    _ambientAudio.ambientSoundNightVolume = volume;
}

std::shared_ptr<Object> Area::createObject(ObjectType type, const std::string &blueprintResRef, const std::shared_ptr<Location> &location, bool appear) {
    std::shared_ptr<Object> object;
    switch (type) {
    case ObjectType::Item: {
        std::shared_ptr<Item> item =
            _game.newItemFromBlueprint(blueprintResRef);
        object = std::move(item);
        break;
    }
    case ObjectType::Creature:
        // A creature without a template is not created.
        object = _game.newCreatureFromBlueprint(blueprintResRef);
        break;
    case ObjectType::Placeable: {
        std::shared_ptr<Placeable> placeable =
            _game.newPlaceableFromBlueprint(blueprintResRef);
        object = std::move(placeable);
        break;
    }
    case ObjectType::Waypoint:
        object = _game.newWaypointFromBlueprint(blueprintResRef);
        break;
    case ObjectType::Store:
        object = _game.newStoreFromBlueprint(blueprintResRef);
        break;
    default:
        warn("Unsupported object type: " + std::to_string(static_cast<int>(type)));
        break;
    }
    if (!object) {
        return nullptr;
    }

    if (location) {
        object->setPosition(location->position());
        object->setFacing(objectFacingFromScript(location->facing()));
        if (auto *creature = dyn_cast<Creature>(object.get())) {
            creature->setPosition(computeSafeLocation(location->position(), kCreaturePlacementRadius, *creature, true).value_or(location->position()));
            landObject(*creature);
        }
    }

    add(object);

    auto creature = std::dynamic_pointer_cast<Creature>(object);
    // An appearing creature starts with the appear action, which the actions
    // of its creation script, run later, wait behind.
    if (creature && appear) {
        creature->clearAllActions(true);
        if (creature->isCommandable()) {
            creature->addAction(_game.newAction<AppearAction>());
        }
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
        // Turning the camera with the mouse drops the object under the pointer.
        if (camera->isMouseLookMode() || !_hilightedObject->isSelectable()) {
            _hilightedObject.reset();
        } else {
            Collision collision;
            auto objectPos = _hilightedObject->getSelectablePosition();
            if (glm::distance2(cameraPos, objectPos) > kSelectionDistance2 || (sceneGraph.testLineOfSight(cameraPos, objectPos, collision) && collision.user != _hilightedObject.get())) {
                _hilightedObject.reset();
            }
        }
    }
    // The selected object mirrors the leader's target, which the game keeps valid.
}

void Area::hilightObject(std::shared_ptr<Object> object) {
    _hilightedObject = std::move(object);
}

void Area::selectObject(std::shared_ptr<Object> object, bool force) {
    _selectedObject = std::move(object);
    _forceSelection = force;
}

std::optional<std::reference_wrapper<const Object>> Area::getNearestObject(const Object &target, int nth, const std::function<bool(const Object &)> &matches) const {
    if (!isObjectResident(target)) {
        return std::nullopt;
    }
    auto base = std::find(_objectsByX.begin(), _objectsByX.end(), &target);
    return findNearestObject(target.position(), static_cast<size_t>(base - _objectsByX.begin()), nth, matches);
}

std::optional<std::reference_wrapper<const Object>> Area::getNearestObjectToLocation(const glm::vec3 &position, int nth, const std::function<bool(const Object &)> &matches) const {
    auto base = std::lower_bound(
        _objectsByX.begin(), _objectsByX.end(), position.x,
        [](const Object *entry, float x) { return entry->position().x < x; });
    // With no object at or beyond the position along the x axis there is nothing to find.
    if (base == _objectsByX.end()) {
        return std::nullopt;
    }
    return findNearestObject(position, static_cast<size_t>(base - _objectsByX.begin()), nth, matches);
}

// The search walks outwards from the base entry of the x-ordered list: one
// step east, one west, two west, two east, three east, three west, and so on.
// The base entry is never visited. Matches rank by distance, ties in the
// order the walk finds them.
std::optional<std::reference_wrapper<const Object>> Area::findNearestObject(const glm::vec3 &origin, size_t base, int nth,
                                                                            const std::function<bool(const Object &)> &matches) const {
    std::vector<std::pair<std::reference_wrapper<const Object>, float>> candidates;
    const auto count = static_cast<std::ptrdiff_t>(_objectsByX.size());
    const auto start = static_cast<std::ptrdiff_t>(base);
    auto visit = [&](std::ptrdiff_t index) {
        if (index < 0 || index >= count) {
            return;
        }
        const Object &object = *_objectsByX[index];
        // A linked door's transition trigger stands in for the door and is
        // never a result.
        const auto *trigger = dyn_cast<Trigger>(&object);
        if ((trigger && trigger->isLinkedDoorTransition()) || !matches(object)) {
            return;
        }
        candidates.emplace_back(object, object.getSquareDistanceTo(origin));
    };
    for (std::ptrdiff_t step = 1; step <= count; ++step) {
        const std::ptrdiff_t first = step % 2 ? step : -step;
        visit(start + first);
        visit(start - first);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](auto &left, auto &right) {
        return left.second < right.second;
    });

    // A script asking for an nth object past the matches gets none.
    if (nth < 0 || nth >= static_cast<int>(candidates.size())) {
        return std::nullopt;
    }
    return candidates[nth].first;
}

std::shared_ptr<Creature> Area::getNearestCreature(const std::shared_ptr<Object> &target, const SearchCriteriaList &criterias, int nth) {
    // The search runs around an object standing in the area, which is never
    // one of its results.
    if (!isObjectResident(*target)) {
        return nullptr;
    }
    return findNearestCreature(target->position(), *target, *target, criterias, nth);
}

std::shared_ptr<Creature> Area::getNearestCreatureToLocation(const Location &location, const SearchCriteriaList &criterias, int nth) {
    // The search starts from the first object at or beyond the location along
    // the x axis, and that object is never one of its results. With no such
    // object there is nothing to find. Among objects at the same x, the first
    // in the x order is taken.
    auto start = std::lower_bound(
        _objectsByX.begin(), _objectsByX.end(), location.position().x,
        [](const Object *entry, float x) { return entry->position().x < x; });
    if (start == _objectsByX.end()) {
        return nullptr;
    }
    return findNearestCreature(location.position(), **start, std::nullopt, criterias, nth);
}

std::shared_ptr<Creature> Area::findNearestCreature(const glm::vec3 &origin, const Object &excluded,
                                                    std::optional<std::reference_wrapper<const Object>> searching,
                                                    const SearchCriteriaList &criterias, int nth) {
    std::vector<std::pair<std::shared_ptr<Creature>, float>> candidates;

    for (auto &object : getObjectsByType(ObjectType::Creature)) {
        if (object.get() == &excluded) {
            continue;
        }
        auto creature = std::static_pointer_cast<Creature>(object);
        // The dead, and party members down at no vitality, are never found.
        if (creature->isDead() || creature->isTemporarilyDead()) {
            continue;
        }
        if (!matchesCriterias(*creature, criterias, searching)) {
            continue;
        }
        float distance2 = creature->getSquareDistanceTo(origin);
        candidates.push_back(std::make_pair(std::move(creature), distance2));
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](auto &left, auto &right) {
        return left.second < right.second;
    });

    return nth >= 0 && nth < static_cast<int>(candidates.size()) ? candidates[nth].first : nullptr;
}

// The reputation criterion asks how the candidate regards the object the
// search is centred on. A reputation type outside the three leaves the
// running result as it is.
static bool matchesReputation(const Creature &candidate, const Object &searching,
                              int reputation, bool matched, const Game &game) {
    // A friend stands at 90 or more, an enemy at 10 or less.
    static constexpr int kFriendStanding = 90;
    static constexpr int kEnemyStanding = 10;
    switch (static_cast<ReputationType>(reputation)) {
    case ReputationType::Friend:
        return getObjectReputation(candidate, searching, game) >= kFriendStanding;
    case ReputationType::Enemy:
        return getObjectReputation(candidate, searching, game) <= kEnemyStanding;
    case ReputationType::Neutral: {
        const int standing = getObjectReputation(candidate, searching, game);
        return standing > kEnemyStanding && standing < kFriendStanding;
    }
    }
    return matched;
}

// A perception type outside the eight asks for nothing.
static bool matchesPerception(const Creature &candidate, const Creature &searching, int perception) {
    bool seen = searching.perception().sees(candidate.id());
    bool heard = searching.perception().hears(candidate.id());

    switch (static_cast<PerceptionType>(perception)) {
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
    return true;
}

// Whichever criterion asks for the race, the first criterion's value being
// racial type "all" matches every creature and "invalid" matches none.
static bool matchesRace(const Creature &candidate, int firstValue, int race) {
    if (firstValue == static_cast<int>(RacialType::All)) {
        return true;
    }
    if (firstValue == static_cast<int>(RacialType::Invalid)) {
        return false;
    }
    return static_cast<int>(candidate.racialType()) == race;
}

// A creature without a class answers to the invalid class.
static bool matchesClass(const Creature &candidate, int clazz) {
    const auto &classLevels = candidate.attributes().classLevels();
    if (classLevels.empty()) {
        return clazz == static_cast<int>(ClassType::Invalid);
    }
    return std::any_of(classLevels.begin(), classLevels.end(), [clazz](const auto &classLevel) {
        return static_cast<int>(classLevel.first->type()) == clazz;
    });
}

// Criteria are tested in order against a running result that starts false,
// and the first that fails rejects the creature. A criterion whose type or
// value is -1 is skipped. The alive criterion tests nothing and leaves the
// running result as it is, so it rejects every creature when it comes first.
// Reputation needs an object the search is centred on; perception without a
// searching creature leaves the running result as it is.
bool Area::matchesCriterias(const Creature &creature, const SearchCriteriaList &criterias,
                            std::optional<std::reference_wrapper<const Object>> searching) const {
    std::optional<std::reference_wrapper<const Creature>> searchingCreature;
    if (searching) {
        searchingCreature = Creature::asCreature(searching->get());
    }
    const int firstValue = criterias.empty() ? -1 : criterias.front().second;
    bool matched = false;

    for (const auto &[type, value] : criterias) {
        if (type == CreatureType::Invalid || value == -1) {
            continue;
        }
        switch (type) {
        case CreatureType::RacialType:
            matched = matchesRace(creature, firstValue, value);
            break;
        case CreatureType::PlayerChar:
            matched = creature.isPCValue() == value;
            break;
        case CreatureType::Class:
            matched = matchesClass(creature, value);
            break;
        case CreatureType::Reputation:
            matched = searching && matchesReputation(creature, searching->get(), value, matched, _game);
            break;
        case CreatureType::IsAlive:
            break;
        case CreatureType::HasSpellEffect:
            matched = creature.hasSpellEffect(value);
            break;
        case CreatureType::DoesNotHaveSpellEffect:
            matched = !creature.hasSpellEffect(value);
            break;
        case CreatureType::Perception:
            if (searchingCreature) {
                matched = matchesPerception(creature, searchingCreature->get(), value);
            }
            break;
        default:
            matched = false;
            break;
        }
        if (!matched) {
            return false;
        }
    }

    return matched;
}

void Area::updatePerception(float dt) {
    updatePerceptionPasses(dt, false);
}

// Each creature perceives on its own schedule. A creature outside the party
// checks the party every update; the controlled creature looks around every
// 0.2 s and everyone else every 4 s. A creature's first update does both.
//
// No script runs during a pass, as notices wait for its end, and creatures are
// destroyed only between updates, so the creature list holds still.
void Area::updatePerceptionPasses(float dt, bool all) {
    auto leader = _game.party().getLeader();
    const ObjectList &creatures = getObjectsByType(ObjectType::Creature);
    _perceptionPassActive = true;
    for (size_t i = 0; i < creatures.size(); ++i) {
        const auto observer = std::static_pointer_cast<Creature>(creatures[i]);
        if (!observer->isRuntimeLive() || !isObjectResident(*observer)) {
            continue;
        }
        const bool controlled = observer == leader;
        if (!controlled && observer->isDead()) {
            continue;
        }
        // A creature held by a time stop does not look around.
        if (!all && _game.isFrozenByTimeStop(*observer)) {
            continue;
        }
        const bool inParty = _game.party().isMember(*observer);
        const bool started = observer->perceptionStarted();
        observer->setPerceptionPassTime(observer->perceptionPassTime() + dt);
        const float interval = controlled ? kLeaderPerceptionPass : kPerceptionPass;
        bool fullPass = all || !started || observer->perceptionPassTime() >= interval;
        // A priority-0 creature waits out its throttle before a timed pass.
        if (fullPass && started && !all && observer->throttlesPerceptionPass()) {
            fullPass = false;
        }
        if (fullPass) {
            observer->setPerceptionPassTime(0.0f);
        }
        observer->setPerceptionStarted();
        updatePerceptionPass(observer, !started || !inParty, fullPass);
    }
    _perceptionPassActive = false;
    dispatchPerceptionNotices();
    updateClientPresence();
}

void Area::updatePerceptionPass(const std::shared_ptr<Creature> &observer, bool partyTargets, bool fullPass) {
    auto &party = _game.party();
    observer->resolvePerceptionRanges();
    const bool inParty = party.isMember(*observer);
    const float range = std::max(observer->spotRange(), observer->listenRange());
    auto inRange = [&](const Creature &target) {
        const glm::vec3 offset {target.position() - observer->position()};
        return glm::dot(offset, offset) <= range * range || (_game.isTSL() && target.forceAlwaysUpdate());
    };
    if (partyTargets) {
        for (const auto &member : party.members()) {
            const auto &target = member.creature;
            if (!target || target == observer) {
                continue;
            }
            if (observer->perception().has(target->id()) || inRange(*target)) {
                updatePerceptionPair(observer, target);
            }
        }
    }
    if (!fullPass) {
        return;
    }

    // Those already perceived, then any others in range. Outside the party,
    // party members are left to the party check.
    // Those already perceived are taken by descending ID.
    const auto &perception = observer->perception();
    std::vector<uint32_t> known = std::move(_perceivedIds);
    known.clear();
    for (const auto *entries : {&perception.seen, &perception.heard, &perception.invisible}) {
        for (const auto &entry : *entries) {
            known.push_back(entry.first);
        }
    }
    std::sort(known.begin(), known.end(), std::greater<uint32_t>());
    known.erase(std::unique(known.begin(), known.end()), known.end());
    for (uint32_t id : known) {
        auto target = _game.getObjectById<Creature>(id);
        if (!target || !target->isRuntimeLive()) {
            observer->forgetPerceived(id);
            continue;
        }
        if (!inParty && party.isMember(*target)) {
            continue;
        }
        updatePerceptionPair(observer, target);
    }
    _perceivedIds = std::move(known);
    const ObjectList &candidates = getObjectsByType(ObjectType::Creature);
    for (size_t i = 0; i < candidates.size(); ++i) {
        auto target = std::static_pointer_cast<Creature>(candidates[i]);
        if (target == observer || observer->perception().has(target->id())) {
            continue;
        }
        if (!inParty && party.isMember(*target)) {
            continue;
        }
        if (inRange(*target)) {
            updatePerceptionPair(observer, target);
        }
    }
}

// The notices wait for the next perception update.
void Area::perceiveNow(const std::shared_ptr<Creature> &observer) {
    if (!observer || !isObjectResident(*observer)) {
        return;
    }
    const bool active = _perceptionPassActive;
    _perceptionPassActive = true;
    updatePerceptionPass(observer, false, true);
    _perceptionPassActive = active;
    updateClientPresence();
}

void Area::refreshPerceptionFor(Creature &changed) {
    ObjectList &creatures = getObjectsByType(ObjectType::Creature);
    auto changedIt = std::find_if(
        creatures.begin(),
        creatures.end(),
        [&changed](const std::shared_ptr<Object> &object) {
            return object.get() == &changed;
        });
    if (changedIt == creatures.end()) {
        return;
    }

    auto changedCreature = std::static_pointer_cast<Creature>(*changedIt);
    std::vector<std::shared_ptr<Creature>> others;
    for (const auto &object : creatures) {
        if (object.get() != &changed) {
            others.push_back(std::static_pointer_cast<Creature>(object));
        }
    }
    const bool active = _perceptionPassActive;
    _perceptionPassActive = true;
    for (const auto &other : others) {
        updatePerceptionPair(other, changedCreature);
        updatePerceptionPair(changedCreature, other);
    }
    _perceptionPassActive = active;
    // Inside a perception pass the notices wait for its end.
    if (!active) {
        dispatchPerceptionNotices();
    }
    updateClientPresence();
}

bool Area::isEyeLineClear(const glm::vec3 &from, const glm::vec3 &to,
                          std::optional<std::reference_wrapper<const Object>> observer,
                          std::optional<std::reference_wrapper<const Object>> target,
                          bool pastSeeThroughDoor) const {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    // Whether the line met the observer or the target.
    auto isObserverOrTarget = [&](const scene::IUser *user) {
        return (target && user == &target->get()) || (observer && user == &observer->get());
    };
    Collision collision;
    if (!sceneGraph.testLineOfSight(from, to, collision)) {
        return true;
    }
    if (isObserverOrTarget(collision.user) || glm::distance2(from, to) < glm::distance2(from, collision.intersection)) {
        return true;
    }
    const auto *door = dynamic_cast<const Door *>(collision.user);
    if (!pastSeeThroughDoor || !door || door->blocksSight()) {
        return false;
    }
    // The line goes on from beyond the door, which it passes over; anything
    // else it then meets but the observer or the target blocks it.
    static constexpr float kPastStep = 0.01f;
    const glm::vec3 direction {glm::normalize(to - from)};
    glm::vec3 origin {collision.intersection};
    while (true) {
        origin += direction * kPastStep;
        if (glm::dot(to - origin, direction) <= 0.0f || !sceneGraph.testLineOfSight(origin, to, collision)) {
            return true;
        }
        if (collision.user == door) {
            origin = collision.intersection;
            continue;
        }
        return isObserverOrTarget(collision.user);
    }
}

// Sound is cast both ways between the eyes. A door or other object hit both
// ways is thin; otherwise the gap between the two hits is the thickness of
// what lies between. Where the area routes sound around walls, a way around
// costs 2 and no way around blocks the sound; elsewhere each whole metre of
// wall costs 5.
std::optional<int> Area::soundOcclusion(const Creature &listener, const Creature &source,
                                        const glm::vec3 &listenerEye, const glm::vec3 &sourceEye) const {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    Collision forward, backward;
    const bool forwardHit = sceneGraph.testLineOfSight(listenerEye, sourceEye, forward);
    const bool backwardHit = sceneGraph.testLineOfSight(sourceEye, listenerEye, backward);
    auto hitObject = [&](const Collision &collision) -> const Object * {
        auto *object = dynamic_cast<const Object *>(collision.user);
        return object == &listener || object == &source ? nullptr : object;
    };
    const Object *forwardObject = forwardHit ? hitObject(forward) : nullptr;
    const Object *backwardObject = backwardHit ? hitObject(backward) : nullptr;
    float thickness2 = 0.0f;
    if (forwardObject && forwardObject == backwardObject) {
        thickness2 = kSharedObstacleThickness2;
    } else if (forwardHit || backwardHit) {
        const glm::vec3 forwardPoint = forwardObject ? forwardObject->position()
                                       : forwardHit  ? forward.intersection
                                                     : sourceEye;
        const glm::vec3 backwardPoint = backwardObject ? backwardObject->position()
                                        : backwardHit  ? backward.intersection
                                                       : listenerEye;
        thickness2 = glm::distance2(forwardPoint, backwardPoint);
    }
    if (thickness2 <= 0.0f) {
        return 0;
    }
    if (_interior || _underground) {
        if (!pathExists(_pathfinder, listener.position(), source.position())) {
            return std::nullopt;
        }
        return -kSoundDetourPenalty;
    }
    return -kSoundWallPenaltyPerMetre * static_cast<int>(std::sqrt(thickness2));
}

void Area::notifyPerception(const std::shared_ptr<Creature> &observer, const std::shared_ptr<Object> &target,
                            PerceptionEvent event) {
    static const char *const kNames[] = {"heard by", "inaudible to", "seen by", "vanished from"};
    debug(str(boost::format("%s %s %s") % target->tag() % kNames[static_cast<int>(event)] % observer->tag()),
          LogChannel::Perception);
    _perceptionNotices.push_back({observer, target, event});
}

// Notices are posted, not run inside the pass that found them.
void Area::dispatchPerceptionNotices() {
    auto notices = std::move(_perceptionNotices);
    _perceptionNotices.clear();
    for (const auto &notice : notices) {
        if (!notice.observer->isRuntimeLive() || !notice.target->isRuntimeLive()) {
            continue;
        }
        notice.observer->runOnNotice(notice.target, notice.event);
    }
}

// Other creatures exist for the player only while the controlled creature
// sees or hears them.
void Area::updateClientPresence() {
    auto leader = _game.party().getLeader();
    if (!leader || !isObjectResident(*leader)) {
        return;
    }
    const auto &perception = leader->perception();
    for (const auto &object : getObjectsByType(ObjectType::Creature)) {
        const bool present = object == leader || _game.party().isMember(*object) ||
                             perception.sees(object->id()) || perception.hears(object->id()) ||
                             object->forceAlwaysUpdate();
        object->setClientPresent(present);
    }
}

// One creature's look at another. Sight is the controlled creature's always,
// and for others a clear line within range. Hearing is rolled while out of
// sight; sight only until seen, after which the creature stays seen while in
// sight. Party members always perceive each other.
void Area::updatePerceptionPair(
    const std::shared_ptr<Creature> &observer,
    const std::shared_ptr<Creature> &target) {

    if (!observer || !target || observer == target) {
        return;
    }
    auto leader = _game.party().getLeader();
    const bool controlled = observer == leader;
    if (!controlled && observer->isDead()) {
        return;
    }
    if (!observer->isRuntimeLive() || !target->isRuntimeLive() || !isObjectResident(*observer)) {
        return;
    }
    observer->resolvePerceptionRanges();
    observer->refreshPerceptionRolls();

    const bool sameArea = isObjectResident(*target);
    bool sight = false;
    bool invisible = false;
    if (sameArea) {
        if (controlled) {
            sight = true;
        } else {
            const glm::vec3 lift {0.0f, 0.0f, kPerceptionEyeHeight};
            const glm::vec3 eye {observer->position() + lift};
            const glm::vec3 targetEye {target->position() + lift};
            const float range = std::max(observer->spotRange(), observer->listenRange());
            if (glm::distance2(eye, targetEye) < range * range || (_game.isTSL() && target->forceAlwaysUpdate())) {
                sight = isEyeLineClear(eye, targetEye, *observer, *target, true);
            }
        }
        invisible = observer->isBlind() || target->isInvisibleTo(*observer);
    }

    const uint32_t id = target->id();
    const auto &perception = observer->perception();
    const bool present = perception.has(id);
    const bool wasSeen = perception.sees(id);
    const bool wasHeard = perception.hears(id);
    bool seen = false;
    bool heard = false;
    bool newly = false;
    bool lost = false;
    auto detect = [&](bool spot) {
        if (!sameArea) {
            return;
        }
        heard = observer->detectsBySound(*target, invisible);
        seen = spot && observer->detectsBySight(*target, invisible);
    };
    if (present) {
        if (perception.isInvisible(id) != invisible) {
            observer->setObjectInvisible(target, invisible);
            if (invisible) {
                observer->clearHostileActionsAgainst(*target);
            }
        }
        if (sight) {
            if (wasSeen) {
                seen = true;
                heard = wasHeard;
            } else {
                detect(true);
                newly = (seen && !wasSeen) || (heard && !wasHeard);
            }
            if (wasSeen && invisible) {
                seen = false;
                lost = true;
            }
            if (invisible && (wasSeen || wasHeard)) {
                lost = true;
            }
        } else {
            detect(false);
            newly = heard;
            lost = heard ? wasSeen : true;
        }
    } else {
        detect(sight);
        newly = seen || heard;
    }

    if (_game.party().isMember(*observer) && _game.party().isMember(*target)) {
        seen = true;
        heard = true;
        invisible = false;
        lost = false;
        newly = true;
    }
    observer->updateMindTrickPerception(*target, heard, seen);

    if (present && lost) {
        if (wasHeard && !heard) {
            notifyPerception(observer, target, PerceptionEvent::Inaudible);
        }
        if (wasSeen && !seen) {
            notifyPerception(observer, target, PerceptionEvent::Vanished);
        }
        observer->setObjectSeen(target, seen);
        observer->setObjectHeard(target, heard);
        if (!seen && !heard && !invisible) {
            observer->forgetPerceived(id);
        }
    }
    if (!newly) {
        return;
    }
    if (present) {
        if (seen && !observer->perception().sees(id)) {
            observer->setObjectSeen(target, true);
            notifyPerception(observer, target, PerceptionEvent::Seen);
        }
        if (heard && !observer->perception().hears(id)) {
            observer->setObjectHeard(target, true);
            notifyPerception(observer, target, PerceptionEvent::Heard);
        }
    } else if (seen || heard) {
        observer->setObjectSeen(target, seen);
        observer->setObjectHeard(target, heard);
        observer->setObjectInvisible(target, invisible);
        if (heard) {
            notifyPerception(observer, target, PerceptionEvent::Heard);
        }
        if (seen) {
            notifyPerception(observer, target, PerceptionEvent::Seen);
        }
    }
}

// How far each talk volume carries. Talk and silent shout carry the farthest.
static constexpr float kTalkRange = 1000.0f;
static constexpr float kWhisperRange = 3.0f;
static constexpr float kShoutRange = 250.0f;
static constexpr float kSilentTalkRange = 35.0f;
// Silent talk reaches a creature that does not see the speaker only within this distance.
static constexpr float kSilentTalkUnseenRange = 10.0f;
// The script event a listener hearing a matching pattern is sent.
static constexpr int kConversationEvent = 7;

void Area::broadcastDialog(Object &speaker, const std::string &message, int talkVolume) {
    // Any volume other than these is talk.
    float range = kTalkRange;
    bool silent = false;
    switch (static_cast<TalkVolume>(talkVolume)) {
    case TalkVolume::Whisper:
        // Only a creature can whisper; anything else reaches nobody.
        if (!isa<Creature>(&speaker)) {
            return;
        }
        range = kWhisperRange;
        break;
    case TalkVolume::Shout:
        range = kShoutRange;
        break;
    case TalkVolume::SilentTalk:
        range = kSilentTalkRange;
        silent = true;
        break;
    default:
        break;
    }
    const bool creatureSpeaker = isa<Creature>(&speaker);
    std::vector<std::string> pieces;
    for (const auto &object : _objects) {
        if (object.get() == &speaker) {
            continue;
        }
        const float squareDistance = speaker.getSquareDistanceTo(*object);
        if (squareDistance > range * range || !object->isListening()) {
            continue;
        }
        if (auto listener = dyn_cast<Creature>(object.get())) {
            if (creatureSpeaker) {
                // A creature hears another creature only as it perceives it;
                // silent talk also needs it seen or near.
                const auto &perception = listener->perception();
                if (!perception.hears(speaker.id())) {
                    continue;
                }
                if (silent && !perception.sees(speaker.id()) && squareDistance >= kSilentTalkUnseenRange * kSilentTalkUnseenRange) {
                    continue;
                }
            } else {
                // Anything else it hears within its hearing range.
                const float listenRange = listener->listenRange();
                if (squareDistance > listenRange * listenRange) {
                    continue;
                }
            }
        }
        const auto number = object->testListenExpressions(message, pieces);
        if (!number) {
            continue;
        }
        // The event carries the piece count, the pattern number and the
        // pieces after the whole string, whose place stays empty.
        std::vector<std::string> strings = pieces;
        strings[0].clear();
        _game.queueScriptEvent(*object, speaker,
                               Event(kConversationEvent, {static_cast<int32_t>(pieces.size()), *number}, {}, std::move(strings), {}));
    }
}

std::optional<std::reference_wrapper<Object>> Area::getObjectAt(int x, int y) const {
    auto partyLeader = _game.party().getLeader();
    if (!partyLeader) {
        return std::nullopt;
    }
    auto &scene = _services.scene.graphs.get(kSceneMain);
    auto model = scene.pickModelAt(x, y, partyLeader.get());
    if (!model) {
        return std::nullopt;
    }
    // A kept corpse is picked as its body bag.
    for (const auto &body : _corpses) {
        if (body.model.get() == model) {
            auto bodyBag = _game.getObjectById(body.bodyBagId);
            if (!bodyBag) {
                return std::nullopt;
            }
            return std::ref(*bodyBag);
        }
    }
    auto object = dynamic_cast<Object *>(model->user());
    if (!object) {
        return std::nullopt;
    }
    return std::ref(*object);
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
