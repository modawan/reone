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

#include "reone/game/object/trigger.h"

#include "reone/game/animations.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/messagelog.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/trigger.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

// The ranges.2da row of a trap's radius.
static constexpr int kTrapRadiusRange = 17;
// The reach of a trap when ranges.2da gives none.
static constexpr float kDefaultTrapRadius = 1.0f;
// The animations.2da rows of a trap model hidden and shown.
static constexpr int kTrapHiddenAnimation = 347;
static constexpr int kTrapShownAnimation = 350;
// A mine goes off on a creature that regards its setter at this standing or below.
static constexpr int kMineTriggerStanding = 10;
static constexpr float kMineHalfSize = 2.0f;

static constexpr float kDebugTestDuration = 0.25f;
static constexpr float kDebugInsideDuration = 0.25f;
static constexpr float kDebugEnterDuration = 1.5f;

static glm::vec4 debugColorForState(Trigger::DebugState state) {
    switch (state) {
    case Trigger::DebugState::Entered:
        return glm::vec4(1.0f, 0.42f, 0.12f, 0.95f);
    case Trigger::DebugState::Inside:
        return glm::vec4(0.16f, 0.95f, 0.38f, 0.95f);
    case Trigger::DebugState::Tested:
        return glm::vec4(1.0f, 0.88f, 0.18f, 0.95f);
    default:
        return glm::vec4(0.48f, 0.74f, 1.0f, 0.85f);
    }
}

void Trigger::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> utt(_services.resource.gffs.get(resRef, ResType::Utt));
    if (!utt) {
        return;
    }
    deserialize(*utt, SerializedIdentityContext::templateResource(resRef));
}

void Trigger::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utt = _services.resource.gffs.get(templateRes, ResType::Utt)) {
            deserializeAll(*utt, SerializedIdentityContext::templateResource(templateRes));
            captureAuthoredRecord(*utt, gff);
        }
    }
    deserializeAll(gff, identityContext);
    loadAppearance();
    updateTransform();
}

void Trigger::configureLinkedDoorTransition(const std::shared_ptr<Door> &door) {
    _linkedDoor = door;
    _linkedDoorTransition = true;
    _linkedToModule = door->linkedToModule();
    _linkedTo = door->linkedTo();
    _linkedToFlags = door->linkedToFlags();
    _transitionDestin = door->transitionDestination();
    _name = _transitionDestin.str();
    _geometry = door->linkedTransitionGeometry();

    setPosition(door->position());
    setFacing(door->getFacing());
    loadAppearance();
    updateTransform();
}

void Trigger::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
    gff.readResRef(_onHeartbeat, "ScriptHeartbeat");
    gff.readResRef(_onEnter, "ScriptOnEnter");
    gff.readResRef(_onExit, "ScriptOnExit");
    gff.readResRef(_onClick, "OnClick");
    gff.readResRef(_onUserDefined, "ScriptUserDefine");
    _trap.deserialize(gff);
    if (gff.readString(_linkedTo, "LinkedTo")) {
        boost::to_lower(_linkedTo);
    }
    gff.readByte(_linkedToFlags, "LinkedToFlags");
    gff.readResRef(_linkedToModule, "LinkedToModule");
    gff.readBool(_autoRemoveKey, "AutoRemoveKey");
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    if (gff.readLocString(_locName, "LocalizedName", _services.resource.strings)) {
        _name = _locName.str();
    }
    gff.readEnum(_faction, "Faction");
    gff.readString(_keyName, "KeyName");
    // The detect and disarm DCs come from traps.2da, not from the record.
    _trap.loadTypeDCs(_game);
    // An empty or "default" trap script means the trap type's own script.
    static const std::string kDefaultTrapScript {"default"};
    if (_trap.triggeredScript().empty() || _trap.triggeredScript() == kDefaultTrapScript) {
        _trap.setTriggeredScript(_game.trapType(_trap.type()).script);
    }
    uint32_t creatorId = 0;
    if (gff.readDword(creatorId, "CreatorId")) {
        _creator = SavedObjectReference::fromSerializedId(creatorId, identityContext);
    }
    int32_t triggerType = 0;
    if (gff.readInt(triggerType, "Type")) {
        _triggerType = triggerType;
        // A trap stays a trap even if a later record gives another type.
        if (triggerType == kTrapType) {
            _isTrap = true;
        }
    }
    gff.readFloat(_highlightHeight, "HighlightHeight");

    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");

    // Ignore XOrientation and YOrientation.
    //
    // The game applies orientation only when instantiating
    // from a template, and it does not apply orientation when loading
    // from a savegame. It also uses script Facing only to the Trigger
    // object and not to the corresponding mesh.
    //
    // It appears that no script in the game relies on this behavior,
    // therefore we do not support it.

    gff.readWord(_loadScreenId, "LoadScreenID");
    gff.readLocString(_transitionDestin, "TransitionDestin", _services.resource.strings);
    gff.readBool(_setByPlayerParty, "SetByPlayerParty");
    gff.readBool(_commandable, "Commandable");

    auto geometry = gff.getList("Geometry");
    if (!geometry.empty()) {
        _geometry.clear();
        for (auto &point : geometry) {
            float x = point->getFloat("PointX");
            float y = point->getFloat("PointY");
            float z = point->getFloat("PointZ");
            _geometry.push_back(glm::vec3(x, y, z));
        }
    }

    // Not handled:
    // - Cursor
    // - PortraitId
    // - VarTable
    // - SWVarTable
}

void Trigger::loadAppearance() {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    _sceneNode = sceneGraph.newTrigger(_geometry);
    if (!_sceneNode) {
        return;
    }
    _sceneNode->setLocalTransform(glm::translate(_position));
    syncDebugVisual();
    loadTrapModel();
}

void Trigger::loadTrapModel() {
    if (!_isTrap || _trapModel) {
        return;
    }
    // Every trap type names its model.
    const std::string &modelName = _game.trapType(_trap.type()).model;
    auto model = _services.resource.models.get(modelName);
    if (!model) {
        throw ResourceNotFoundException("Trap model not found: " + modelName);
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    _trapModel = sceneGraph.newModel(*model, ModelUsage::Placeable);
    _trapModel->setLocalTransform(glm::translate(_position));
    _trapModel->setDrawDistance(_game.options().graphics.drawDistance);
    _trapShown = false;
    _trapModel->playAnimation(
        _services.game.animations.getNameById(kTrapHiddenAnimation),
        nullptr,
        AnimationProperties::fromFlags(AnimationFlags::loop));
}

glm::vec3 Trigger::nearestPoint(const glm::vec3 &from) const {
    if (_isTrap) {
        const glm::vec3 offset {from - _position};
        if (glm::dot(offset, offset) <= 1e-4f) {
            return from;
        }
        return _position + glm::normalize(offset) * trapRadius();
    }
    // Nearest point on the outline, edge by edge in vertex order.
    glm::vec3 nearest {0.0f};
    float nearestDistance2 = 1e8f;
    const size_t count = _geometry.size();
    for (size_t i = 0; i < count; ++i) {
        const glm::vec3 start {_position + _geometry[i]};
        const glm::vec3 edge {_position + _geometry[(i + 1) % count] - start};
        const float t = glm::dot(from - start, edge) / glm::dot(edge, edge);
        glm::vec3 point {start};
        if (t > 1.0f) {
            point += edge;
        } else if (t >= 0.0f) {
            point += edge * t;
        }
        const glm::vec3 delta {from - point};
        const float distance2 = glm::dot(delta, delta);
        if (distance2 < nearestDistance2) {
            nearestDistance2 = distance2;
            nearest = point;
        }
    }
    return nearest;
}

glm::vec3 Trigger::getSelectablePosition() const {
    return _trapModel ? _trapModel->getWorldCenterOfAABB() : _position;
}

std::shared_ptr<Creature> Trigger::trapCreator() const {
    return std::dynamic_pointer_cast<Creature>(_creator.boundObject());
}

uint32_t Trigger::trapCreatorId() const {
    if (_creator.isInvalid()) {
        return script::kObjectInvalid;
    }
    if (auto creator = _creator.boundObject()) {
        return creator->id();
    }
    return _creator.isSerializedIdentity() ? script::kObjectInvalid : _creator.id;
}

void Trigger::setTrapCreator(const std::shared_ptr<Object> &creator) {
    _creator = creator ? SavedObjectReference::fromRuntimeId(creator->id()) : SavedObjectReference {};
    if (creator) {
        _game.bindSavedObjectReference(_creator);
    }
    auto *creature = creator ? dyn_cast<Creature>(creator.get()) : nullptr;
    _setByPlayerParty = creature && (_game.party().player().get() == creature || _game.party().isMember(*creature));
}

void Trigger::initMine(
    const Area &area,
    int trapType,
    const glm::vec3 &position,
    const std::shared_ptr<Object> &creator,
    Faction faction,
    int detectDC,
    int disarmDC,
    int ownerDemolitionsSkill) {
    setTrapCreator(creator);
    _isTrap = true;
    // Saved as a trap trigger, so a reload keeps the mine.
    _triggerType = kTrapType;
    _trap.setUpMine(_game, trapType, detectDC, disarmDC, ownerDemolitionsSkill);
    _faction = faction;
    if (auto nameStrRef = _game.trapType(trapType).nameStrRef) {
        _locName = LocString(*nameStrRef, "", _services.resource.strings);
        _name = _locName.str();
    }
    // A square four metres across, each corner on the ground beneath it, or
    // level with the mine where there is no room or the ground is at height
    // zero.
    _position = position;
    auto corner = [&](float x, float y) {
        glm::vec3 point {x, y, 0.0f};
        const float ground = area.groundHeight(glm::vec3(position.x + x, position.y + y, 0.0f), Area::GroundFace::Locating);
        if (ground != 0.0f) {
            point.z = ground - position.z;
        }
        return point;
    };
    _geometry = {
        corner(-kMineHalfSize, kMineHalfSize),
        corner(-kMineHalfSize, -kMineHalfSize),
        corner(kMineHalfSize, -kMineHalfSize),
        corner(kMineHalfSize, kMineHalfSize)};
    loadAppearance();
    updateTransform();
}

bool Trigger::canFireMineOn(const Creature &creature, bool force) const {
    bool fire = true;
    if (!force) {
        // How the creature regards the mine's creator, or the mine itself
        // without one.
        auto creator = trapCreator();
        const Object &setter = creator ? static_cast<const Object &>(*creator) : *this;
        fire = getObjectReputation(creature, setter, _game) <= kMineTriggerStanding &&
               _faction != creature.faction();
    }
    if (creature.hasEffectImmunity(ImmunityType::Trap)) {
        fire = false;
    }
    if (_game.isTSL()) {
        if (creature.hasEffectiveFeat(FeatType::MineImmunity)) {
            fire = false;
        }
        // A party walks over mines when its first character knows how.
        auto first = _game.party().getLeader();
        if (fire && _game.party().isMember(creature) && first && first->hasEffectiveFeat(FeatType::PartyMineImmunity)) {
            fire = false;
        }
    }
    return fire;
}

void Trigger::enterMine(const std::shared_ptr<Creature> &creature, bool force) {
    _lastEnteredId = creature->id();
    fireMine(force);
}

void Trigger::fireMine(bool force) {
    auto creature = _game.getObjectById<Creature>(_lastEnteredId);
    if (!creature || !canFireMineOn(*creature, force)) {
        return;
    }
    auto area = _game.module()->area();

    // Carrying the trap's key disarms it instead. A party member's key may be
    // anywhere in the party's inventory.
    const std::string keyTag = boost::to_lower_copy(_keyName);
    auto repository = _game.party().sharedInventoryReceiver(creature);
    std::shared_ptr<Item> key = repository->getItemByTag(keyTag);
    bool equippedKey = false;
    if (!key) {
        for (const auto &[slot, item] : creature->equipment()) {
            if (item && item->tag() == keyTag) {
                key = item;
                equippedKey = true;
                break;
            }
        }
    }
    if (key) {
        if (_autoRemoveKey) {
            if (equippedKey) {
                creature->takeEquippedItem(key);
                _game.destroyRuntimeObjectGraph(key);
            } else {
                bool last = false;
                repository->removeItem(key, last);
                if (last) {
                    _game.destroyRuntimeObjectGraph(key);
                }
            }
        }
        disarmTrap(*creature);
        area->destroyObject(*this);
        return;
    }

    if (_game.party().getLeader() == creature) {
        Trap::reportTriggered(_game);
    }
    _game.scriptRunner().run(_trap.triggeredScript(), _id);
    _game.scriptRunner().run(_onEnter, _id);
    if (_trap.isOneShot()) {
        area->destroyObject(*this);
    }
}

void Trigger::updateTrapPresentation() {
    if (!_isTrap || !_trapModel) {
        return;
    }
    auto viewer = _game.party().getLeader();
    const auto &detection = _trap.detection();
    const bool shown = detection.flagged ||
                       (viewer && (!isTrapHostileTo(*viewer) || detection.isDetectedBy(viewer->id())));
    if (shown == _trapShown) {
        return;
    }
    _trapShown = shown;
    _trapModel->playAnimation(
        _services.game.animations.getNameById(shown ? kTrapShownAnimation : kTrapHiddenAnimation),
        nullptr,
        AnimationProperties::fromFlags(AnimationFlags::loop));
}

void Trigger::resolveSavedReferences(
    const std::function<std::shared_ptr<Object>(uint32_t)> &resolver) {
    Object::resolveSavedReferences(resolver);
    // The recorded creator binds with the rest of the loaded references.
    if (!_creator.isInvalid()) {
        _game.bindSavedObjectReference(_creator);
    }
}

void Trigger::update(float dt) {
    // A trigger runs every due heartbeat, the first check included.
    if (isHeartbeatDueSince(_heartbeatDay, _heartbeatTime)) {
        stampHeartbeat();
        if (!_onHeartbeat.empty()) {
            _game.scriptRunner().run(_onHeartbeat, _id);
        }
    }
    Object::update(dt);
    if (_isTrap && !_creator.isInvalid() && !_creator.boundObject()) {
        _game.bindSavedObjectReference(_creator);
    }
    updateTrapPresentation();

    _debugTestAge = glm::max(0.0f, _debugTestAge - dt);
    _debugInsideAge = glm::max(0.0f, _debugInsideAge - dt);
    _debugEnterAge = glm::max(0.0f, _debugEnterAge - dt);

    if (!isActive()) {
        _tenants.clear();
        syncDebugVisual();
        return;
    }

    std::set<std::shared_ptr<Object>> tenantsToRemove;
    for (auto &tenant : _tenants) {
        if (tenant && isIn(tenant->position())) {
            continue;
        }
        tenantsToRemove.insert(tenant);
    }
    for (auto &tenant : tenantsToRemove) {
        _tenants.erase(tenant);
        _lastLeftId = tenant->id();

        if (_onExit.empty()) {
            continue;
        }

        _game.scriptRunner().run(_onExit, _id);
    }

    syncDebugVisual();
}

void Trigger::addTenant(const std::shared_ptr<Object> &object) {
    _tenants.insert(object);
    _lastEnteredId = object->id();
    syncDebugVisual();
    // Stepping on a trap goes through the trap, which runs the enter script itself.
    if (_isTrap) {
        fireMine(false);
        return;
    }
    if (_onEnter.empty()) {
        return;
    }

    _game.scriptRunner().run(_onEnter, _id);
}

// The click script an area transition takes when it has none of its own, and
// the name that stands for none.
static constexpr char kTransitionClickScript[] = "nw_g0_transition";
static constexpr char kDefaultClickScript[] = "default";

void Trigger::receiveClick(const std::shared_ptr<Object> &clicker) {
    _lastEnteredId = clicker ? clicker->id() : script::kObjectInvalid;
    if ((_onClick.empty() || _onClick == kDefaultClickScript) && isAreaTransition()) {
        _onClick = kTransitionClickScript;
    }
    if (_onClick.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onClick,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::ClickingObject, script::Variable::ofObject(_lastEnteredId)}});
}

void Trigger::removeTenant(const Object *object) {
    for (auto it = _tenants.begin(); it != _tenants.end();) {
        if (it->get() == object) {
            it = _tenants.erase(it);
            syncDebugVisual();
        } else {
            ++it;
        }
    }
}

bool Trigger::isIn(const glm::vec3 &point) const {
    if (_isTrap) {
        const glm::vec3 offset {point - _position};
        const float radius = trapRadius();
        return glm::dot(offset, offset) <= radius * radius;
    }
    auto sceneNode = std::static_pointer_cast<TriggerSceneNode>(_sceneNode);
    return sceneNode && sceneNode->isIn(glm::vec2(point));
}

float Trigger::trapRadius() const {
    const auto row = _services.game.spells.findRange(kTrapRadiusRange);
    return row && row->get().primary ? *row->get().primary : kDefaultTrapRadius;
}

bool Trigger::isTenant(const std::shared_ptr<Object> &object) const {
    auto maybeTenant = find(_tenants.begin(), _tenants.end(), object);
    return maybeTenant != _tenants.end();
}

bool Trigger::isActive() const {
    if (!_linkedDoorTransition) {
        return true;
    }
    auto door = _linkedDoor.resolve();
    return door && door->isOpen();
}

bool Trigger::acceptsTransitionActivator(const std::shared_ptr<Object> &activator) const {
    if (_linkedToModule.empty() || !isActive()) {
        return false;
    }
    if (!activator) {
        return false;
    }
    // A following companion crossing a transition is ignored. Taking control
    // of a companion makes it the leader, so it keeps the ability to
    // transition and stays controlled in the destination.
    return _game.party().movesBetweenModules(*activator);
}

bool Trigger::detachLinkedDoorTransition(const Door &door) {
    auto linkedDoor = _linkedDoor.resolve();
    if (!_linkedDoorTransition || linkedDoor.get() != &door) {
        return false;
    }

    _linkedDoor.reset();
    _linkedToModule.clear();
    _linkedTo.clear();
    _tenants.clear();
    syncDebugVisual();
    return true;
}

Trigger::DebugState Trigger::debugState() const {
    if (_debugEnterAge > 0.0f) {
        return DebugState::Entered;
    }
    if (!_tenants.empty() || _debugInsideAge > 0.0f) {
        return DebugState::Inside;
    }
    if (_debugTestAge > 0.0f) {
        return DebugState::Tested;
    }
    return DebugState::Default;
}

glm::vec4 Trigger::debugColor() const {
    return debugColorForState(debugState());
}

void Trigger::markDebugTested(bool inside) {
    _debugTestAge = kDebugTestDuration;
    if (inside) {
        _debugInsideAge = kDebugInsideDuration;
    }
    syncDebugVisual();
}

void Trigger::markDebugEntered() {
    _debugEnterAge = kDebugEnterDuration;
    syncDebugVisual();
}

void Trigger::syncDebugVisual() {
    if (!_sceneNode) {
        return;
    }
    static_cast<TriggerSceneNode *>(_sceneNode.get())->setDebugColor(debugColor());
}

} // namespace game

} // namespace reone
