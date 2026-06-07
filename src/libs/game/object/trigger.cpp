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

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/script/runner.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/trigger.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

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
    deserialize(*utt);
}

void Trigger::deserialize(const resource::Gff &gff) {
    std::string templateRes;
    if (gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utt = _services.resource.gffs.get(templateRes, ResType::Utt)) {
            deserializeAll(*utt);
        }
    }
    deserializeAll(gff);
    loadAppearance();
    updateTransform();
}

void Trigger::deserializeAll(const resource::Gff &gff) {
    gff.readResRef(_onHeartbeat, "ScriptHeartbeat");
    gff.readResRef(_onEnter, "ScriptOnEnter");
    gff.readResRef(_onExit, "ScriptOnExit");
    gff.readResRef(_onUserDefined, "ScriptUserDefine");
    gff.readResRef(_onTrapTriggered, "OnTrapTriggered");
    gff.readResRef(_onDisarm, "OnDisarm");
    gff.readByte(_trapType, "TrapType");
    gff.readBool(_trapOneShot, "TrapOneShot");
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
    gff.readBool(_trapDisarmable, "TrapDisarmable");
    gff.readBool(_trapDetectable, "TrapDetectable");
    gff.readInt(_triggerType, "Type");
    gff.readFloat(_highlightHeight, "HighlightHeight");

    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");
    {
        float cosine, sine;
        if (gff.readFloat(cosine, "XOrientation") && gff.readFloat(sine, "YOrientation")) {
            _orientation = glm::quat(glm::vec3(0.0f, 0.0f, -glm::atan(cosine, sine)));
        }
    }
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
    // - OnClick
    // - CreatorId (IDs are not deserialized)
    // - Cursor
    // - PortraitId
    // - VarTable
    // - SWVarTable
}

void Trigger::loadAppearance() {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    _sceneNode = sceneGraph.newTrigger(_geometry);
    _sceneNode->setLocalTransform(glm::translate(_position));
    syncDebugVisual();
}

void Trigger::update(float dt) {
    Object::update(dt);

    _debugTestAge = glm::max(0.0f, _debugTestAge - dt);
    _debugInsideAge = glm::max(0.0f, _debugInsideAge - dt);
    _debugEnterAge = glm::max(0.0f, _debugEnterAge - dt);

    std::set<std::shared_ptr<Object>> tenantsToRemove;
    for (auto &tenant : _tenants) {
        if (tenant) {
            glm::vec2 position2d(tenant->position());
            if (isIn(position2d))
                continue;
        }
        tenantsToRemove.insert(tenant);
    }
    for (auto &tenant : tenantsToRemove) {
        _tenants.erase(tenant);

        if (_onExit.empty()) {
            continue;
        }

        _game.scriptRunner().run(
            _onExit,
            {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
             {script::ArgKind::ExitingObject, script::Variable::ofObject(tenant->id())}});
    }

    syncDebugVisual();
}

void Trigger::addTenant(const std::shared_ptr<Object> &object) {
    _tenants.insert(object);
    syncDebugVisual();
    if (_onEnter.empty()) {
        return;
    }

    _game.scriptRunner().run(
        _onEnter,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::EnteringObject,
          script::Variable::ofObject(object->id())}});
}

void Trigger::removeTenant(const Object *object) {
    for (auto it = _tenants.begin(); it != _tenants.end(); ++it) {
        if (it->get() == object) {
            _tenants.erase(it);
            syncDebugVisual();
            return;
        }
    }
}

bool Trigger::isIn(const glm::vec2 &point) const {
    return static_cast<TriggerSceneNode *>(_sceneNode.get())->isIn(point);
}

bool Trigger::isTenant(const std::shared_ptr<Object> &object) const {
    auto maybeTenant = find(_tenants.begin(), _tenants.end(), object);
    return maybeTenant != _tenants.end();
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
