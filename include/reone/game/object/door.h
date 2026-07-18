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

#pragma once

#include "reone/resource/format/gffreader.h"
#include "reone/scene/node/walkmesh.h"

#include "../object.h"

namespace reone {

namespace game {

class Door : public Object {
public:
    Door(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Door,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Door;
    }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(const resource::Gff &gff);

    bool isSelectable() const override;
    void damage(int amount, uint32_t damager) override;

    void open();
    void close();

    bool isLocked() const { return _locked; }
    bool isStatic() const { return _static; }
    bool isKeyRequired() const { return _keyRequired; }
    bool isAutoRemoveKey() const { return _autoRemoveKey; }
    bool isNotBlastable() const { return _notBlastable; }

    const std::string &keyName() const { return _keyName; }

    void onOpen(uint32_t triggererId);
    void onFailToOpen(const Object &triggerer);

    const std::string &getOnOpen() const { return _onOpen; }
    const std::string &getOnFailToOpen() const { return _onFailToOpen; }

    int genericType() const { return _genericType; }
    Faction faction() const { return _faction; }
    const std::string &linkedToModule() const { return _linkedToModule; }
    const std::string &linkedTo() const { return _linkedTo; }
    uint8_t linkedToFlags() const { return _linkedToFlags; }
    const std::string &transitionDestin() const { return _transitionDestin.str(); }
    const resource::LocString &transitionDestination() const { return _transitionDestin; }
    const std::vector<glm::vec3> &linkedTransitionGeometry() const { return _linkedTransitionGeometry; }

    void setLocked(bool locked);

    // Walkmeshes

    std::shared_ptr<scene::WalkmeshSceneNode> walkmeshOpen1() const { return _walkmeshOpen1; }
    std::shared_ptr<scene::WalkmeshSceneNode> walkmeshOpen2() const { return _walkmeshOpen2; }
    std::shared_ptr<scene::WalkmeshSceneNode> walkmeshClosed() const { return _walkmeshClosed; }

    // END Walkmeshes

private:
    // Serializable
    resource::LocString _locName;
    uint32_t _appearance {0};
    uint8_t _genericType {0};
    bool _isOpen {false};
    bool _autoRemoveKey {false};
    Faction _faction {Faction::Invalid};
    uint8_t _fort {0};
    uint8_t _will {0};
    uint8_t _ref {0};
    std::string _keyName;
    bool _keyRequired {false};
    uint8_t _openLockDC {0};
    uint8_t _closeLockDC {0};
    uint8_t _secretDoorDC {0};
    uint16_t _portraitId {0};
    uint8_t _hardness {0};

    std::string _onClosed;
    std::string _onDamaged;
    std::string _onDeath;
    std::string _onDisarm;
    std::string _onLock;
    std::string _onMeleeAttacked;
    std::string _onOpen;
    std::string _onSpellCastAt;
    std::string _onTrapTriggered;
    std::string _onUnlock;
    std::string _onClick;
    std::string _onFailToOpen;
    std::string _onDialog;

    uint8_t _trapType {0};
    bool _trapDisarmable {true};
    bool _trapDetectable {true};
    uint8_t _disarmDC {0};
    uint8_t _trapDetectDC {0};
    uint8_t _trapFlag {0};
    bool _trapOneShot {true};
    bool _locked {false};
    bool _lockable {false};
    uint8_t _linkedToFlags {0};
    std::string _linkedTo;
    std::string _linkedToModule;
    uint16_t _loadScreenId {0};
    resource::LocString _description;
    bool _static {false};
    bool _notBlastable {false};
    resource::LocString _transitionDestin;
    // END Serializable

    // Walkmeshes

    std::shared_ptr<scene::WalkmeshSceneNode> _walkmeshOpen1;
    std::shared_ptr<scene::WalkmeshSceneNode> _walkmeshOpen2;
    std::shared_ptr<scene::WalkmeshSceneNode> _walkmeshClosed;
    std::vector<glm::vec3> _linkedTransitionGeometry;

    // END Walkmeshes

    // Scripts

    // END Scripts

    void runDamagedScript(uint32_t damagerId);
    void runDeathScript(uint32_t damagerId);

    void deserializeAll(const resource::Gff &gff);
    void loadAppearance();
    void loadLinkedTransitionGeometry(const graphics::Walkmesh &walkmesh);
    void updateTransform() override;
};

} // namespace game

} // namespace reone
