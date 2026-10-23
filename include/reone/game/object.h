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

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "reone/scene/animproperties.h"
#include "reone/scene/graph.h"
#include "reone/scene/node.h"
#include "reone/scene/user.h"
#include "reone/script/types.h"
#include "reone/system/cast.h"
#include "reone/system/timer.h"

#include "action.h"
#include "action/playanimation.h"
#include "castspell.h"
#include "effect.h"
#include "runtimeref.h"
#include "savedruntime.h"
#include "saveprovenance.h"
#include "types.h"

namespace reone {

namespace resource {
class Gff;
}

namespace game {

struct ServicesView;

class Action;
class Area;
class Game;
class Item;
class ModuleSnapshotBuilder;
class Room;
class Trap;

/**
 * Trap state that lives only for the session: the flagged state and the
 * creatures that have found the trap. Entries are never removed.
 */
struct TrapDetection {
    bool flagged {false};
    std::vector<uint32_t> detectedBy;

    bool isDetectedBy(uint32_t creatureId) const {
        return std::find(detectedBy.begin(), detectedBy.end(), creatureId) != detectedBy.end();
    }
    void addDetectedBy(uint32_t creatureId) {
        if (!isDetectedBy(creatureId)) {
            detectedBy.push_back(creatureId);
        }
    }
};

/**
 * A creature's reaction to damage other than a weapon hit: electrical or dark
 * side damage jolts it for the reaction time, any other type makes it flinch.
 */
struct DamageReaction {
    bool jolt {false};
    int milliseconds {0};
};

class Object : public scene::IUser, boost::noncopyable {
    friend class Area;

public:
    /** Damage immunity is kept within these bounds. */
    static constexpr int kMinDamageImmunity = -100;
    static constexpr int kMaxDamageImmunity = 100;
    /** The spell of an object that is not running a spell-impact script. */
    static constexpr uint32_t kNoEffectSpellId = 0xffffffffu;

    enum class RuntimeState {
        Constructing,
        Live,
        Retired,
        Presentation,
    };

    virtual ~Object() = default;

    static bool classof(Object *from) {
        return true;
    }

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    /** Places the object at a record's position, facing its orientation or bearing. */
    void deserializePlacement(const resource::Gff &gff);

    virtual void update(float dt);
    virtual void damage(
        int amount,
        const std::shared_ptr<Object> &damager);
    /** \p reaction is set for damage other than a weapon hit. */
    virtual void applyDamageEffect(
        int amount,
        const std::shared_ptr<Object> &damager,
        std::optional<DamageReaction> reaction);
    /** Heals a creature by \p amount; other objects are not healed. */
    void heal(int amount);

    void face(const Object &other);
    void face(const glm::vec3 &point);
    void faceAwayFrom(const Object &other);

    bool contains(const glm::vec3 &point) const;

    virtual bool isSelectable() const;
    bool isOpen() const { return _open; }

    bool isMinOneHP() const { return _minOneHP; }
    virtual bool isDead() const { return _dead; }
    bool isDestroyable() const { return _destroyable; }
    bool isRaiseable() const { return _raiseable; }
    bool isSelectableWhenDead() const { return _selectableWhenDead; }
    void setRaiseable(bool value) { _raiseable = value; }
    /** Sets whether the object can be destroyed, raised, and selected while dead. */
    void setDestroyability(bool destroyable, bool raiseable, bool selectableWhenDead);

    /**
     * How the object's body leaves the scene when it is destroyed. None of
     * these is saved.
     */
    bool deletesWithoutFade() const { return _deleteNoFade; }
    uint32_t fadeOutTime() const { return _fadeOutTime; }
    bool keepsCorpse() const { return _keepCorpse; }
    void setDeleteNoFade(bool value) { _deleteNoFade = value; }
    void setFadeOutTime(uint32_t milliseconds) { _fadeOutTime = milliseconds; }
    void setKeepCorpse(bool value) { _keepCorpse = value; }
    /** Ends the scene presentation of every effect, as deleting the object does. */
    void endEffectPresentations();

    bool isCommandable() const { return _commandable; }
    bool isInConversation() const { return _isInConversation; }
    std::shared_ptr<Object> dialogOwner() const { return _dialogOwner.resolve(); }
    void setDialogOwner(const std::shared_ptr<Object> &owner) { _dialogOwner = owner; }

    float getDistanceTo(const glm::vec2 &point) const;
    float getSquareDistanceTo(const glm::vec2 &point) const;
    float getDistanceTo(const glm::vec3 &point) const;
    float getSquareDistanceTo(const glm::vec3 &point) const;
    float getDistanceTo(const Object &other) const;
    float getSquareDistanceTo(const Object &other) const;

    virtual glm::vec3 getSelectablePosition() const;
    float getFacing() const { return glm::eulerAngles(_orientation).z; }

    uint32_t id() const { return _id; }
    virtual std::string getOnSpellCastAt() const { return {}; }
    /** The trap the object carries: a door's, a placeable's or a trigger's; none for any other object. */
    virtual std::optional<std::reference_wrapper<Trap>> findTrap() { return std::nullopt; }
    /** The trap the object carries: a door's, a placeable's or a trigger's; none for any other object. */
    virtual std::optional<std::reference_wrapper<const Trap>> findTrap() const { return std::nullopt; }
    /** Shows \p text over the object for \p duration seconds; no duration clears it. */
    void setFeedbackText(std::string text, float duration);
    const std::string &feedbackText() const { return _feedbackText; }
    Game &game() const { return _game; }
    ServicesView &services() const { return _services; }
    bool isRuntimeLive() const { return _runtimeState == RuntimeState::Live; }
    bool isPresentationOnly() const {
        return _runtimeState == RuntimeState::Presentation;
    }
    uint64_t runtimeIncarnation() const { return _runtimeIncarnation; }
    const std::string &tag() const { return _tag; }
    ObjectType type() const { return _type; }
    const std::string &blueprintResRef() const { return _blueprintResRef; }
    const std::string &name() const { return _name; }
    const std::string &conversation() const { return _conversation; }
    bool plotFlag() const { return _plot; }
    /**
     * Damage immunity is kept per damage type: the lowest (most vulnerable)
     * of the types in \p flags, within -100..100.
     */
    int damageImmunity(int flags) const;
    /** Stores \p value, within -100..100, for the lowest type in \p flags only. */
    void setDamageImmunity(int flags, int value);
    SpellType spellCast() const { return _spellCast; }
    void setSpellCast(SpellType spell) { _spellCast = spell; }
    uint32_t effectSpellId() const { return _effectSpellId; }
    /** The creator of an area of effect, from its saved CreatorId; none when it does not resolve. */
    std::shared_ptr<Object> effectSource() const;
    void setEffectSpellId(uint32_t spellId) { _effectSpellId = spellId; }
    const SpellCastContext &spellCastContext() const { return _spellScriptContext.cast; }
    void setSpellCastContext(SpellCastContext context) { _spellScriptContext.cast = context; }
    SpellScriptContext &spellScriptContext() { return _spellScriptContext; }
    const SpellScriptContext &spellScriptContext() const { return _spellScriptContext; }
    /**
     * The flight time of the object's last spell projectile, measured when a
     * cast releases it. An instant cast measures none and reuses this one.
     */
    uint32_t lastSpellProjectileMilliseconds() const { return _lastSpellProjectileMilliseconds; }
    void setLastSpellProjectileMilliseconds(uint32_t milliseconds) { _lastSpellProjectileMilliseconds = milliseconds; }
    /** The area whose spatial index holds the object; none while it is in no area. */
    std::optional<std::reference_wrapper<Area>> spatialArea() const {
        if (!_spatialArea) {
            return std::nullopt;
        }
        return std::ref(*_spatialArea);
    }
    /** The object and \p other are held by the same area, or both by none. */
    bool sharesSpatialArea(const Object &other) const { return _spatialArea == other._spatialArea; }

    Room *room() const { return _room; }
    const glm::vec3 &position() const { return _position; }
    const glm::mat4 &transform() const { return _transform; }
    bool visible() const { return _visible; }
    /** Whether the player's side knows of this object, so it is drawn and can be picked. */
    bool isClientPresent() const { return _clientPresent; }
    std::shared_ptr<scene::SceneNode> sceneNode() const { return _sceneNode; }

    void setTag(std::string tag) { _tag = std::move(tag); }
    void setConversation(std::string conversation) { _conversation = std::move(conversation); }
    void setName(std::string name) { _name = std::move(name); }
    void setPlotFlag(bool plot) { _plot = plot; }
    void setCommandable(bool commandable) { _commandable = commandable; }
    virtual void setIsInConversation(bool isInConversation) { _isInConversation = isInConversation; }

    void setRoom(Room *room);
    void setPosition(const glm::vec3 &position);
    /** Faces the object at once. */
    virtual void setFacing(float facing);
    void setVisible(bool visible);
    void setClientPresent(bool present);

    // Animation

    virtual void playAnimation(AnimationType type, scene::AnimationProperties properties = scene::AnimationProperties());

    virtual std::string getAnimationName(AnimationType type) const { return ""; }
    virtual std::string getActiveAnimationName() const { return ""; };

    // END Animation

    // Inventory

    /**
     * The object acquires an item: a creature counts credits, chemicals,
     * components and pazaak cards instead of keeping them (the item is
     * destroyed), and a stackable item joins matching stacks as far as they
     * hold it. Returns the stack now holding the item, or null when it was
     * counted.
     */
    std::shared_ptr<Item> addItem(const std::string &resRef, int stackSize = 1, std::optional<bool> dropable = true);
    std::shared_ptr<Item> addItem(const std::shared_ptr<Item> &item);
    bool removeItem(const std::shared_ptr<Item> &item, bool &last);
    bool removeItemStack(const std::shared_ptr<Item> &item);
    void moveDropableItemsTo(Object &other);
    /** Every item moves to other, the last first. */
    void moveAllItemsTo(Object &other);

    std::shared_ptr<Item> getFirstItem();
    std::shared_ptr<Item> getNextItem();
    std::shared_ptr<Item> getItemByTag(const std::string &tag);

    const std::vector<std::shared_ptr<Item>> &items() const { return _items; }

    // Runtime children whose semantic lifetime is owned by this object. New
    // nested game-object types participate in registry finalization by
    // extending this list; Game does not need to know their concrete type.
    virtual std::vector<std::shared_ptr<Object>> ownedRuntimeObjects() const;

    // END Inventory

    // Effects

    /**
     * Removes every effect except innate and equipped effects and the internal
     * state record, from the end of the list; with retainOwn, effects the
     * object created itself stay too. An object that had effects is
     * commandable afterwards.
     */
    void clearAllEffects(bool retainOwn = false);
    void removeEffect(const std::shared_ptr<Effect> &effect);
    /**
     * Hides the effects a script removes, chosen by \p match against \p value,
     * and queues their removal.
     */
    void queueScriptEffectRemoval(ScriptEffectRemovalMatch match, const EffectInstance &value);
    /**
     * Applies an effect value. A linked package that leaves nothing on the
     * object but effect icons and visuals (other than visuals 5000 and 5001)
     * is taken off again at once.
     */
    bool applyEffect(const std::shared_ptr<Effect> &effect, DurationType durationType, float duration = 0.0f);
    /** Applies an effect record, giving it an effect ID; returns whether it was admitted. */
    bool applyEffect(EffectInstance effect);
    bool restoreEffect(EffectInstance effect);
    size_t removeEffectsById(EffectId id);
    /** Removes the effect applied as \p applicationOrder; returns whether it left the collection. */
    bool removeEffectApplication(uint64_t applicationOrder);
    /** Applies each member of a linked package and counts those accepted and rejected. */
    EffectPackageApplicationResult applyEffectPackage(const std::vector<EffectInstance> &members);

    const std::deque<EffectInstance> &effects() const { return _effects; }
    /**
     * Presents a visual shown once on the object for \p seconds. The visual
     * is not one of the object's effects: it runs and ends on its own.
     */
    void addShownVisual(const EffectInstance &visual, float seconds);
    /** The visuals shown once on the object that are still presenting, oldest first. */
    std::vector<std::reference_wrapper<const EffectInstance>> shownVisuals() const;
    /** Find the canonical applied record for an exact executable payload. */
    EffectInstance *findEffectInstance(const Effect &effect);
    /** The applied effect record of \p applicationOrder, if it is still applied. */
    std::optional<std::reference_wrapper<EffectInstance>> findEffectApplication(uint64_t applicationOrder);
    std::vector<EffectInstance> saveEffectSnapshot() const;
    bool hasEffect(EffectType type) const;
    /** An applied effect came from the spell. */
    bool hasSpellEffect(int spell) const;
    std::shared_ptr<Effect> getFirstEffect();
    std::shared_ptr<Effect> getNextEffect();

    // END Effects

    // Stunt mode

    bool isStuntMode() const { return _stunt; }

    /**
     * Places this object into the stunt mode. Objects in this mode have their
     * position and orientation fixed to the world origin. Subsequent changes to
     * position and orientation will be buffered and applied when
     * stopStuntMode is called.
     */
    void startStuntMode();

    void stopStuntMode();

    // END Stunt mode

    // Hit Points

    // Base maximum hit points, not considering any bonuses.
    int hitPoints() const { return _hitPoints; }

    // Maximum hit points, after considering all bonuses and penalties.
    virtual int maxHitPoints() const { return _maxHitPoints; }

    // Current runtime hit points.
    virtual int currentHitPoints() const { return _currentHitPoints; }
    int currentHitPointsWithoutTemporary() const { return _currentHitPoints; }

    void setMinOneHP(bool minOneHP) { _minOneHP = minOneHP; }
    virtual void setMaxHitPoints(int maxHitPoints) { _maxHitPoints = maxHitPoints; }
    virtual void setCurrentHitPoints(int hitPoints) {
        _currentHitPoints = _minOneHP && hitPoints <= 0 ? 1 : hitPoints;
    }

    // END Hit Points

    // Actions

    /**
     * Clears the object's actions: every one that can be cleared goes, the one
     * running included, except one its round has taken up, which goes only
     * when \p force is set, as a forced clear also clears the round's attack
     * records. The round's pending entries stay; a caller that wants them gone
     * drops them itself. One that cannot be commanded keeps everything, unless
     * \p evenUncommandable is set.
     */
    virtual void clearAllActions(bool force = false, bool evenUncommandable = false);
    /** Removes every action, clearable or not, as the object leaves play. */
    virtual void teardownActions();
    /** Cancels and removes every queued action of \p type. */
    void removeActionsOfType(ActionType type);

    /** Drops the queued action groups whose last command scripts can name attacks or casts. */
    void discardHostileActionGroups();
    /**
     * Queues \p action at the back, in \p group; without one, in the group of
     * the record it was restored from, or else in a new group.
     */
    void addAction(std::shared_ptr<Action> action, std::optional<int> group = std::nullopt);
    /** Queues \p action at the front, choosing its group as addAction() does. */
    void addActionOnTop(std::shared_ptr<Action> action, std::optional<int> group = std::nullopt);
    /** Queues \p action just before \p parent, in its group; returns false when \p parent is not queued. */
    bool addActionBefore(const Action &parent, std::shared_ptr<Action> action);
    /** Drops the actions queued directly behind one, while they match. */
    void removeActionsBehind(const Action &action, const std::function<bool(const Action &)> &matches);
    /**
     * Clears the action groups of the queued actions \p matches picks: each
     * clearable action of such a group is cancelled and removed.
     */
    void cancelActionGroups(const std::function<bool(const Action &)> &matches);
    bool hasOrdinaryActionsPending() const { return !_actions.nodes.empty(); }
    uint32_t currentSerializedActionId() const {
        return _actions.nodes.empty() ? static_cast<uint32_t>(SavedActionType::Invalid) : _actions.nodes.front()->actionId;
    }
    /**
     * What scripts see the object doing (GetCurrentAction): among the actions
     * of the group at the head of its queue, the action running left out, the
     * script number of the last one scripts can name; ACTION_INVALID when none
     * can be named, ACTION_QUEUEEMPTY when nothing waits.
     */
    int currentScriptAction() const;
    /** Posts action as a module timed event that runs as this object when due. */
    void delayAction(std::shared_ptr<Action> action, float seconds);

    bool hasUserActionsPending(const Action *excluded = nullptr) const;

    std::shared_ptr<Action> getCurrentAction() const;

    const OrdinaryActionQueue &actions() const { return _actions; }
    std::vector<SavedActionRecord> saveActionSnapshot() const;
    /** Puts a node back at the head of the queue, in a new group. */
    void requeueActionNode(const OrdinaryActionQueue::Node &node);
    /** Moves a queued node behind every other queued node. */
    void moveActionNodeToBack(const OrdinaryActionQueue::Node &node);

    /** Drop live execution and object bindings after their Area was captured. */
    void retireAreaRuntimeState(
        const std::set<const Object *> &retainedObjects);

    // END Actions

    // Combat

    uint32_t getLastHostileActor() const;

    /**
     * Record \p actor as the last hostile actor. Unless \p force is set, a
     * plot receiver, a non-creature, a dead actor or the receiver itself
     * clears it, and an id that names no object, or a member of the
     * receiver's own faction, leaves it unchanged.
     */
    void setLastHostileActor(uint32_t actor, bool force = false);
    int forceAlwaysUpdate() const { return _forceAlwaysUpdate; }
    void setForceAlwaysUpdate(int value) { _forceAlwaysUpdate = value; }

    uint32_t getLastDamager() const;
    uint32_t getLastKiller() const { return _lastKillerId; }
    void setLastKiller(uint32_t id) { _lastKillerId = id; }
    void setLastDamager(const std::shared_ptr<Object> &damager);
    /** The last damage the object took of the types in \p damageTypeFlag. */
    int getLastDamageAmountByType(int damageTypeFlag) const;
    /** The total of the last damage the object took. */
    int getTotalDamageDealt() const;
    void setLastDamageAmounts(const std::array<int, 15> &amounts) {
        _lastDamageAmounts = amounts;
    }

    // END Combat

    // Local variables

    bool getLocalBoolean(int index) const;
    int getLocalNumber(int index) const;

    const std::map<int, bool> &localBooleans() const { return _localBooleans; }
    const std::map<int, int> &localNumbers() const { return _localNumbers; }
    void deserializeRuntimeState(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void bindSavedRuntimeState();
    void publishSavedRuntimeState();
    const std::vector<EffectInstance> &savedEffects() const { return _savedEffects; }
    const SavedActionQueue &savedActionQueue() const { return _savedActionQueue; }
    bool hasPublishedSavedRuntimeState() const { return _savedRuntimePublished; }
    /** Saved state has been read for the object and is not yet published. */
    bool isRestoringSavedRuntime() const { return _savedRuntimeParsed && !_savedRuntimePublished; }

    void captureSaveRecord(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        SaveRecordOrigin origin = {});
    const std::optional<SaveRecordProvenance> &saveRecordProvenance() const {
        return _saveRecordProvenance;
    }
    std::optional<SerializedObjectIdentity> serializedObjectIdentity() const {
        return _saveRecordProvenance
                   ? _saveRecordProvenance->identity
                   : std::nullopt;
    }
    void assignSerializedObjectIdentity(
        const SerializedObjectIdentity &identity);

    virtual void resolveSavedReferences(
        const std::function<std::shared_ptr<Object>(uint32_t)> &resolver);
    std::shared_ptr<Object> savedReference(std::string_view field) const;



    void setLocalBoolean(int index, bool value);
    void setLocalNumber(int index, int value);

    // END Local variables

    // Scripts

    const std::string &getOnHeartbeat() const { return _onHeartbeat; }
    const std::string &getOnUserDefined() const { return _onUserDefined; }
    void setOnUserDefined(std::string onUserDefined) { _onUserDefined = std::move(onUserDefined); }

    /**
     * Drop this object's OnHeartbeat script, leaving its other event scripts
     * alone, so its heartbeats run nothing. Used by the KotOR II
     * RemoveHeartbeat routine once a heartbeat script has done its one-off
     * work.
     */
    void clearOnHeartbeat() { _onHeartbeat.clear(); }

    // END Scripts

protected:
    friend class Game;
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;

    /** An item taken out of this object's inventory goes to other. */
    void handOverItem(const std::shared_ptr<Item> &item, Object &other);

    /**
     * Drops a node of a removed action group, bypassing commandability,
     * clearability and callbacks.
     */
    void discardGroupNode(const OrdinaryActionQueue::Node &node);

    /** Whether a heartbeat interval has passed since a world time; only the time part of the difference counts. */
    bool isHeartbeatDueSince(uint32_t day, uint32_t time) const;
    void stampHeartbeat();
    /**
     * Stamps a due heartbeat and runs the script unless it is the first check
     * or the object cannot run it.
     */
    void updateStampedHeartbeat(const std::string &script, bool runnable = true);

    /**
     * Record event 15 on a door or placeable: the attacker, the hostile actor,
     * and the attacker's revealed state and last weapon.
     */
    void recordReceivedAttack(uint32_t attackerId, int weaponAttackType);

    // Add one privately constructed Item to a candidate owned graph using the
    // same stacking rules as runtime inventory insertion. Authoritative saved
    // identities can be preserved as distinct records during restoration.
    std::shared_ptr<Item> appendOwnedItemCandidate(
        std::vector<std::shared_ptr<Item>> &items,
        const std::shared_ptr<Item> &item,
        bool preserveSerializedIdentities);
    /** Atomically replace canonical effect state and run exact lifecycle hooks. */
    void replaceEffectState(std::deque<EffectInstance> replacement) noexcept;

    uint32_t _id;
    RuntimeState _runtimeState {RuntimeState::Constructing};
    uint64_t _runtimeIncarnation {0};
    ObjectType _type;
    std::string _sceneName;
    Game &_game;
    ServicesView &_services;

    // Serializable
    std::string _tag;
    std::string _blueprintResRef;
    std::string _conversation;
    std::string _onHeartbeat;
    std::string _onUserDefined;
    bool _minOneHP {false};
    bool _plot {false};
    bool _commandable {true};
    bool _interruptable {false};
    int _hitPoints {0};
    int _maxHitPoints {0};
    int _currentHitPoints {0};
    glm::vec3 _position {0.0f};
    glm::quat _orientation {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<std::shared_ptr<Item>> _items; // FIXME: deserialize
    // END Serializable

    /**
     * World time of the last heartbeat check; none has been stamped while the
     * time is zero.
     */
    uint32_t _heartbeatDay {0};
    uint32_t _heartbeatTime {0};

    std::string _name;
    SpellType _spellCast {SpellType::All};
    SpellScriptContext _spellScriptContext;
    uint32_t _effectSpellId {kNoEffectSpellId}; /**< Only while running a spell-impact script. */
    uint32_t _lastSpellProjectileMilliseconds {0};
    bool _dead {false};
    bool _destroyable {true};
    bool _raiseable {false};
    bool _selectableWhenDead {false};
    bool _deleteNoFade {false};
    uint32_t _fadeOutTime {0};
    bool _keepCorpse {false};
    bool _isInConversation {false};
    RuntimeObjectRef<Object> _dialogOwner;
    glm::mat4 _transform {1.0f};
    bool _visible {true};
    bool _clientPresent {true};
    /** Non-owning; Area detachment/destruction clears this before releasing ownership. */
    Area *_spatialArea {nullptr};
    Room *_room {nullptr};
    std::deque<EffectInstance> _effects;
    /** A visual shown once on the object, and how long it has left to present. */
    struct ShownVisual {
        EffectInstance visual;
        float remaining {0.0f};
    };
    std::vector<ShownVisual> _shownVisuals;
    uint64_t _nextEffectApplicationOrder {1};
    /** While a linked package is applied: nothing lasting has been kept yet. */
    bool _linkAwaitingLastingEffect {false};
    /** The immunity to each damage type, by the type's bit. */
    std::array<int, 15> _damageImmunity {};
    std::vector<uint64_t> _removingEffectApplications;
    bool _open {false};
    bool _stunt {false};
    std::string _activeAnimName;
    std::string _feedbackText;
    float _feedbackTextRemaining {0.0f};

    std::shared_ptr<scene::SceneNode> _sceneNode;

    int _itemIndex {0};
    int _effectIndex {0};

    // Actions

    OrdinaryActionQueue _actions;
    std::weak_ptr<ActionQueueNode> _executingActionNode;

    /**
     * One entry of the saved action queue as it was published: the queue node
     * of a restored action, or a saved action that cannot run, kept as it was
     * loaded.
     */
    struct PublishedSavedAction {
        /** The queue node of the restored action. */
        std::weak_ptr<ActionQueueNode> restored;
        /** The saved action that cannot run, saved again unchanged in its place. */
        std::optional<SavedActionRecord> kept;
    };
    /**
     * The published saved action queue, in its saved order. A clearing of the
     * queue drops the kept saved actions with the live ones.
     */
    std::vector<PublishedSavedAction> _publishedSavedActions;

    // END Actions

    RuntimeObjectRef<Object> _lastHostileActor;
    RuntimeObjectRef<Object> _lastDamager;
    uint32_t _lastKillerId {script::kObjectInvalid};
    std::array<int, 15> _lastDamageAmounts;
    std::optional<uint32_t> _savedLastDamagerId;

    // Local variables
    std::map<std::string, uint32_t> _savedReferenceIds;
    std::map<std::string, RuntimeObjectRef<Object>> _savedReferences;
    std::vector<uint8_t> _savedPerceptionData;
    std::vector<EffectInstance> _savedEffects;
    int _forceAlwaysUpdate {0};
    SavedActionQueue _savedActionQueue;
    std::vector<SavedScheduledAction> _savedScheduledActions;
    std::vector<bool> _savedScheduledReferencesBound;
    float _savedScheduledTime {0.0f};
    /** The round hold of a creature owning no round, in seconds, and who holds it. */
    float _savedOwnerHold {0.0f};
    SavedObjectReference _savedOwnerHoldBy;
    /** The creature's saved rounds that no queued command carries. */
    std::vector<SavedCreatureRound> _savedRounds;
    SerializedIdentityContext _savedRuntimeIdentityContext;
    std::vector<bool> _savedActionReferencesBound;
    bool _savedRuntimeParsed {false};
    bool _savedRuntimePublished {false};
    std::optional<SaveRecordProvenance> _saveRecordProvenance;


    std::map<int, bool> _localBooleans;
    std::map<int, int> _localNumbers;

    // END Local variables

    Object(uint32_t id, ObjectType type, std::string sceneName,
           Game &game, ServicesView &services);

    virtual void updateTransform();
    /** The orientation the scene node shows. */
    virtual glm::quat presentedOrientation() const { return _orientation; }
    virtual bool canExecuteActions() const { return true; }

    void deserializeOwnedItems(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        SaveRecordOriginKind originKind,
        bool forceDropable = false,
        std::string originOwner = {});

    // Actions

    void updateActions(float dt);
    void releaseOrientationLock();
    /**
     * Queues an action restored from a save behind the others, in its saved
     * group. It was admitted when it was given, so it is not scheduled into a
     * combat round again.
     *
     * @return the node the action is queued in; none when the object is not live
     */
    OrdinaryActionQueue::Node restoreAction(std::shared_ptr<Action> action);
    /**
     * The group \p action joins: \p group if given, else the group of the
     * record it was restored from, else a new group.
     */
    int chooseActionGroup(const Action &action, std::optional<int> group);
    /**
     * Cancels and removes queued actions, and drops the kept saved actions;
     * see clearAllActions for \p force and \p keepUnclearable.
     */
    void removeActions(bool force, bool keepUnclearable);
    void removeCompletedActions();

    void executeActions(float dt);

    // END Actions

    // Effects

    /** Runs an effect's admission and keeps it in the effect list, ordered by saved type. */
    bool admitEffect(EffectInstance effect);
    void updateEffects(float dt);
    /** Runs each visual shown once, and ends those whose time is up. */
    void updateShownVisuals(float dt);
    virtual void onEffectsRestored() {}

    int applyDamageToHitPoints(int amount, int currentHitPoints);

    // END Effects
};

} // namespace game

} // namespace reone
