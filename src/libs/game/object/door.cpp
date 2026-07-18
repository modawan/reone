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

#include "reone/game/di/services.h"
#include "reone/game/game.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

void Door::deserialize(const resource::Gff &gff) {
    std::string templateRes;
    if (gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utd = _services.resource.gffs.get(templateRes, ResType::Utd)) {
            deserializeAll(*utd);
        }
    }
    deserializeAll(gff);
    loadAppearance();
    updateTransform();
}

void Door::deserializeAll(const resource::Gff &gff) {
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    if (gff.readLocString(_locName, "LocName", _services.resource.strings)) {
        _name = _locName.str();
    }

    gff.readDword(_appearance, "Appearance");
    gff.readByte(_genericType, "GenericType");
    gff.readBool(_isOpen, "OpenState");
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
    gff.readBool(_plot, "Plot");
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
    gff.readResRef(_onDisarm, "OnDisarm");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onLock, "OnLock");
    gff.readResRef(_onMeleeAttacked, "OnMeleeAttacked");
    gff.readResRef(_onOpen, "OnOpen");
    gff.readResRef(_onSpellCastAt, "OnSpellCastAt");
    gff.readResRef(_onTrapTriggered, "OnTrapTriggered");
    gff.readResRef(_onUnlock, "OnUnlock");
    gff.readResRef(_onUserDefined, "OnUserDefined");
    gff.readResRef(_onClick, "OnClick");
    gff.readResRef(_onFailToOpen, "OnFailToOpen");
    gff.readResRef(_onDialog, "OnDialog");
    gff.readByte(_trapType, "TrapType");
    gff.readBool(_trapDisarmable, "TrapDisarmable");
    gff.readBool(_trapDetectable, "TrapDetectable");
    gff.readByte(_disarmDC, "DisarmDC");
    gff.readByte(_trapDetectDC, "TrapDetectDC");
    gff.readByte(_trapFlag, "TrapFlag");
    gff.readBool(_trapOneShot, "TrapOneShot");
    gff.readBool(_locked, "Locked");
    gff.readBool(_lockable, "Lockable");
    gff.readByte(_linkedToFlags, "LinkedToFlags");
    gff.readString(_linkedTo, "LinkedTo");
    gff.readResRef(_linkedToModule, "LinkedToModule");
    gff.readWord(_loadScreenId, "LoadScreenID");
    gff.readLocString(_description, "Description", _services.resource.strings);
    gff.readBool(_static, "Static");
    gff.readBool(_notBlastable, "NotBlastable");
    gff.readLocString(_transitionDestin, "TransitionDestin", _services.resource.strings);

    // FIXME: deserialize EffectList, ActionList
}

void Door::loadAppearance() {
    std::shared_ptr<TwoDA> doors(_services.resource.twoDas.get("genericdoors"));
    std::string modelName(boost::to_lower_copy(doors->getString(_genericType, "modelname")));

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
}

void Door::loadLinkedTransitionGeometry(const Walkmesh &walkmesh) {
    AABB bounds;
    for (const auto &face : walkmesh.faces()) {
        for (const auto &vertex : face.vertices) {
            bounds.expand(vertex);
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
    return !_static && !_open;
}

void Door::damage(int amount, uint32_t damager) {
    if (_dead || _plot || _notBlastable) {
        return;
    }
    if (amount <= 0) {
        return;
    }

    int currentHitPoints = _currentHitPoints > 0 ? _currentHitPoints : _hitPoints;
    if (amount == std::numeric_limits<int>::max()) {
        _currentHitPoints = isMinOneHP() ? 1 : 0;
    } else {
        _currentHitPoints = std::max(isMinOneHP() ? 1 : 0, currentHitPoints - amount);
    }

    damager = damager ? damager : script::kObjectInvalid;
    runDamagedScript(damager);

    if (_currentHitPoints > 0) {
        return;
    }

    _dead = true;
    _locked = false;
    open();
    onOpen(damager);
    runDeathScript(damager);
}

void Door::open() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (model) {
        // model->setDefaultAnimation("opened1", AnimationProperties::fromFlags(AnimationFlags::loop));
        model->playAnimation("opening1");
    }
    if (_walkmeshOpen1) {
        _walkmeshOpen1->setEnabled(true);
    }
    if (_walkmeshOpen2) {
        _walkmeshOpen2->setEnabled(false);
    }
    if (_walkmeshClosed) {
        _walkmeshClosed->setEnabled(false);
    }
    _open = true;
}

void Door::close() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (model) {
        // model->setDefaultAnimation("closed", AnimationProperties::fromFlags(AnimationFlags::loop));
        model->playAnimation("closing1");
    }
    if (_walkmeshOpen1) {
        _walkmeshOpen1->setEnabled(false);
    }
    if (_walkmeshOpen2) {
        _walkmeshOpen2->setEnabled(false);
    }
    if (_walkmeshClosed) {
        _walkmeshClosed->setEnabled(true);
    }
    _open = false;
}

void Door::onOpen(uint32_t triggererId) {
    if (_onOpen.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastOpenedBy, Variable::ofObject(triggererId)},
         {script::ArgKind::ClickingObject, Variable::ofObject(triggererId)},
         {script::ArgKind::EnteringObject, Variable::ofObject(triggererId)}});
}

void Door::onFailToOpen(const Object &triggerer) {
    if (_onFailToOpen.empty()) {
        return;
    }

    _game.scriptRunner().run(
        _onFailToOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::ClickingObject, Variable::ofObject(triggerer.id())}});
}

void Door::runDamagedScript(uint32_t damagerId) {
    if (_onDamaged.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(damagerId)},
         {script::ArgKind::LastDamager, Variable::ofObject(damagerId)}});
}

void Door::runDeathScript(uint32_t damagerId) {
    if (_onDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDeath,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(damagerId)},
         {script::ArgKind::LastDamager, Variable::ofObject(damagerId)}});
}

void Door::setLocked(bool locked) {
    _locked = locked;
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

} // namespace game

} // namespace reone
