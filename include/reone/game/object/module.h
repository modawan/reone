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
    // Structural load from records already validated by Game. Fresh-world
    // spawn scripts are deliberately dispatched later via runSpawnScripts(),
    // after the destination has crossed its publication boundary.
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
    void runOnLoadScript();
    void runOnStartScript();
    void runSpawnScripts();

    bool handle(const input::Event &event);
    void update(float dt);

    std::vector<ContextAction> getContextActions(const std::shared_ptr<Object> &object) const;

    // Reputation is directed, so how the player may interact with a creature
    // follows that creature's own view of the party leader, not the reverse
    // relationship: an effect that lowers only the creature's hostility still
    // has to open up conversation. A dead creature is never hostile.
    bool isHostileToPartyLeader(const Creature &creature) const;

    const std::string &name() const { return _name; }

    /**
     * The module's localized name, as authored in the module IFO's Mod_Name.
     * Distinct from name(), which is the module's resource name and is
     * normalized to lower case. Empty when the field resolves to nothing.
     */
    const std::string &localizedName() const { return _localizedName; }

    const ModuleInfo &info() const { return _info; }
    std::shared_ptr<Area> area() const { return _area; }
    Player &player() { return *_player; }
    bool isSaveGame() const { return _isSaveGame; }
    const std::vector<std::shared_ptr<Creature>> &limboCreatures() const { return _limboCreatures; }
    const SavedEventQueue &savedEventQueue() const { return _savedEventQueue; }
    size_t pendingSavedEventCount() const;
    std::vector<SavedEventRecord> saveEventSnapshot() const;
    size_t enqueueSaveEvent(SavedEventRecord event);
    size_t enqueueBoundSaveEvent(
        SavedEventRecord event, bool referencesBound);
    bool cancelSaveEvent(size_t index);

    void deserializeSavedEventQueue(
        const resource::Gff &ifo,
        const SerializedIdentityContext &identityContext);
    void bindSavedEventQueue();
    void publishSavedEventQueue();
    void dispatchDueSavedEvents();

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;

    std::string _name;
    std::string _localizedName;
    ModuleInfo _info;
    std::shared_ptr<Area> _area;
    std::unique_ptr<Player> _player;
    bool _isSaveGame {false};
    std::vector<std::shared_ptr<Creature>> _limboCreatures;
    SavedEventQueue _savedEventQueue;
    std::vector<bool> _savedEventLive;
    std::vector<bool> _savedEventReferencesBound;

    struct PublishedSavedEvent {
        size_t savedIndex {0};
        /**
         * Absolute due time in world milliseconds, composed once from the
         * record's day/time pair when the queue is published. Dispatch then
         * compares clocks without rebuilding a calendar every frame.
         */
        uint64_t dueMilliseconds {0};
        std::shared_ptr<SavedScriptContinuation> continuation;
        bool delivered {false};
    };

    std::vector<PublishedSavedEvent> _publishedSavedEvents;
    bool _savedEventsPublished {false};

    void onCreatureClick(const std::shared_ptr<Creature> &creature);
    void onDoorClick(const std::shared_ptr<Door> &door);
    void onObjectClick(const std::shared_ptr<Object> &object);
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
    void deliverSavedEvent(PublishedSavedEvent &event);

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
