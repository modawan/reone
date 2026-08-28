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

class Creature;
class IReputes;

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
    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    bool isSelectable() const override;
    void damage(int amount, uint32_t damager) override;
    void update(float dt) override;

    void open();
    void close();

    /**
     * Resting state the door last physically reached. A door in transition
     * still reports the state it is leaving, because that is where it still
     * stands, and its collision, pose and open flag all follow from it.
     */
    DoorState state() const { return _state; }

    /** Transition the door is playing, if any. */
    DoorTransition transition() const { return _transition; }

    /**
     * True while the door is swinging open. It has not reached its opened pose
     * yet, so the doorway is still blocking and isOpen is still false.
     */
    bool isOpening() const { return _transition == DoorTransition::Opening; }

    /**
     * True while the door is swinging shut. It has not reached its closed pose
     * yet, so the doorway is still passable and isOpen is still true.
     */
    bool isClosing() const { return _transition == DoorTransition::Closing; }

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
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;
    // Serializable
    resource::LocString _locName;
    uint32_t _appearance {0};
    uint8_t _genericType {0};
    DoorState _state {DoorState::Closed};
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

    /**
     * The transition in flight. It names an animation to wait on and nothing
     * else: collision, pose and the open flag are read off _state alone, and
     * _state only moves when a transition arrives. A reversal overwrites this,
     * which is what stops the superseded transition from ever completing.
     */
    DoorTransition _transition {DoorTransition::None};

    void runDamagedScript(uint32_t damagerId);
    void runDeathScript(uint32_t damagerId);

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void loadAppearance();

    /** Put model pose, walkmeshes and the open flag into correspondence with _state. */
    void applyRestingState();
    void applyRestingPose();
    void enableStateWalkmeshes();
    void beginTransition(DoorTransition transition);
    void finishTransition();
    bool isTransitionComplete() const;
    static const char *transitionAnimation(DoorTransition transition);
    static DoorState transitionTarget(DoorTransition transition);
    void loadLinkedTransitionGeometry(const graphics::Walkmesh &walkmesh);
    void updateTransform() override;
};

/**
 * Whether actor is allowed to bash door open. This is the single definition of
 * door bash eligibility, shared by the player context action and by the
 * DOOR_ACTION_BASH script routines.
 */
bool canBashDoor(const Door &door, const Creature &actor, const IReputes &reputes);

} // namespace game

} // namespace reone
