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

#include "../object.h"

#include "trap.h"

namespace reone {

namespace scene {
class ModelSceneNode;
}

namespace game {

class Area;
class Creature;

/** How a mine trigger is shown leaving when it is removed. */
enum class MineRemoval {
    /** It leaves at once. */
    Plain,
    /** It goes with its explosion: its type's explosion sound and its activate clip. */
    Explosion,
    /** It goes disarmed: its deactivate clip. */
    Disarmed
};

class Trigger : public Object {
public:
    /** The trigger type of an area transition. */
    static constexpr int kAreaTransitionType = 1;
    /** The trigger type of a trap. */
    static constexpr int kTrapType = 2;

    enum class DebugState {
        Default,
        Tested,
        Inside,
        Entered
    };

    Trigger(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Trigger,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Trigger;
    }

    void loadFromBlueprint(const std::string &resRef);
    Faction faction() const { return _faction; }
    void setFaction(Faction faction) { _faction = faction; }

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    void update(float dt) override;
    void resolveSavedReferences(
        const std::function<std::shared_ptr<Object>(uint32_t)> &resolver) override;

    void addTenant(const std::shared_ptr<Object> &object);
    /** Counts the object inside without announcing it: it must step out and in again to be announced. */
    void recordTenant(const std::shared_ptr<Object> &object);

    /**
     * Drops an object from the tenants. Announced, it becomes the object that
     * last left and the exit script runs. Unannounced, as for an object
     * destroyed while standing inside, nothing runs: a destroyed object never
     * moves, so it would otherwise stay a tenant for good.
     */
    void removeTenant(const Object *object, bool announce = false);

    /**
     * Whether a point is inside the trigger: for a trap, within the trap
     * radius of its position in three dimensions; otherwise inside its
     * outline on the ground plane, and where floors lie over others (see
     * Area::hasStackedFloors), at the level of the trigger's position (see
     * Area::isAtLevelOf).
     */
    bool isIn(const glm::vec3 &point) const;
    /**
     * Whether the step from \p from to \p to crosses the trigger's edge: for a
     * trap, the surface of its radius; otherwise its outline on the ground
     * plane, and where floors lie over others, with one end at the level of
     * the trigger's position.
     */
    bool crosses(const glm::vec3 &from, const glm::vec3 &to) const;
    bool isTenant(const std::shared_ptr<Object> &object) const;
    /** Whether the trigger's type makes it an area transition. */
    bool isAreaTransition() const { return _triggerType == kAreaTransitionType; }
    /** Whether the trigger is of the generic type, neither a transition nor a trap. */
    bool isGeneric() const { return _triggerType == 0; }

    const std::vector<glm::vec3> &geometry() const { return _geometry; }
    DebugState debugState() const;
    glm::vec4 debugColor() const;

    void markDebugTested(bool inside);
    void markDebugEntered();

    const std::string &getOnEnter() const { return _onEnter; }
    /**
     * A creature walked up to the trigger it was heading for: it becomes the
     * entering object, and the click script runs with it as the clicking
     * object. An area transition with no click script of its own, or the
     * default one, takes the stock transition script for good.
     */
    void receiveClick(const std::shared_ptr<Object> &clicker);
    /**
     * An object entering the trigger: it becomes the entering object. At an
     * area transition, which also hears its own entry signalled again with
     * its number \p transition, the party passes as Area::passTransition()
     * says; a mine fires on the one entering, on any of them when \p force;
     * any other trigger runs its enter script.
     */
    void receiveEntered(const std::shared_ptr<Object> &enterer, std::optional<int> transition = std::nullopt, bool force = false);
    /** An object leaving the trigger: it becomes the leaving object, and the exit script runs. */
    void receiveExited(const std::shared_ptr<Object> &exiter);
    const std::string &getOnExit() const { return _onExit; }
    /** The object that last came into the trigger, kept until the next one does. */
    uint32_t lastEnteredId() const { return _lastEnteredId; }
    /** The object that last left the trigger, kept until the next one does. */
    uint32_t lastLeftId() const { return _lastLeftId; }

    // Traps

    bool isTrap() const { return _isTrap; }
    bool isTrapped() const { return _isTrap; }
    /** A trap trigger counts among the area's traps for as long as it stands. */
    bool countsAsAreaTrap() const { return _isTrap; }
    /** The trap flag: 1 while the trigger is a trap, else 0. */
    uint8_t trapFlag() const { return _isTrap ? 1 : 0; }
    bool trapDisarmable() const { return _trap.isDisarmable(); }
    bool trapDetectable() const { return _trap.isDetectable(); }
    bool trapOneShot() const { return _trap.isOneShot(); }
    int trapBaseType() const { return _trap.type(); }
    const std::string &trapKeyTag() const { return _keyName; }
    int trapDetectDC() const { return _trap.detectDC(_game); }
    int trapDisarmDC() const { return _trap.disarmDC(_game); }
    const Trap &trap() const { return _trap; }
    /** A trigger's trap record; only a trap trigger is armed with it. */
    std::optional<std::reference_wrapper<Trap>> findTrap() override { return std::ref(_trap); }
    /** A trigger's trap record; only a trap trigger is armed with it. */
    std::optional<std::reference_wrapper<const Trap>> findTrap() const override { return std::cref(_trap); }
    TrapDetection &trapDetection() { return _trap.detection(); }
    const TrapDetection &trapDetection() const { return _trap.detection(); }
    /** A trap is hostile to a creature it regards at 89 or less that is not of its faction. */
    bool isTrapHostileTo(const Creature &creature) const { return Trap::isHostileTo(*this, _faction, creature); }
    /** Point of this trigger nearest to a position: the trap radius, or the polygon outline. */
    glm::vec3 nearestPoint(const glm::vec3 &from) const;
    /** The trap's model; none while the trap does not exist for the player (see Area::updatePresence). */
    std::shared_ptr<scene::ModelSceneNode> trapModel() const { return _trapModel; }
    /** The sound of the mine going off as it is removed (its trap type's); empty when blank. */
    const std::string &trapExplosionSound() const;
    glm::vec3 getSelectablePosition() const override;

    /** The creature that set this trap, or none. */
    std::shared_ptr<Creature> trapCreator() const;
    /** The creator's identity as recorded, whether or not it still exists. */
    uint32_t trapCreatorId() const;
    bool isSetByPlayerParty() const { return _setByPlayerParty; }
    int ownerDemolitionsSkill() const { return _trap.ownerDemolitionsSkill(); }
    /**
     * Make this trigger a mine: a 4 m square around the position, with the
     * type's script and name, the given DCs and its creator's side. Each
     * corner stands at the ground height of the area there, the room under it
     * looked for around height 0, or level with the mine where that height is
     * 0.
     */
    void initMine(
        const Area &area,
        int trapType,
        const glm::vec3 &position,
        const std::shared_ptr<Object> &creator,
        Faction faction,
        int detectDC,
        int disarmDC,
        int ownerDemolitionsSkill);
    /** A mine standing for a trapped door or placeable: neither seen nor disarmed on its own. */
    void hideLinkedMine() { _trap.hide(); }
    /**
     * A creature enters the mine: it becomes the entering object, and the
     * mine goes off on it. Forced firing skips the standing test. Whatever
     * happens, a mine entered goes with its explosion when it is removed.
     */
    void enterMine(const std::shared_ptr<Creature> &creature, bool force);
    /**
     * Whether the mine goes off on the creature: without force, only on one
     * that regards the mine's creator (or the mine itself) as an enemy and is
     * not of its faction. A creature immune to traps, and in TSL one with the
     * mine immunity feat or in a party whose leader has the party feat, walks
     * over it.
     */
    bool canFireMineOn(const Creature &creature, bool force) const;
    /** How the mine is shown leaving when it is removed. */
    MineRemoval mineRemoval() const { return _mineRemoval; }
    /** Sets how the mine is shown leaving when it is removed. */
    void setMineRemoval(MineRemoval removal) { _mineRemoval = removal; }
    /**
     * A script destroying the mine with a fade shows it leaving by that fade,
     * in milliseconds: 1 shows its explosion, 2 its disarm, and any other has
     * it leave at once.
     */
    void setMineRemovalFromFade(uint32_t fadeMilliseconds);
    /**
     * The trap's model leaves the trigger to play out its removal; the
     * trigger keeps none.
     */
    std::shared_ptr<scene::ModelSceneNode> releaseTrapModel();
    /** The trap's model is drawn this far below the trigger. */
    void lowerTrapModel(float depth);
    /** Where the trap's model is drawn: the trigger's position, less any lowering. */
    glm::vec3 trapModelPosition() const;

    // END Traps

    const std::string &linkedToModule() const { return _linkedToModule; }
    const std::string &linkedTo() const { return _linkedTo; }
    uint8_t linkedToFlags() const { return _linkedToFlags; }
    const std::string &transitionDestin() const { return _transitionDestin.str(); }

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;
    // Serializable
    std::string _onEnter;
    std::string _onExit;
    std::string _onClick;
    uint32_t _lastEnteredId {script::kObjectInvalid};
    uint32_t _lastLeftId {script::kObjectInvalid};

    Trap _trap {Trap::Carrier::Trigger};
    std::string _linkedTo;
    uint8_t _linkedToFlags {0};
    std::string _linkedToModule;
    bool _autoRemoveKey {false};
    resource::LocString _locName;
    Faction _faction {Faction::Invalid};
    std::string _keyName;
    int32_t _triggerType {0};
    float _highlightHeight {0.1f};
    uint16_t _loadScreenId {0};
    resource::LocString _transitionDestin;
    bool _setByPlayerParty {false};
    std::vector<glm::vec3> _geometry;
    // END Serializable

    bool _isTrap {false};
    SavedObjectReference _creator;
    std::shared_ptr<scene::ModelSceneNode> _trapModel;
    /** How far below the trigger the trap's model is drawn. */
    float _trapModelDepth {0.0f};
    bool _trapShown {false};
    /** How the mine is shown leaving when it is removed; it leaves at once unless told otherwise. */
    MineRemoval _mineRemoval {MineRemoval::Plain};

    std::set<std::shared_ptr<Object>> _tenants;
    float _debugTestAge {0.0f};
    float _debugInsideAge {0.0f};
    float _debugEnterAge {0.0f};

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    /** The entering creature steps on the trap: its key disarms it, or the trap goes off. */
    void stepOnMine(bool force);
    void loadAppearance();
    /** Builds the trap's model at its place, showing the hidden loop. */
    void loadTrapModel();
    /**
     * Builds or drops the trap's model as the trigger comes to exist or
     * stops existing for the player (see Area::updatePresence), and has it
     * show the shown or hidden loop.
     */
    void updateTrapPresentation();
    void setTrapCreator(const std::shared_ptr<Object> &creator);
    /** The reach of a trap around its position: ranges.2da's trap row, or a metre without one. */
    float trapRadius() const;
    /** Whether a point stands at the level of the trigger's position (see Area::isAtLevelOf); every point does outside an area. */
    bool isAtLevel(const glm::vec3 &point) const;
    /**
     * Set the mine off on the entering creature, when there is one. Whatever
     * happens, the mine then goes with its explosion when it is removed.
     */
    void fireMine(bool force);

    void syncDebugVisual();
};

} // namespace game

} // namespace reone
