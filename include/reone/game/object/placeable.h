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

#include <functional>
#include <optional>

#include "reone/resource/format/gffreader.h"
#include "reone/scene/node/walkmesh.h"

#include "../object.h"

#include "trap.h"

namespace reone {

namespace game {

struct AttackEventFields;
class Creature;
class Trigger;

class Placeable : public Object {
public:
    /** A portrait ID naming no portrait row: the portrait resref is used. */
    static constexpr uint16_t kNoPortraitId = 0xffff;

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

    std::string getOnSpellCastAt() const override { return _onSpellCastAt; }

    bool isPartyInteract() const { return _partyInteract; }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    void damage(
        int amount,
        const std::shared_ptr<Object> &damager) override;

    bool hasInventory() const { return _hasInventory; }
    bool isSelectable() const override { return _usable; }
    glm::vec3 getSelectablePosition() const override;
    bool isUsable() const { return _usable; }
    /** Appearances marked hostile in placeables.2da read as hostile objects. */
    bool isHostileAppearance() const { return _hostileAppearance; }
    /** Destruction animation and pending deletion do not change the HP predicate. */
    bool isDead() const override { return !_plot && currentHitPoints() <= 0; }
    bool isLocked() const { return _locked; }
    bool isKeyRequired() const { return _keyRequired; }
    bool isNotBlastable() const { return _notBlastable; }
    const std::string &keyName() const { return _keyName; }
    uint8_t closeLockDC() const { return _closeLockDC; }
    /** Run the script for being locked. */
    void onLocked();

    // Traps

    bool isTrapped() const { return _trap.isArmed(); }
    bool trapDisarmable() const { return _trap.isDisarmable(); }
    bool trapDetectable() const { return _trap.isDetectable(); }
    int ownerDemolitionsSkill() const { return _trap.ownerDemolitionsSkill(); }
    bool trapOneShot() const { return _trap.isOneShot(); }
    int trapBaseType() const { return _trap.type(); }
    const std::string &trapKeyTag() const { return _keyName; }
    int trapDetectDC() const { return _trap.detectDC(_game); }
    int trapDisarmDC() const { return _trap.disarmDC(_game); }
    const Trap &trap() const { return _trap; }
    std::optional<std::reference_wrapper<Trap>> findTrap() override { return std::ref(_trap); }
    std::optional<std::reference_wrapper<const Trap>> findTrap() const override { return std::cref(_trap); }
    TrapDetection &trapDetection() { return _trap.detection(); }
    const TrapDetection &trapDetection() const { return _trap.detection(); }
    /** Hostile when neither faction nor standing is shared with the creature. */
    bool isTrapHostileTo(const Creature &creature) const { return Trap::isHostileTo(*this, _faction, creature); }
    /** An armed trap goes off on the caller; forced firing skips the standing test. */
    void triggerTrap(const std::shared_ptr<Creature> &caller, bool force);
    /** The trap is disarmed by the caller. */
    void disarmTrap(const Object &caller) { _trap.disarm(*this, caller); }
    /** Arm a set mine; the linked mine trigger marks the trap in the world (TSL). */
    void armMine(int trapType, int detectDC, int disarmDC, int ownerDemolitionsSkill,
                 const std::shared_ptr<Trigger> &linkedMine,
                 const std::shared_ptr<Creature> &setter, int blastBonus, bool blast);

    // END Traps

    int appearance() const { return _appearance; }
    Faction faction() const { return _faction; }
    void setFaction(Faction faction) { _faction = faction; }
    /** The placeable's own saving throw from its template; effects never change it. */
    int savingThrow(SavingThrow save) const;
    std::shared_ptr<scene::WalkmeshSceneNode> walkmesh() const { return _walkmesh; }
    /** Whether a creature steps right up to the placeable's use point to use it. */
    bool isPreciseUse() const { return _preciseUse; }
    /**
     * The point a creature at from goes to in order to use the placeable: the
     * nearer of its walkmesh's two use points, the second one on a tie; the
     * placeable's own position when it has no walkmesh. A point at the world
     * origin is no point: the first gives way to the position, the second to
     * the first.
     */
    glm::vec3 nearestActionPoint(const glm::vec3 &from) const;
    /** The box around the walkmesh where it stands; empty at the origin without one. */
    graphics::AABB collisionBounds() const;

    /** The placeable takes its destroyed pose. */
    void enterDestroyedState();
    /** A damaged signal reached the placeable: the damager is recorded and the damaged script runs. */
    void receiveDamagedSignal(const std::shared_ptr<Object> &damager);
    /** A death signal reached the placeable: the killer is recorded and the death script runs. */
    void receiveDeathSignal(uint32_t killerId);
    /**
     * An attack reached the placeable: it is recorded, the melee-attacked
     * script runs, and an armed trap goes off on an attacker close by.
     */
    void receiveAttackEvent(uint32_t attackerId, std::optional<std::reference_wrapper<const AttackEventFields>> fields);
    int computerUseAdjustment() const { return _computerUseAdjustment; }
    /** The conversation with the placeable ended: its script runs and the computer use adjustment clears. */
    void runEndDialogScript();
    void update(float dt) override;

    void setLocked(bool locked) { _locked = locked; }

    // Body bags

    int bodyBagRow() const { return _bodyBagId; }
    bool isBodyBag() const { return _isBodyBag; }
    bool isCorpse() const { return _isCorpse; }
    void setBodyBagVisible(bool visible) { _isBodyBagVisible = visible; }
    /** A bag not yet shown stays hidden while the area keeps a corpse that passes picks to it. */
    bool isBodyBagHidden() const;
    /** The fixed body bag fields for a placeables.2da appearance. */
    void loadBodyBag(int appearance);
    /**
     * The bag takes the items of its source and faces as it does. A corpse bag
     * neither dies when empty nor is plot; an empty one has no inventory.
     */
    void fillBodyBag(Object &source, bool corpse, std::optional<int> nameStrRef);
    /** A corpse bag takes over its creature's body and dead pose. */
    void adoptCorpseModel(std::shared_ptr<scene::ModelSceneNode> model);

    // END Body bags

    // Inventory

    /** A use has started opening the inventory and waits for it to swing open. */
    bool isInventoryOpenPending() const { return _inventoryOpenPending; }
    /**
     * A use starts opening the inventory: the placeable swings open and plays
     * its opened sound. Returns how long the use waits before the inventory
     * opens.
     */
    float beginOpeningInventory();
    /** The use that started the opening opens the inventory for the opener. */
    void completeOpeningInventory(Object &opener);
    /**
     * A use by a creature the player does not control opens nothing: the
     * opened sound plays and the pending opening is dropped.
     */
    void playOpenedSound();
    /**
     * The inventory of a closed placeable that has one opens on the
     * controlled creature's container screen, and its open script is
     * signalled with the opener. Anyone else is refused, and the placeable
     * takes its close pose.
     */
    void openInventory(Object &opener);
    /**
     * An open inventory closes. Taking all moves every item to a closer the
     * player controls. The closed script is signalled with the closer and the
     * placeable takes its close pose; closing it empty destroys a placeable
     * that dies when empty.
     */
    void closeInventory(Object &closer, bool takeAll);

    // END Inventory

    // Scripts

    void onOpen(uint32_t triggererId);
    /** The placeable failed to open for the opener: its script for that runs. */
    void onFailToOpen(uint32_t openerId);
    void onClosed(uint32_t closerId);
    void runOnUsed(std::shared_ptr<Object> usedBy);
    void runOnInvDisturbed(uint32_t triggerrer, InventoryDisturbType type, uint32_t item);

    // END Scripts

    void applyDamageEffect(
        int amount,
        const std::shared_ptr<Object> &damager,
        std::optional<DamageReaction> reaction) override;

private:
    friend class ModuleSnapshotBuilder;

    /** Count down to a set mine's blast at the lock. */
    void updateMineBlast(float dt);
    void updateHeartbeat();
    void acquireItemsFrom(Object &source);
    void updateBodyBagPresentation();
    /**
     * The placeable takes an open-state animation: opening swings it open
     * first and closing an open one swings it shut first, then the state's
     * loop plays. A corpse bag keeps its body's dead pose.
     */
    void setOpenStateAnimation(int animation);
    void updateOpenStateAnimation();

    // Serializable
    resource::LocString _locName;
    bool _autoRemoveKey {false};
    Faction _faction {Faction::Invalid};
    uint8_t _openLockDC {0};
    std::string _keyName;
    Trap _trap {Trap::Carrier::Placeable};
    bool _usable {false};
    bool _hostileAppearance {false};
    bool _preciseUse {false};
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
    bool _partyInteract {false};
    uint16_t _portraitId {kNoPortraitId};
    std::string _portrait;
    uint8_t _bodyBagId {0xFF};
    bool _dieWhenEmpty {false};
    uint8_t _lightState {0};
    resource::LocString _description;

    std::string _onClosed;
    std::string _onDamaged;
    std::string _onDeath;
    std::string _onInvDisturbed;
    std::string _onFailToOpen;
    std::string _onLock;
    std::string _onMeleeAttacked;
    std::string _onOpen;
    std::string _onSpellCastAt;
    std::string _onUnlock;
    std::string _onUsed;
    std::string _onDialog;
    std::string _onEndDialogue;

    int32_t _animation {-1};
    bool _isBodyBag {false};
    bool _isBodyBagVisible {true};
    bool _isCorpse {false};

    // END Serializable

    int _animationState {0};
    bool _inventoryOpenPending {false};
    /** The loop that follows the open-state clip running now. */
    std::string _openStateLoop;
    /**
     * Runtime dialog input: constructed and cleared to zero. Do not derive a
     * nonzero value from unrelated lock, trap or skill-effect parameters.
     */
    int _computerUseAdjustment {0};
    std::shared_ptr<scene::WalkmeshSceneNode> _walkmesh;

    // Scripts

    // END Scripts

    void runDamagedScript();

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void loadAppearance();

    void updateTransform() override;
};

} // namespace game

} // namespace reone
