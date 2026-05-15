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

class Placeable : public Object {
public:
    Placeable(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Placeable,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Placeable;
    }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(const resource::Gff &gff);

    void damage(int amount, uint32_t damager) override;

    bool hasInventory() const { return _hasInventory; }
    bool isSelectable() const override { return _usable; }
    bool isUsable() const { return _usable; }
    bool isLocked() const { return _locked; }
    bool isKeyRequired() const { return _keyRequired; }
    bool isNotBlastable() const { return _notBlastable; }

    int appearance() const { return _appearance; }
    Faction faction() const { return _faction; }
    std::shared_ptr<scene::WalkmeshSceneNode> walkmesh() const { return _walkmesh; }

    void setLocked(bool locked) { _locked = locked; }

    // Scripts

    void onOpen(uint32_t triggererId);
    void runOnUsed(std::shared_ptr<Object> usedBy);
    void runOnInvDisturbed(std::shared_ptr<Object> triggerrer);

    // END Scripts

private:
    // Serializable
    resource::LocString _locName;
    bool _autoRemoveKey {false};
    Faction _faction {Faction::Invalid};
    uint8_t _openLockDC {0};
    std::string _keyName;
    bool _trapDisarmable {true};
    bool _trapDetectable {true};
    uint8_t _disarmDC {0};
    uint8_t _trapDetectDC {0};
    uint8_t _trapFlag {0};
    bool _trapOneShot {true};
    uint8_t _trapType {0};
    bool _usable {false};
    bool _static {false};
    bool _notBlastable {false};
    bool _groundPile {false};
    uint32_t _appearance {0};
    uint8_t _hardness {0};
    uint8_t _fort {0};
    uint8_t _will {0};
    uint8_t _ref {0};
    bool _lockable {false};
    bool _locked {false};
    bool _hasInventory {false};
    bool _keyRequired {false};
    uint8_t _closeLockDC {0};
    bool _open {false};
    bool _partyInteract {false};
    uint16_t _portraitId {0};
    uint8_t _bodyBagId {0xFF};
    bool _dieWhenEmpty {false};
    uint8_t _lightState {0};
    resource::LocString _description;

    std::string _onClosed;
    std::string _onDamaged;
    std::string _onDeath;
    std::string _onDisarm;
    std::string _onInvDisturbed;
    std::string _onLock;
    std::string _onMeleeAttacked;
    std::string _onOpen;
    std::string _onSpellCastAt;
    std::string _onUnlock;
    std::string _onUsed;
    std::string _onDialog;
    std::string _onEndDialogue;
    std::string _onTrapTriggered;

    int32_t _animation {-1};
    bool _isBodyBag {false};
    bool _isBodyBagVisible {true};
    bool _isCorpse {false};

    // END Serializable

    int _animationState {0};
    std::shared_ptr<scene::WalkmeshSceneNode> _walkmesh;

    // Scripts

    // END Scripts

    void runDamagedScript(uint32_t damagerId);
    void runDeathScript(uint32_t damagerId);

    void deserializeAll(const resource::Gff &gff);
    void loadAppearance();

    void updateTransform() override;
};

} // namespace game

} // namespace reone
