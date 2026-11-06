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

#include "reone/game/object/door.h"

#include <algorithm>
#include <limits>

#include "reone/game/action/opendoor.h"
#include "reone/game/animations.h"
#include "reone/game/animationutil.h"
#include "reone/game/attack.h"
#include "reone/game/di/services.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/game/twodautil.h"
#include "reone/graphics/di/services.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/model.h"
#include "reone/scene/types.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static constexpr int kDamagedEvent = static_cast<int>(SavedSignalEventType::Damaged);
// The animations.2da row of a destroyed door's pose.
static constexpr int kDestroyedAnimation = 330;
// LinkedToFlags of a door linked to another door and to a waypoint.
static constexpr int kLinkedToDoor = 1;
static constexpr int kLinkedToWaypoint = 2;

void Door::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utd = _services.resource.gffs.get(templateRes, ResType::Utd)) {
            deserializeAll(*utd, SerializedIdentityContext::templateResource(templateRes));
            captureAuthoredRecord(*utd, gff);
        }
    }
    deserializeAll(gff, identityContext);
    loadAppearance();
    applyRestingState();
    updateTransform();
}

void Door::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    if (gff.readLocString(_locName, "LocName", _services.resource.strings)) {
        _name = _locName.str();
    }

    // Only the low byte of the appearance names the door's look.
    uint32_t appearance = _appearance;
    if (gff.readDword(appearance, "Appearance")) {
        _appearance = static_cast<uint8_t>(appearance);
    }
    gff.readByte(_genericType, "GenericType");
    {
        // OpenState names the resting state the door was authored in. Every K1
        // and K2 blueprint that sets it sets AnimationState to the same value,
        // so OpenState on its own is authoritative.
        uint8_t openState;
        if (gff.readByte(openState, "OpenState")) {
            switch (openState) {
            case 0:
                _state = DoorState::Closed;
                break;
            case 1:
                _state = DoorState::Opened1;
                break;
            case 3:
                _state = DoorState::Destroyed;
                break;
            case 2:
                _state = DoorState::Opened2;
                break;
            default:
                warn(str(boost::format("Door: unsupported OpenState %d, treating as closed") % static_cast<int>(openState)));
                _state = DoorState::Closed;
                break;
            }
        }
    }
    gff.readBool(_autoRemoveKey, "AutoRemoveKey");
    {
        float bearing;
        if (gff.readFloat(bearing, "Bearing")) {
            _orientation = glm::quat(glm::vec3(0.0f, 0.0f, bearing));
        }
    }
    gff.readFloat(_position[0], "X");
    gff.readFloat(_position[1], "Y");
    gff.readFloat(_position[2], "Z");
    gff.readEnum(_faction, "Faction");
    gff.readByte(_fort, "Fort");
    gff.readByte(_will, "Will");
    gff.readByte(_ref, "Ref");
    if (gff.readShort(_hitPoints, "HP")) {
        _maxHitPoints = _hitPoints;
    }
    gff.readShort(_currentHitPoints, "CurrentHP");
    // An Invulnerable flag takes precedence over Plot, an explicit zero
    // included.
    if (!gff.readBool(_plot, "Invulnerable")) {
        gff.readBool(_plot, "Plot");
    }
    gff.readBool(_minOneHP, "Min1HP");
    if (gff.readString(_keyName, "KeyName")) {
        boost::to_lower(_keyName);
    }
    gff.readBool(_keyRequired, "KeyRequired");
    gff.readByte(_openLockDC, "OpenLockDC");
    gff.readByte(_closeLockDC, "CloseLockDC");
    gff.readByte(_secretDoorDC, "SecretDoorDC");
    gff.readResRef(_conversation, "Conversation");
    gff.readWord(_portraitId, "PortraitId");
    gff.readByte(_hardness, "Hardness");
    gff.readResRef(_onClosed, "OnClosed");
    gff.readResRef(_onDamaged, "OnDamaged");
    gff.readResRef(_onDeath, "OnDeath");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onLock, "OnLock");
    gff.readResRef(_onMeleeAttacked, "OnMeleeAttacked");
    gff.readResRef(_onOpen, "OnOpen");
    gff.readResRef(_onSpellCastAt, "OnSpellCastAt");
    gff.readResRef(_onUnlock, "OnUnlock");
    gff.readResRef(_onUserDefined, "OnUserDefined");
    gff.readResRef(_onClick, "OnClick");
    gff.readResRef(_onFailToOpen, "OnFailToOpen");
    gff.readResRef(_onDialog, "OnDialog");
    _trap.deserialize(gff);
    // A saved door without a DC keeps the current one as shifted for difficulty.
    // Placed doors take their DCs from the blueprint alone.
    _trap.deserializeArming(gff, _game, identityContext.isSerializedState());
    gff.readBool(_locked, "Locked");
    gff.readBool(_lockable, "Lockable");
    gff.readByte(_linkedToFlags, "LinkedToFlags");
    gff.readString(_linkedTo, "LinkedTo");
    gff.readResRef(_linkedToModule, "LinkedToModule");
    gff.readWord(_loadScreenId, "LoadScreenID");
    gff.readLocString(_description, "Description", _services.resource.strings);
    gff.readBool(_static, "Static");
    if (_static) {
        _plot = true;
    }
    gff.readBool(_notBlastable, "NotBlastable");
    gff.readLocString(_transitionDestin, "TransitionDestin", _services.resource.strings);

    // FIXME: deserialize EffectList, ActionList
}

// The appearance picks the door's look. A door with one takes
// its model from doortypes and is never used precisely; one without takes both
// from its generic type in genericdoors. The walkmeshes follow the model.
void Door::loadAppearance() {
    std::string modelName;
    int soundType;
    if (_appearance != 0) {
        std::shared_ptr<TwoDA> types(_services.resource.twoDas.get("doortypes"));
        modelName = boost::to_lower_copy(types->getString(_appearance, "model"));
        _preciseUse = false;
        _blocksSight = types->getInt(_appearance, "blocksight", 1) != 0;
        soundType = types->getInt(_appearance, "soundapptype");
    } else {
        std::shared_ptr<TwoDA> doors(_services.resource.twoDas.get("genericdoors"));
        modelName = boost::to_lower_copy(doors->getString(_genericType, "modelname"));
        _preciseUse = doors->getInt(_genericType, "preciseuse", 0) != 0;
        _blocksSight = doors->getInt(_genericType, "blocksight", 1) != 0;
        soundType = doors->getInt(_genericType, "soundapptype");
    }
    _soundArmorType = boost::to_lower_copy(
        getRequiredTwoDA(_services.resource.twoDas, "placeableobjsnds")->getString(soundType, "armortype"));

    _linkedTransitionGeometry.clear();
    auto walkmeshClosed = _services.resource.walkmeshes.get(modelName + "0", ResType::Dwk);
    if (walkmeshClosed) {
        loadLinkedTransitionGeometry(*walkmeshClosed);
    }

    auto model = _services.resource.models.get(modelName);
    if (!model) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    auto modelSceneNode = sceneGraph.newModel(*model, ModelUsage::Door);
    modelSceneNode->setUser(*this);
    // Under Force Sight a door is drawn see-through.
    modelSceneNode->setForceSightStyle(scene::ModelSceneNode::ForceSightStyle::Translucent);
    // modelSceneNode->setDrawDistance(_game.options().graphics.drawDistance);
    _sceneNode = std::move(modelSceneNode);

    if (walkmeshClosed) {
        _walkmeshClosed = sceneGraph.newWalkmesh(*walkmeshClosed);
        _walkmeshClosed->setUser(*this);
    }

    auto walkmeshOpen1 = _services.resource.walkmeshes.get(modelName + "1", ResType::Dwk);
    if (walkmeshOpen1) {
        _walkmeshOpen1 = sceneGraph.newWalkmesh(*walkmeshOpen1);
        _walkmeshOpen1->setUser(*this);
        _walkmeshOpen1->setEnabled(false);
    }

    auto walkmeshOpen2 = _services.resource.walkmeshes.get(modelName + "2", ResType::Dwk);
    if (walkmeshOpen2) {
        _walkmeshOpen2 = sceneGraph.newWalkmesh(*walkmeshOpen2);
        _walkmeshOpen2->setUser(*this);
        _walkmeshOpen2->setEnabled(false);
    }

    // A door authored open must load directly into its opened pose, with the
    // matching walkmesh, rather than resting shut and playing an opening
    // transition it already finished before the module was entered.
    applyRestingState();
}

void Door::applyRestingState() {
    _transition = DoorTransition::None;
    _transitionSwing.clear();
    clearStateAnimation();
    _lastAnimation = stateAnimation(_state);
    _open = _state != DoorState::Closed;
    enableStateWalkmeshes();
    applyRestingPose();
}

void Door::enableStateWalkmeshes() {
    if (_walkmeshClosed) {
        _walkmeshClosed->setEnabled(_state == DoorState::Closed);
    }
    if (_walkmeshOpen1) {
        _walkmeshOpen1->setEnabled(_state == DoorState::Opened1);
    }
    if (_walkmeshOpen2) {
        _walkmeshOpen2->setEnabled(_state == DoorState::Opened2);
    }
}

void Door::applyRestingPose() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }
    // Every K1 and K2 door model carries these as zero-length pose animations.
    // They finish on the update they are played, and a finished non-fire-forget
    // channel is retained, so the pose holds.
    switch (_state) {
    case DoorState::Opened1:
        model->playAnimation("opened1");
        break;
    case DoorState::Opened2:
        model->playAnimation("opened2");
        break;
    case DoorState::Destroyed:
        model->playAnimation(_services.game.animations.getNameById(kDestroyedAnimation));
        break;
    case DoorState::Closed:
    default:
        model->playAnimation("closed");
        break;
    }
}

void Door::loadLinkedTransitionGeometry(const Walkmesh &walkmesh) {
    AABB bounds;
    for (const auto &face : walkmesh.faces) {
        for (const auto &vertex : face.indices) {
            bounds.expand(walkmesh.vertices[vertex]);
        }
    }
    if (bounds.isDegenerate() || bounds.min().x == bounds.max().x || bounds.min().y == bounds.max().y) {
        return;
    }

    float z = bounds.min().z;
    _linkedTransitionGeometry = {
        glm::vec3(bounds.min().x, bounds.min().y, z),
        glm::vec3(bounds.min().x, bounds.max().y, z),
        glm::vec3(bounds.max().x, bounds.max().y, z),
        glm::vec3(bounds.max().x, bounds.min().y, z)};
}

bool Door::isSelectable() const {
    // A door already swinging open is on its way out of reach even though it is
    // not open yet, and offering it again would only re-run OnOpen.
    return !_static && !_open && !isOpening();
}

void Door::damage(
    int amount,
    const std::shared_ptr<Object> &damager) {
    if (isDead() || _plot || _notBlastable) {
        return;
    }
    if (amount <= 0) {
        return;
    }

    setLastDamager(damager);
    uint32_t damagerId = getLastDamager();
    const int hitPointsBefore = currentHitPoints();
    if (amount == std::numeric_limits<int>::max()) {
        _currentHitPoints = isMinOneHP() ? 1 : 0;
    } else {
        int adjustedAmount = applyDamageToHitPoints(amount, hitPointsBefore);
        _game.floatingText().addDamage(
            *this, amount, adjustedAmount, damagerId);
    }

    _game.queueScriptEvent(*this, damager ? std::optional<std::reference_wrapper<const Object>>(*damager) : std::nullopt, Event(kDamagedEvent, {amount}, {}, {}, {}));

    if (!isDead()) {
        return;
    }

    _locked = false;
    if (damager) {
        open(*damager);
    } else {
        open();
    }
    onOpen(damagerId);
    runDeathScript();
}

void Door::open(std::optional<std::reference_wrapper<const Object>> opener) {
    // The front of a door is the side its local +Y axis points to.
    const glm::vec3 front {_orientation * glm::vec3(0.0f, 1.0f, 0.0f)};
    const bool inFront = opener && glm::dot(opener->get().position() - _position, front) > 0.0f;
    const DoorTransition opening = inFront ? DoorTransition::Opening1 : DoorTransition::Opening2;
    swingOpen(opening);
    if (auto linked = linkedDoor()) {
        linked->swingOpen(opening);
    }
}

void Door::close() {
    swingShut();
    if (auto linked = linkedDoor()) {
        linked->swingShut();
    }
}

void Door::onClosed(uint32_t closerId) {
    if (_onClosed.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onClosed,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastClosedBy, Variable::ofObject(closerId)}});
}

std::shared_ptr<Door> Door::linkedDoor() {
    if (_linkedToFlags != kLinkedToDoor && _linkedToFlags != kLinkedToWaypoint) {
        return nullptr;
    }
    if (auto linked = _linkedDoor.resolve()) {
        return linked;
    }
    auto linked = std::dynamic_pointer_cast<Door>(
        spatialArea()->get().getObjectByTag(boost::to_lower_copy(_linkedTo)));
    _linkedDoor = linked;
    return linked;
}

void Door::swingOpen(DoorTransition opening) {
    if (actionState() == transitionTarget(opening)) {
        // Already open, or swinging open, to that side. Restarting would
        // rewind the leaf and re-run whatever the caller does around the
        // transition. A door open to the other side swings over.
        return;
    }

    // A door that is swinging aside is still in the doorway. K2 will not
    // let the player through one until the opening animation has finished, so
    // the closed walkmesh stays enabled for the whole transition and only comes
    // off when the door has physically arrived at its opened pose.
    beginTransition(opening);
}

void Door::swingShut() {
    if (isClosing()) {
        return;
    }
    if (_state == DoorState::Closed && _transition == DoorTransition::None) {
        // Already shut and blocking. Closing again must not replay the
        // transition, which would briefly reopen the doorway to collision.
        return;
    }

    // The mirror image: a door that is swinging shut has not sealed the doorway
    // yet. Scripts routinely close a door behind somebody who is still walking
    // through it - K2 103PER closes TO102PER on the same beat that sends the
    // player through it, and DOR_PER03 takes 2.667 seconds to shut - so the
    // doorway stays passable until the door reaches its closed pose.
    beginTransition(DoorTransition::Closing);
}

// The swing comes from the loop shown and the last animation: a door opens
// with a swing only from its closed loop, and swings shut with closing1 only
// after opened1.
void Door::beginTransition(DoorTransition transition) {
    // _state is left alone. It names where the door still physically stands,
    // and everything that can obstruct or admit a creature is read off it, so
    // the door keeps the collision of the state it is leaving until it arrives.
    const int target = stateAnimation(transitionTarget(transition));
    const auto swingRow = stateLoopSwingRow(shownLoopAnimation(), _lastAnimation, target, true);
    _transition = transition;
    clearStateAnimation();
    _lastAnimation = target;
    _transitionSwing = swingRow ? _services.game.animations.getNameById(static_cast<uint32_t>(*swingRow)) : std::string();

    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (model && !_transitionSwing.empty()) {
        model->playAnimation(_transitionSwing);
    }
    if (isTransitionComplete()) {
        // No model, or no such animation on it: there is nothing to wait for,
        // so the door arrives immediately rather than being stranded in a
        // transition that can never complete.
        finishTransition();
    }
}

void Door::finishTransition() {
    const DoorState previous = _state;
    _state = transitionTarget(_transition);
    applyRestingState();
    if (_state == previous) {
        return;
    }
    if (auto area = spatialArea()) {
        std::optional<std::reference_wrapper<const Door>> shutDoor;
        if (_state == DoorState::Closed) {
            shutDoor = *this;
        }
        area->get().budgeCreatures(_position, collisionBounds(), shutDoor);
    }
}

void Door::update(float dt) {
    // A dead door runs no heartbeat.
    updateStampedHeartbeat(_onHeartbeat, !isDead());
    Object::update(dt);
    updateMineBlast(dt);

    if (_transition != DoorTransition::None && isTransitionComplete()) {
        finishTransition();
    }
    updateStateAnimation();
}

void Door::setStateAnimation(int id) {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    const int row = doorAnimationRow(id);
    if (!model || row < 0) {
        return;
    }
    auto &animations = _services.game.animations;
    const std::string clip = animations.getNameById(static_cast<uint32_t>(row));
    if (clip.empty() || !model->model().getAnimation(clip)) {
        return;
    }
    if (!animations.isLoopingById(static_cast<uint32_t>(row))) {
        if (_lastStateOneShot == id) {
            return;
        }
        _lastStateOneShot = id;
        _lastAnimation = id;
        _stateOneShot = clip;
        model->playAnimation(clip);
        return;
    }
    const int shown = shownLoopAnimation();
    if (id == shown) {
        return;
    }
    const int current = _lastAnimation;
    _lastAnimation = id;
    _lastStateOneShot.reset();
    _stateLoop = clip;
    _stateLoopAnimation = id;
    _stateOneShot.clear();
    if (auto swingRow = stateLoopSwingRow(shown, current, id, true)) {
        const std::string swing = animations.getNameById(static_cast<uint32_t>(*swingRow));
        if (!swing.empty() && model->model().getAnimation(swing)) {
            _stateOneShot = swing;
            model->playAnimation(swing);
            return;
        }
    }
    model->playAnimation(clip, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
}

float Door::animationLength(int id) const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    const int row = doorAnimationRow(id);
    if (!model || row < 0) {
        return 0.0f;
    }
    auto clip = model->model().getAnimation(_services.game.animations.getNameById(static_cast<uint32_t>(row)));
    return clip ? clip->length() : 0.0f;
}

bool Door::playsAnimationOnce(int id) const {
    const int row = doorAnimationRow(id);
    return row >= 0 && _services.game.animations.isFireForgetById(static_cast<uint32_t>(row));
}

void Door::clearStateAnimation() {
    _stateLoop.clear();
    _stateLoopAnimation.reset();
    _stateOneShot.clear();
    _lastStateOneShot.reset();
}

int Door::stateAnimation(DoorState state) {
    static constexpr int kClosedAnimation = 10022;
    static constexpr int kOpened1Animation = 10050;
    static constexpr int kOpened2Animation = 10051;
    static constexpr int kDeadAnimation = 10072;
    switch (state) {
    case DoorState::Opened1:
        return kOpened1Animation;
    case DoorState::Opened2:
        return kOpened2Animation;
    case DoorState::Destroyed:
        return kDeadAnimation;
    case DoorState::Closed:
    default:
        return kClosedAnimation;
    }
}

// A swinging door already shows the loop it swings to.
int Door::shownLoopAnimation() const {
    return _stateLoopAnimation ? *_stateLoopAnimation : stateAnimation(actionState());
}

void Door::updateStateAnimation() {
    if (_stateOneShot.empty()) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model || model->activeAnimationName() != _stateOneShot) {
        _stateOneShot.clear();
        return;
    }
    if (!model->isAnimationFinished()) {
        return;
    }
    _stateOneShot.clear();
    if (_stateLoop.empty()) {
        applyRestingPose();
        return;
    }
    model->playAnimation(_stateLoop, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
}

bool Door::isTransitionComplete() const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return true;
    }
    // Something other than this transition's swing owns the model - a
    // reversal, or a swing the door never had or does not play - so there is
    // nothing left to wait for.
    if (_transitionSwing.empty() || model->activeAnimationName() != _transitionSwing) {
        return true;
    }
    return model->isAnimationFinished();
}

DoorState Door::transitionTarget(DoorTransition transition) {
    switch (transition) {
    case DoorTransition::Opening1:
        return DoorState::Opened1;
    case DoorTransition::Opening2:
        return DoorState::Opened2;
    default:
        return DoorState::Closed;
    }
}

DoorState Door::actionState() const {
    return _transition == DoorTransition::None ? _state : transitionTarget(_transition);
}

std::optional<std::array<glm::vec3, 2>> Door::actionPoints(DoorState state) const {
    std::shared_ptr<WalkmeshSceneNode> node;
    switch (state) {
    case DoorState::Closed:
        node = _walkmeshClosed;
        break;
    case DoorState::Opened1:
        node = _walkmeshOpen1;
        break;
    case DoorState::Opened2:
        node = _walkmeshOpen2;
        break;
    default:
        return std::nullopt;
    }
    // Without a walkmesh the points lie at the door's own origin.
    std::array<glm::vec3, 2> points {_position, _position};
    if (!node) {
        return points;
    }
    const Walkmesh &walkmesh = node->walkmesh();
    for (size_t i = 0; i < points.size(); ++i) {
        // A point placed absolutely stands in for the relative one.
        const glm::vec3 &absolute = walkmesh.absoluteUsePositions[i];
        const glm::vec3 &local = absolute != glm::vec3(0.0f) ? absolute : walkmesh.relativeUsePositions[i];
        points[i] = glm::vec3(_transform * glm::vec4(local + walkmesh.position, 1.0f));
    }
    return points;
}

// A door with no use point over walkable ground is used from the world
// origin.
glm::vec3 Door::nearestActionPoint(const glm::vec3 &from, bool closed) const {
    // A point beyond this squared distance does not compete with a closed
    // state point when the door's own state offers none.
    static constexpr float kNoPointDistance2 = 1000.0f;

    const Area &area = spatialArea()->get();
    auto walkable = [&area](const glm::vec3 &point) {
        return area.isPositionWalkable(point);
    };
    auto distance2 = [&](const glm::vec3 &point) {
        const glm::vec3 offset {from - point};
        return glm::dot(offset, offset);
    };

    const DoorState state = closed ? DoorState::Closed : actionState();
    glm::vec3 best {0.0f};
    float bestDistance2 = kNoPointDistance2;
    if (auto points = actionPoints(state)) {
        const auto &[first, second] = *points;
        const float firstDistance2 = distance2(first);
        const float secondDistance2 = distance2(second);
        if (secondDistance2 > firstDistance2) {
            if (walkable(first)) {
                best = first;
                bestDistance2 = firstDistance2;
            } else if (walkable(second)) {
                best = second;
                bestDistance2 = secondDistance2;
            }
        } else if (walkable(second)) {
            best = second;
            bestDistance2 = secondDistance2;
        }
    }
    if (state != DoorState::Closed) {
        const auto closedPoints = actionPoints(DoorState::Closed);
        for (const glm::vec3 &point : *closedPoints) {
            if (bestDistance2 > distance2(point) && walkable(point)) {
                return point;
            }
        }
    }
    return best;
}

void Door::onOpen(uint32_t triggererId) {
    if (_onOpen.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastOpenedBy, Variable::ofObject(triggererId)},
         {script::ArgKind::ClickingObject, Variable::ofObject(triggererId)}});
}

void Door::receiveClick(const std::shared_ptr<Object> &clicker) {
    _lastEnteredId = clicker ? clicker->id() : kObjectInvalid;
    if (_linkedToFlags == kLinkedToDoor || _linkedToFlags == kLinkedToWaypoint) {
        auto creature = dyn_cast<Creature>(clicker);
        const Party &party = _game.party();
        if (creature && !_linkedToModule.empty() && party.movesBetweenModules(*creature) && !creature->isDead() &&
            !(party.isMember(*creature) && creature->currentHitPoints() <= 0)) {
            _game.scheduleModuleTransition(_linkedToModule, _linkedTo);
        }
        return;
    }
    if (_onClick.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onClick,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::ClickingObject, Variable::ofObject(_lastEnteredId)}});
}

void Door::onLocked() {
    if (_onLock.empty()) {
        return;
    }
    _game.scriptRunner().run(_onLock, {{script::ArgKind::Caller, Variable::ofObject(_id)}});
}

void Door::onFailToOpen(uint32_t openerId, bool quiet) {
    _lastEnteredId = openerId;
    if (!_onFailToOpen.empty()) {
        _game.scriptRunner().run(
            _onFailToOpen,
            {{script::ArgKind::Caller, Variable::ofObject(_id)},
             {script::ArgKind::ClickingObject, Variable::ofObject(openerId)}});
    }
    if (!_locked || quiet) {
        return;
    }
    // A door with an order to open itself says nothing.
    for (const auto &action : actions()) {
        auto *opening = dyn_cast<OpenDoorAction>(action.get());
        if (opening && opening->door().get() == this) {
            return;
        }
    }
    // Otherwise the creature the player controls hears the door is locked.
    static constexpr int kLockedStrRef = 1437;
    auto leader = _game.party().getLeader();
    if (leader && leader->id() == openerId) {
        _game.addFeedbackMessage(kLockedStrRef);
    }
}

void Door::runConversationScript() {
    // Only a door whose conversation script is "default" takes the fallback
    // one, which stays its own; a door with none runs nothing.
    if (_onDialog == "default") {
        _onDialog = kFallbackConversationScript;
    }
    _game.scriptRunner().run(_onDialog, _id);
}

void Door::runDamagedScript() {
    if (_onDamaged.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

void Door::runDeathScript() {
    if (_onDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDeath,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

void Door::enterDestroyedState() {
    _state = DoorState::Destroyed;
    applyRestingState();
}

void Door::receiveDamagedSignal(const std::shared_ptr<Object> &damager) {
    setLastDamager(damager);
    setLastHostileActor(damager ? damager->id() : script::kObjectInvalid);
    runDamagedScript();
}

void Door::receiveAttackEvent(uint32_t attackerId, std::optional<std::reference_wrapper<const AttackEventFields>> fields) {
    recordReceivedAttack(attackerId, fields ? fields->get().weaponAttackType : 0);
    if (!isRuntimeLive()) {
        return;
    }
    // The melee-attacked script runs whether or not the object is destroyed.
    _game.scriptRunner().run(_onMeleeAttacked, id());
    _trap.springOnStrike(*this, _faction, attackerId);
}

void Door::receiveDeathSignal(uint32_t killerId) {
    setLastKiller(killerId);
    _game.scriptRunner().run(_onDeath, {{script::ArgKind::Caller, Variable::ofObject(_id)}});
}

void Door::setLocked(bool locked) {
    _locked = locked;
}

bool canBashDoor(const Door &door) {
    return door.isLocked() &&
           door.isSelectable() &&
           !door.isDead() &&
           !door.plotFlag() &&
           !door.isNotBlastable();
}

graphics::AABB Door::collisionBounds() const {
    graphics::AABB bounds;
    for (const auto &node : {_walkmeshClosed, _walkmeshOpen1, _walkmeshOpen2}) {
        if (!node) {
            continue;
        }
        const glm::mat4 &transform = node->absoluteTransform();
        for (const auto &vertex : node->walkmesh().vertices) {
            bounds.expand(glm::vec3(transform * glm::vec4(vertex, 1.0f)));
        }
    }
    return bounds;
}

void Door::updateTransform() {
    Object::updateTransform();

    if (_walkmeshOpen1) {
        _walkmeshOpen1->setLocalTransform(_transform);
    }
    if (_walkmeshOpen2) {
        _walkmeshOpen2->setLocalTransform(_transform);
    }
    if (_walkmeshClosed) {
        _walkmeshClosed->setLocalTransform(_transform);
    }
}

void Door::applyDamageEffect(
    int amount,
    const std::shared_ptr<Object> &damager,
    std::optional<DamageReaction>) {
    if (amount == 0) {
        _game.floatingText().addDamage(*this, 0, 0, getLastDamager());
        _game.queueScriptEvent(*this, damager ? std::optional<std::reference_wrapper<const Object>>(*damager) : std::nullopt, Event(kDamagedEvent, {amount}, {}, {}, {}));
        return;
    }

    damage(amount, damager);
}

int Door::savingThrow(SavingThrow save) const {
    switch (save) {
    case SavingThrow::Fortitude:
        return _fort;
    case SavingThrow::Reflex:
        return _ref;
    case SavingThrow::Will:
        return _will;
    default:
        return 0;
    }
}

// Traps

// A door whose lock a mine blew swings open this long after the blast.
static constexpr float kMineOpenDelay = 1.0f;
// The setter's Demolitions rank counts at half in a mine's blast at a door.
static constexpr int kMineBlastRankDivisor = 2;

void Door::triggerTrap(const std::shared_ptr<Creature> &caller, bool force) {
    if (caller) {
        _trap.spring(*this, _faction, *caller, force);
    }
}

void Door::armMine(int trapType, int detectDC, int disarmDC, int ownerDemolitionsSkill,
                   const std::shared_ptr<Trigger> &linkedMine,
                   const std::shared_ptr<Creature> &setter, int blastBonus, bool blast) {
    _trap.armMine(_game, trapType, detectDC, disarmDC, ownerDemolitionsSkill,
                  linkedMine, setter, blastBonus, blast);
}

// A blast that breaks the lock unlocks the door, which swings open a moment
// later.
void Door::updateMineBlast(float dt) {
    if (_mineOpenDelay) {
        *_mineOpenDelay -= dt;
        if (*_mineOpenDelay < 0.0f) {
            _mineOpenDelay.reset();
            _locked = false;
            open();
        }
    }
    auto blast = _trap.updateMineBlast(dt, kMineBlastRankDivisor, _openLockDC);
    if (!blast) {
        return;
    }
    if (blast->opens) {
        _mineOpenDelay = kMineOpenDelay;
        _game.scriptRunner().run(_onUnlock, {{script::ArgKind::Caller, script::Variable::ofObject(_id)}});
    }
    _trap.reportMineBlast(_game, *blast);
}

// END Traps

} // namespace game

} // namespace reone
