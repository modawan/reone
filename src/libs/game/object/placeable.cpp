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

#include "reone/game/object/placeable.h"

#include <algorithm>
#include <limits>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/script/runner.h"
#include "reone/graphics/di/services.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/model.h"
#include "reone/script/types.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

void Placeable::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> utp(_services.resource.gffs.get(resRef, ResType::Utp));
    if (!utp) {
        return;
    }
    deserialize(*utp);
}

void Placeable::deserialize(const resource::Gff &gff) {
    std::string templateRes;
    if (gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utp = _services.resource.gffs.get(templateRes, ResType::Utp)) {
            deserializeAll(*utp);
        }
    }
    deserializeAll(gff);
    loadAppearance();
    updateTransform();
}

void Placeable::deserializeAll(const resource::Gff &gff) {
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    if (gff.readLocString(_locName, "LocName", _services.resource.strings)) {
        _name = _locName.str();
    }
    gff.readBool(_autoRemoveKey, "AutoRemoveKey");
    gff.readEnum(_faction, "Faction");
    gff.readBool(_plot, "Plot");
    gff.readBool(_minOneHP, "Min1HP");
    gff.readByte(_openLockDC, "OpenLockDCk");
    gff.readString(_keyName, "KeyName");
    gff.readBool(_trapDisarmable, "TrapDisarmable");
    gff.readBool(_trapDetectable, "TrapDetectable");
    gff.readByte(_disarmDC, "DisarmDC");
    gff.readByte(_trapDetectDC, "TrapDetectDC");
    gff.readByte(_trapFlag, "TrapFlag");
    gff.readBool(_trapOneShot, "TrapOneShot");
    gff.readByte(_trapType, "TrapType");
    gff.readBool(_usable, "Useable");
    gff.readBool(_static, "Static");
    gff.readBool(_notBlastable, "NotBlastable");
    gff.readBool(_groundPile, "GroundPile");
    gff.readDword(_appearance, "Appearance");
    if (gff.readShort(_hitPoints, "HP")) {
        _maxHitPoints = _hitPoints;
    }
    gff.readShort(_currentHitPoints, "CurrentHP");
    gff.readByte(_hardness, "Hardness");
    gff.readByte(_fort, "Fort");
    gff.readByte(_will, "Will");
    gff.readByte(_ref, "Ref");
    gff.readBool(_lockable, "Lockable");
    gff.readBool(_locked, "Locked");
    gff.readBool(_hasInventory, "HasInventory");
    gff.readBool(_keyRequired, "KeyRequired");
    gff.readByte(_closeLockDC, "CloseLockDC");
    gff.readBool(_open, "Open");
    gff.readBool(_partyInteract, "PartyInteract");
    gff.readWord(_portraitId, "PortraitId");
    gff.readResRef(_conversation, "Conversation");
    gff.readByte(_bodyBagId, "BodyBag");
    gff.readBool(_dieWhenEmpty, "DieWhenEmpty");
    gff.readByte(_lightState, "LightState");
    gff.readLocString(_description, "Description", _services.resource.strings);
    gff.readResRef(_onClosed, "OnClosed");
    gff.readResRef(_onDamaged, "OnDamaged");
    gff.readResRef(_onDeath, "OnDeath");
    gff.readResRef(_onDisarm, "OnDisarm");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onInvDisturbed, "OnInvDisturbed");
    gff.readResRef(_onLock, "OnLock");
    gff.readResRef(_onMeleeAttacked, "OnMeleeAttacked");
    gff.readResRef(_onOpen, "OnOpen");
    gff.readResRef(_onSpellCastAt, "OnSpellCastAt");
    gff.readResRef(_onUnlock, "OnUnlock");
    gff.readResRef(_onUsed, "OnUsed");
    gff.readResRef(_onUserDefined, "OnUserDefined");
    gff.readResRef(_onDialog, "OnDialog");
    gff.readResRef(_onEndDialogue, "OnEndDialogue");
    gff.readResRef(_onTrapTriggered, "OnTrapTriggered");
    gff.readInt(_animation, "Animation");
    {
        float bearing;
        if (gff.readFloat(bearing, "Bearing")) {
            _orientation = glm::quat(glm::vec3(0.0f, 0.0f, bearing));
        }
    }
    gff.readFloat(_position[0], "X");
    gff.readFloat(_position[1], "Y");
    gff.readFloat(_position[2], "Z");
    gff.readBool(_isBodyBag, "IsBodyBag");
    gff.readBool(_isBodyBagVisible, "IsBodyBagVisible");
    gff.readBool(_isCorpse, "IsCorpse");
    gff.readBool(_commandable, "Commandable");

    for (const auto &itemGff : gff.getList("ItemList")) {
        std::shared_ptr<Item> item = _game.newItem();
        item->deserialize(*itemGff);
        item->setDropable(true);
        addItem(item);
    }

    // FIXME: deserialize EffectList, ActionList
}

void Placeable::loadAppearance() {
    std::shared_ptr<TwoDA> placeables(_services.resource.twoDas.get("placeables"));
    std::string modelName(boost::to_lower_copy(placeables->getString(_appearance, "modelname")));

    auto model = _services.resource.models.get(modelName);
    if (!model) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    auto sceneNode = sceneGraph.newModel(*model, ModelUsage::Placeable);
    sceneNode->setUser(*this);
    sceneNode->setDrawDistance(_game.options().graphics.drawDistance);
    _sceneNode = std::move(sceneNode);

    auto walkmesh = _services.resource.walkmeshes.get(modelName, ResType::Pwk);
    if (walkmesh) {
        _walkmesh = sceneGraph.newWalkmesh(*walkmesh);
    }
}

void Placeable::damage(int amount, uint32_t damager) {
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
    _open = true;
    onOpen(damager);
    runDeathScript(damager);
}

void Placeable::onOpen(uint32_t triggererId) {
    if (_onOpen.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastOpenedBy, Variable::ofObject(triggererId)}});
}

void Placeable::runOnUsed(std::shared_ptr<Object> usedBy) {
    if (_onUsed.empty()) {
        return;
    }

    std::vector<script::Argument> args;
    args.emplace_back(script::ArgKind::Caller, Variable::ofObject(_id));

    if (usedBy) {
        args.emplace_back(script::ArgKind::LastUsedBy,
                          Variable::ofObject(usedBy->id()));
    }

    _game.scriptRunner().run(_onUsed, args);
}

void Placeable::runOnInvDisturbed(std::shared_ptr<Object> triggerrer) {
    if (_onInvDisturbed.empty()) {
        return;
    }

    std::vector<script::Argument> args;
    args.emplace_back(script::ArgKind::Caller, Variable::ofObject(_id));

    // FIXME: implement LastDisturbed
    // triggerrer ? triggerrer->id() : kObjectInvalid

    _game.scriptRunner().run(_onInvDisturbed, args);
}

void Placeable::runDamagedScript(uint32_t damagerId) {
    if (_onDamaged.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(damagerId)},
         {script::ArgKind::LastDamager, Variable::ofObject(damagerId)}});
}

void Placeable::runDeathScript(uint32_t damagerId) {
    if (_onDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDeath,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(damagerId)},
         {script::ArgKind::LastDamager, Variable::ofObject(damagerId)}});
}

void Placeable::updateTransform() {
    Object::updateTransform();

    if (_walkmesh) {
        _walkmesh->setLocalTransform(_transform);
    }
}

} // namespace game

} // namespace reone
