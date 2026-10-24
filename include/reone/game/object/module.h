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

#include <map>
#include <optional>
#include <utility>

#include "reone/graphics/types.h"
#include "reone/input/event.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/parser/gff/ifo.h"
#include "reone/resource/types.h"


#include "../contextaction.h"
#include "../object.h"
#include "../player.h"

#include "area.h"

namespace reone {

namespace scene {

class SceneGraph;

}

namespace game {

struct ModuleInfo {
    std::string entryArea;
    glm::vec3 entryPosition {0.0f};
    float entryFacing {0.0f};
    std::string onModLoad;
    std::string onModStart;
    std::string onActivateItem;
    std::string onAcquireItem;
    std::string onUnacquireItem;
    std::string onEquipItem;
    std::string onPlayerDeath;
    /** The script run when the player's character enters the module. */
    std::string onClientEnter;
    /** The hours the day starts and ends at; the hours outside them are night. */
    int dawnHour {0};
    int duskHour {0};
};

/**
 * What the last item events told the module scripts. Each event overwrites
 * its own fields; nothing here is saved.
 */
struct ModuleItemEvents {
    uint32_t activated {script::kObjectInvalid};
    uint32_t activator {script::kObjectInvalid};
    uint32_t activatedTarget {script::kObjectInvalid};
    glm::vec3 activatedPosition {0.0f};
    uint32_t acquired {script::kObjectInvalid};
    uint32_t acquiredFrom {script::kObjectInvalid};
    uint32_t lost {script::kObjectInvalid};
    uint32_t lostBy {script::kObjectInvalid};
    uint32_t equipped {script::kObjectInvalid};
};

class Door;
class Placeable;
class SavedScriptContinuation;
class ModuleSnapshotBuilder;

class Module : public Object {
public:
    Module(
        uint32_t id,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Module,
            "",
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Module;
    }

    void load(std::string name, const resource::Gff &ifo, bool restoreSavedWorld = false);
    /**
     * Structural load from records already validated by Game. The creatures
     * run their creation scripts on the area's first update.
     */
    void load(
        std::string name,
        const resource::Gff &ifo,
        const resource::Gff &are,
        const resource::Gff &git,
        bool restoreSavedWorld = false);
    void activate();
    void loadParty(
        const std::string &entry = "",
        bool preserveSavedPlacement = false);
    /**
     * Announces that the module has finished loading. Its load script runs
     * when the event is due, after the entries already queued, and the player
     * then enters the area.
     */
    void signalLoaded();
    void runOnStartScript();

    bool handle(const input::Event &event);
    void update(float dt);
    /** The module's and its area's actions run with the clock stopped. */
    void runObjectActions();
    /**
     * A companion or puppet that stands outside the area runs its creation
     * script on the first frame after it comes into being, as one in the area
     * does, and also while the game is paused or a menu is open.
     */
    void runSpawnScriptsOutsideArea();

    std::vector<ContextAction> getContextActions(const std::shared_ptr<Object> &object) const;
    /** The droid power the leader can use against a mine (TSL), by the mine's type; none in KotOR. */
    std::shared_ptr<Spell> mineForcePower(const Trigger &trigger, const Creature &leader) const;

    // Reputation is directed, so how the player may interact with a creature
    // follows that creature's own view of the party leader, not the reverse
    // relationship: an effect that lowers only the creature's hostility still
    // has to open up conversation. A dead creature is never hostile.
    bool isHostileToPartyLeader(const Creature &creature) const;

    const std::string &name() const { return _name; }
    const ModuleItemEvents &itemEvents() const { return _itemEvents; }
    /** The party member whose death last reached the module's player-death script. */
    uint32_t lastPlayerDied() const { return _lastPlayerDied; }
    /** The player's character as it last entered the module. */
    uint32_t lastEnteredId() const { return _lastEnteredId; }

    /**
     * The module's localized name, as authored in the module IFO's Mod_Name.
     * Distinct from name(), which is the module's resource name and is
     * normalized to lower case. Empty when the field resolves to nothing.
     */
    const std::string &localizedName() const { return _localizedName; }

    const ModuleInfo &info() const { return _info; }
    std::shared_ptr<Area> area() const { return _area; }
    Player &player() { return *_player; }
    bool isPlayerMoving() const { return _player && _player->isMoving(); }
    bool isSaveGame() const { return _isSaveGame; }
    const std::vector<std::shared_ptr<Creature>> &limboCreatures() const { return _limboCreatures; }
    /**
     * Diagnostic snapshot of pending records; runtime handles are not offsets
     * into this compact, serialized-order view.
     */
    SavedEventQueue savedEventQueue() const { return SavedEventQueue {saveEventSnapshot()}; }
    size_t pendingSavedEventCount() const;
    std::vector<SavedEventRecord> saveEventSnapshot() const;
    /**
     * Queues a timed module event. A command is the live continuation of a
     * DelayCommand: it runs as the event's target when the event is due, and
     * its script situation is exported only when the queue is saved.
     */
    size_t enqueueSaveEvent(
        SavedEventRecord event, std::shared_ptr<Action> command = nullptr,
        bool keepsCallerFade = false);
    size_t enqueueBoundSaveEvent(
        SavedEventRecord event, bool targetBound,
        std::shared_ptr<Action> command = nullptr,
        bool keepsCallerFade = false);
    bool cancelSaveEvent(size_t index);

    void deserializeSavedEventQueue(
        const resource::Gff &ifo,
        const SerializedIdentityContext &identityContext);
    void bindSavedEventQueue();
    void publishSavedEventQueue();
    /** Shows again the projectiles the loaded save had in flight. */
    void restoreProjectilePresentations();
    void dispatchDueSavedEvents();
    /** Withdraws the death fade queued for the object; a script's destruction stays. */
    void cancelObjectDestruction(const Object &object);
    /**
     * Drops every event still pending for target, whatever its kind: an
     * object that ceases to exist receives none of them, and none is saved.
     */
    void dropPendingEvents(const Object &target);

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;

    std::string _name;
    std::string _localizedName;
    ModuleInfo _info;
    ModuleItemEvents _itemEvents;
    uint32_t _lastPlayerDied {script::kObjectInvalid};
    uint32_t _lastEnteredId {script::kObjectInvalid};
    std::shared_ptr<Area> _area;
    std::unique_ptr<Player> _player;
    bool _isSaveGame {false};
    std::vector<std::shared_ptr<Creature>> _limboCreatures;
    struct PendingSavedEvent {
        SavedEventRecord record;
        /**
         * Delivery looks up only the target. References in the caller or the
         * payload that do not resolve are delivered as invalid objects.
         */
        bool targetBound {false};
        std::optional<uint64_t> publishedDueMilliseconds;
        std::shared_ptr<Action> command;
        /**
         * A script's destruction keeps the fade its call set. Any other
         * destruction, and every one loaded from a save, takes the death fade.
         */
        bool keepsCallerFade {false};
    };
    /**
     * Monotonic module-lifetime handles stay valid when other records retire.
     * Only pending records own storage.
     */
    std::map<size_t, PendingSavedEvent> _pendingSavedEvents;
    size_t _nextSavedEventIndex {0};
    std::vector<SavedProjectile> _savedProjectiles;
    bool _projectilesAwaitingRestore {false};

    struct PublishedSavedEvent {
        size_t savedIndex {0};
        /**
         * Absolute due time in world milliseconds, composed once from the
         * record's day/time pair when the queue is published. Dispatch then
         * compares clocks without rebuilding a calendar every frame.
         */
        uint64_t dueMilliseconds {0};
        std::shared_ptr<SavedScriptContinuation> continuation;
        std::shared_ptr<Action> command;
        bool keepsCallerFade {false};
    };

    using SavedEventDeadline = std::pair<uint64_t, size_t>;
    std::map<SavedEventDeadline, PublishedSavedEvent> _publishedSavedEvents;
    bool _savedEventsPublished {false};
    bool _dispatchingSavedEvents {false};

    void onCreatureClick(const std::shared_ptr<Creature> &creature, bool sound = true);
    void onDoorClick(const std::shared_ptr<Door> &door);
    /** A click plays the accepted sound; the default action key does not. */
    void onObjectClick(const std::shared_ptr<Object> &object, bool sound = true);
    void onPlaceableClick(const std::shared_ptr<Placeable> &placeable);

    void getEntryPoint(const std::string &waypoint, glm::vec3 &position, float &facing) const;

    // Loading

    void loadInfo(const resource::generated::IFO &ifo);
    void loadArea(
        const resource::generated::IFO &ifo,
        const resource::Gff &are,
        const resource::Gff &git,
        bool restoreSavedWorld = false);
    void loadPlayer();
    void loadLimboCreatures(const resource::Gff &ifo);
    /**
     * Schedules a pending event for delivery. An event that cannot be
     * delivered, a timed event whose script situation cannot be resumed
     * among them, stays pending without being scheduled, to be saved again.
     */
    void publishSavedEvent(size_t index);
    void deliverSavedEvent(PublishedSavedEvent &event);
    /** Delivers a spell impact, or the weapon on-hit work an item on-hit impact carries. */
    void deliverSpellImpact(const SavedEventRecord &event, Object &target);
    /** Delivers a script event to the door, placeable, encounter, area or object it signals. */
    void deliverSignalEvent(const SavedEventRecord &event, const std::shared_ptr<Object> &target);
    void deliverSpellCastAtSignal(const SavedScriptEvent &event, Object &target);
    /** Tells a creature, door or placeable that it was attacked. */
    void deliverAttackedEvent(const SavedEventRecord &event, Object &target);
    /** Shows a loaded broadcast bolt. */
    void deliverSafeProjectile(const SavedEventRecord &event);
    /**
     * Destroys the target. A destruction a script did not order takes the
     * death fade; \p keepsCallerFade keeps the fade the script set.
     */
    void deliverDestruction(const std::shared_ptr<Object> &target, bool keepsCallerFade);
    void deliverBodyBag(const SavedBodyBag &spawn, Area &area);
    void deliverAppliedEffect(const EffectInstance &effect, Object &target);
    /**
     * Makes the body bag a dead creature or a destroyed placeable leaves and
     * queues its appearance. Returns the bag, or the invalid object when none
     * is left.
     */
    uint32_t spawnBodyBag(Object &source);
    void receiveItemEvent(const SavedScriptEvent &event, const SavedObjectReference &caller);
    void receivePlayerDeathEvent(const SavedObjectReference &caller);
    /**
     * Runs the module's load script, which is never told the module came from
     * a save, then announces the arrival of the controlled creature with the
     * load-from-save answer the loaded signal carries.
     */
    void receiveLoadedSignal(bool loadFromSaveGame);
    void runOnLoadScript();
    /** The player's character enters the module: it is kept as the entrant and the client-enter script runs. */
    void receiveClientEntered(const Creature &player);

    // END Loading

    // User input

    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleKeyDown(const input::KeyEvent &event);

    // END User input

    friend class TestGameModule;
};

} // namespace game

} // namespace reone
