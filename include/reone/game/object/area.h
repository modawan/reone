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
#include <deque>
#include <functional>
#include <optional>

#include "reone/game/minigame.h"
#include "reone/game/transitioncandidate.h"
#include "reone/graphics/aabb.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/types.h"
#include "reone/input/event.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/parser/gff/are.h"
#include "reone/resource/parser/gff/git.h"
#include "reone/resource/types.h"
#include "reone/system/timer.h"

#include "../object.h"
#include "../object/camera/animated.h"
#include "../object/camera/dialog.h"
#include "../object/camera/firstperson.h"
#include "../object/camera/static.h"
#include "../object/camera/thirdperson.h"
#include "../pathfinder.h"
#include "../types.h"

namespace reone {

namespace game {

const float kHeartbeatInterval = 6.0f;

class AreaOfEffect;
class Creature;
class Door;
class Location;
class Object;
class Room;
class Trigger;
class ModuleSnapshotBuilder;

using RoomMap = std::unordered_map<std::string, std::shared_ptr<Room>>;
using ObjectList = std::vector<std::shared_ptr<Object>>;

class Area : public Object {
public:
    struct Grass {
        std::shared_ptr<graphics::Texture> texture;
        float density {0.0f};
        float quadSize {0.0f};
        int ambient {0};
        int diffuse {0};
        glm::vec4 probabilities {0.0f};
    };

    /**
     * Background music, battle music and ambient sound of the area: the
     * tracks, delay and volumes the area file and scripts set, and whether
     * each is playing.
     */
    struct AmbientAudio {
        bool musicPlaying {true};
        int musicDelay {5000};
        int musicDay {2};
        int musicNight {3};
        bool battleMusicPlaying {false};
        int musicBattle {1};
        bool ambientSoundPlaying {true};
        int ambientSoundDay {1};
        int ambientSoundNight {2};
        /** From 0 to 100. */
        int ambientSoundDayVolume {0};
        /** From 0 to 100. */
        int ambientSoundNightVolume {0};
    };

    enum class DirectLine {
        Clear,
        Blocked,
        CreatureBlocked
    };

    /** The outcome of a straight walk. */
    struct DirectLineTest {
        DirectLine result {DirectLine::Clear};
        /** The creature in the way, when only a creature stands there. */
        std::optional<std::reference_wrapper<const Creature>> blocker;
        /** Where the walk is stopped, by a wall or by that creature. */
        glm::vec3 stopPoint {0.0f};
        /** The door whose wall stops the walk, when a door's wall does. */
        std::optional<std::reference_wrapper<const Door>> door;
        /** The normal where the walk is stopped: the wall's, or for a creature the way out from its centre. */
        glm::vec3 normal {0.0f};
    };

    using SearchCriteriaList = std::vector<std::pair<CreatureType, int>>;

    Area(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services);

    ~Area() override;

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Area;
    }

    void load(
        std::string name,
        const resource::Gff &are,
        const resource::Gff &git,
        const SerializedIdentityContext &identityContext);
    void activate();

    bool handle(const input::Event &event);
    void update(float dt) override;
    /**
     * The frame while the world is held: creatures new here run their creation
     * script, then every object runs its actions with the clock stopped.
     */
    void runObjectActions();

    void destroyObject(const Object &object);
    /**
     * Keep a released effect model in the scene until its OmenEmitter01
     * emitter has no live particles.
     */
    void releaseEffectModel(std::shared_ptr<scene::ModelSceneNode> model);
    /**
     * Shows a visual effect once at a point, as presentation only: its
     * location model plays its impact animation there and goes when that
     * ends, and its impact sound plays at the point. Nothing of it is saved.
     */
    void presentVisualAt(int visualEffectId, const glm::vec3 &position);
    /**
     * Shows a hit spark, as presentation only: the impact model of a visual
     * effect at \p hook of \p target's body, turned by \p direction, the
     * direction of the blow, against the target's facing. The spark follows
     * the node's position but not its rotation, plays its impact animation
     * once and goes when that ends. A creature shows at most twelve at once.
     */
    void presentHitSpark(const Creature &target, int visualEffectId, const glm::vec3 &direction,
                         const std::string &hook);
    /** Place a new mine trigger in this area; see Trigger::initMine. */
    std::shared_ptr<Trigger> spawnMine(
        int trapType,
        const glm::vec3 &position,
        const std::shared_ptr<Object> &creator,
        Faction faction,
        int detectDC,
        int disarmDC,
        int ownerDemolitionsSkill);
    /**
     * Place a new area of effect built from an area-of-effect effect: the
     * vfx_persistent row in integer 0, the scripts in strings 0 to 2 replacing
     * the row's, and, at a point, the effect's duration. A carried one stands
     * on its carrier.
     */
    std::shared_ptr<AreaOfEffect> spawnAreaOfEffect(
        const EffectInstance &effect,
        const glm::vec3 &position,
        float facing,
        const std::shared_ptr<Creature> &carrier);
    /**
     * An effect applied at a point: an area of effect is placed there, a
     * visual plays there, and each member of a link is applied there in turn.
     * Other effects do nothing at a point.
     */
    void applyEffectAtLocation(EffectInstance effect, const Location &location);
    /** The creature jumped: every area of effect it carries jumps with it. */
    void jumpCarriedAreaEffects(const Creature &carrier);
    /** Visit the objects whose world X lies in the range, in X order. */
    template <class Visit>
    void forEachObjectInXRange(float minX, float maxX, Visit &&visit) const {
        auto it = std::lower_bound(
            _objectsByX.begin(), _objectsByX.end(), minX,
            [](const Object *entry, float x) { return entry->position().x < x; });
        for (; it != _objectsByX.end() && (*it)->position().x <= maxX; ++it) {
            visit(static_cast<const Object &>(**it));
        }
    }
    /**
     * Visit the objects outward from \p origin in X order, alternately the
     * next one below it and the next one above it. \p visit takes the object
     * and whether it lies above \p origin, and returns false to stop going
     * that way. Nothing is visited when \p origin is not in the area.
     */
    template <class Visit>
    void forEachObjectOutwardInX(const Object &origin, Visit &&visit) const {
        const auto found = std::find(_objectsByX.begin(), _objectsByX.end(), &origin);
        if (found == _objectsByX.end()) {
            return;
        }
        const size_t index = static_cast<size_t>(found - _objectsByX.begin());
        bool down = index > 0;
        bool up = index + 1 < _objectsByX.size();
        for (size_t step = 1; down || up; ++step) {
            if (down) {
                down = visit(static_cast<const Object &>(*_objectsByX[index - step]), false) && index > step;
            }
            if (up) {
                up = visit(static_cast<const Object &>(*_objectsByX[index + step]), true) && index + step + 1 < _objectsByX.size();
            }
        }
    }
    /**
     * The first or next object of one type (or of all types) standing inside
     * a persistent object: an area of effect, a trigger or an encounter. Each
     * persistent object keeps its own place in the X-ordered object list. An
     * area of effect or trigger passes over the dead and party members at no
     * vitality; an encounter answers only the first object of the type, when
     * it stands in its area, and resumes one object further on. Only the
     * active zone holds anything.
     */
    std::optional<std::reference_wrapper<Object>> getObjectInPersistentObject(
        const Object &persistent, bool first, int objectFilter, PersistentZone zone);
    /** Player-party mines in this area. */
    int playerPartyMineCount() const;
    /** Fewer than fifteen player-party mines are set here; the limit holds for every setter. */
    bool playerCanSetMines() const;
    /**
     * The first spot near position where the creature fits. The position
     * itself, dropped onto the ground, is tried first; then, when radius
     * exceeds a metre, square rings around it from one metre out, stepped by
     * the creature's personal space, rows before columns. With clearLine, and
     * the position walkable and clear of any door, a row spot must also be
     * reachable in a straight line from the position, but for other creatures,
     * and no spot may lie across a door from it. Empty when nothing fits.
     */
    std::optional<glm::vec3> computeSafeLocation(
        const glm::vec3 &position, float radius, const Creature &creature, bool clearLine) const;
    /**
     * The first spot in a direction from base where the creature fits. The
     * spot one direction's length along is tried first. Then come rows one,
     * two and more metres along, as many as whole personal spaces fit under
     * radius, row n holding 2n+1 spots a metre apart across the direction,
     * from its left. Last, computeSafeLocation round base. With clearLine, a
     * row spot must also be reachable in a straight line from base, unless
     * the first spot is off walkable ground or among walls.
     */
    std::optional<glm::vec3> computeSafeLocationInDirection(
        const glm::vec3 &base, const glm::vec3 &direction, float radius, const Creature &creature, bool clearLine) const;
    /**
     * Whether the creature fits at a point on the ground: over walkable ground,
     * no blocking walkmesh face within its personal space across its height,
     * and no other creature within their combined creature personal space,
     * other than the dead and party members down at no vitality.
     */
    bool isSafeLocationPoint(const glm::vec3 &point, const Creature &creature) const;
    /** Whether a straight line between two points meets the walls of a door as it stands. */
    bool crossesDoor(const glm::vec3 &from, const glm::vec3 &to) const;
    /**
     * Whether a wall of a door as it stands lies within the creature's
     * personal space of a point on the ground, up to the creature's height.
     */
    bool isNearDoorWall(const glm::vec3 &point, const Creature &creature) const;
    /** The ground height under a point searched down from it, if it stands over walkable ground. */
    std::optional<float> groundHeight(const glm::vec3 &position) const;
    /**
     * Where a creature appearing near center goes: center itself when the
     * creature fits there, otherwise the first ring spot within twenty metres
     * where it fits, no more than thirty metres from center, and in plain
     * sight of center.
     */
    std::optional<glm::vec3> findOpenSpotInSight(const glm::vec3 &center, const Creature &creature) const;
    /**
     * Moves the creatures that no longer fit where they stand once a corpse
     * bag with this collision box lands at center: each is pushed a metre
     * straight away from center, to the first spot within ten metres where it
     * fits.
     */
    void budgeCreatures(const glm::vec3 &center, const graphics::AABB &bounds);
    /**
     * Note a creature's new position in every encounter and area of effect,
     * signalling entries and exits when announce is set.
     */
    void updateSubAreaOccupancy(const std::shared_ptr<Creature> &creature, bool announce = true);

    /**
     * The first creature the mover walks into on a step between two points,
     * other than the dead and party members down at no vitality.
     */
    std::optional<std::reference_wrapper<const Creature>> findBlockingCreature(const Creature &mover, const glm::vec3 &from, const glm::vec3 &to) const;
    /**
     * Whether the creature can walk straight from one point to another. When
     * only another creature stands in the way, that creature is reported. The
     * point where the walk is stopped, by a wall or by that creature, is
     * reported too, and the door, when a door's wall stops it. The normal
     * there is the wall's, or for a creature the way out from its centre
     * through that point. The ignored creature never stands in the way.
     */
    DirectLineTest testDirectLine(
        const Creature &mover,
        const glm::vec3 &from,
        const glm::vec3 &to,
        std::optional<std::reference_wrapper<const Creature>> ignored = std::nullopt) const;
    /**
     * Whether nothing but the observer or the target, when there is one,
     * stands between two eye points, or stands only beyond the far one. A line
     * allowed past a see-through door sees through the first such door it
     * meets.
     */
    bool isEyeLineClear(const glm::vec3 &from, const glm::vec3 &to,
                        std::optional<std::reference_wrapper<const Object>> observer,
                        std::optional<std::reference_wrapper<const Object>> target,
                        bool pastSeeThroughDoor = false) const;
    /**
     * The point a creature makes for to get a distance away from a threat. Of
     * the eight directions around it, the one leading farthest from the threat
     * wins, weighted toward running straight away; a direction whose way is
     * blocked is shortened until it is clear, but a shortened one must not end
     * nearer the threat than the creature stands. With no clear direction the
     * creature stays where it is.
     */
    glm::vec3 computeAwayPoint(const Creature &mover, const glm::vec3 &threat, float distance) const;
    /**
     * A random point within range of a creature that it can walk straight to.
     * A blocked point is brought in by a quarter, or once that would leave it
     * nearer than two and a half metres, tried again at full distance in a
     * new direction. After 22 tries, or once the distance falls under a metre,
     * the creature's own position is the answer.
     */
    glm::vec3 randomDestination(const Creature &mover, int range) const;
    /**
     * A random point for a random walk: up to seven whole metres either way
     * on each axis from its home, on the ground, which the walker can walk
     * straight to from where it stands. A blocked point is brought in by a
     * quarter while it stays at least three tenths of the full distance away;
     * then the same bearing is tried level with the walker at full distance.
     * After 22 tries, or once the distance falls under a metre, there is none.
     */
    std::optional<glm::vec3> randomWalkPoint(const Creature &walker, const glm::vec3 &home) const;
    void initCameras(const glm::vec3 &entryPosition, float entryFacing);

    void onPartyLeaderMoved(bool roomChanged = false);
    void updateRoomVisibility();

    void startDialog(const std::shared_ptr<Object> &object, const std::string &resRef);
    void update3rdPersonCameraFacing();
    void update3rdPersonCameraTarget();
    bool landObject(Object &object);
    void add(const std::shared_ptr<Object> &object);

    /** Keep a moved object in X order; it passes only objects strictly before or after it. */
    void updateObjectSpatialIndex(Object &object);
    /**
     * The first or next object in X order, from minX to maxX, that matches.
     * Every caller shares one cursor; a search that finds nothing leaves it
     * where it was.
     */
    std::optional<std::reference_wrapper<Object>> getObjectInShape(bool first, float minX, float maxX,
                                                                   const std::function<bool(const Object &)> &matches);

    /** How moving turns a creature. */
    enum class MoveFacing {
        Turn, /**< along the step taken, the model following */
        Keep  /**< not at all */
    };

    /**
     * Move a creature a distance along a direction over walkable ground,
     * sliding along other creatures, and turn it as facing asks. Returns
     * whether it moved.
     */
    bool moveCreatureByDistance(const std::shared_ptr<Creature> &creature,
                                const glm::vec2 &direction, float distance,
                                MoveFacing facing = MoveFacing::Turn);
    /**
     * Step a creature to a point it has been found free to walk straight to:
     * onto the ground there, noting its room, the leader's trail, triggers
     * and subareas. False, and no step, when there is no ground at the point.
     */
    bool stepCreatureTo(const std::shared_ptr<Creature> &creature, const glm::vec2 &point,
                        MoveFacing facing = MoveFacing::Turn);

    bool moveCreature(const std::shared_ptr<Creature> &creature, const glm::vec2 &dir, bool run, float dt,
                      float maxDistance = FLT_MAX, MoveFacing facing = MoveFacing::Turn);
    void determineObjectRoom(Object &object);
    /**
     * The TSL Force rating of the first room whose WalkCheck walkmesh lies
     * under a position; 0 in KotOR, over no room, or for a room without an
     * ARE entry.
     */
    int getRoomForceRating(const glm::vec3 &position) const;
    /**
     * The surface material under a point: the topmost face of any material in
     * the room found under it, or 0 when no room lies under it.
     */
    int getSurfaceMaterial(const glm::vec3 &position) const;
    /**
     * The topmost room face of any material but Trigger met going down from
     * z 1000 to z -1000 at a point, in whichever room it lies; false when
     * there is none. Placeables, doors and creatures are not met.
     */
    bool testRoomSurface(const glm::vec2 &point, scene::Collision &outCollision) const;

    bool isUnescapable() const { return _unescapable; }

    bool hasMinigame() const { return _miniGameSpec.has_value(); }
    const MinigameSpec &miniGame() const { return *_miniGameSpec; }

    Object *getObjectAt(int x, int y) const;

    // Presentation-active module transitions, for the destination banner.
    // Read-only: no tenants, scripts or transitions are touched.
    std::vector<TransitionPortal> transitionPresentationPortals() const;
    glm::vec3 getSelectableScreenCoords(const std::shared_ptr<Object> &object, const glm::mat4 &projection, const glm::mat4 &view) const;

    const CameraStyle &camStyleDefault() const { return _camStyleDefault; }
    bool transitionPending() const { return _transitionPending; }
    bool playerRestrictMode() const { return _playerRestrictMode; }
    /** Turning restrict mode on ends the party's stealth. */
    void setPlayerRestrictMode(bool value);
    const ObjectList &objects() const { return _objects; }
    const std::string &localizedName() const { return _localizedName; }
    const RoomMap &rooms() const { return _rooms; }
    const Grass &grass() const { return _grass; }
    const glm::vec3 &ambientColor() const { return _ambientColor; }

    Pathfinder &pathfinder() { return _pathfinder; }

    void setUnescapable(bool value);

    // Music and ambient sound
    //
    // The area owns these settings and tells the area music player of each
    // change; the player follows them while it plays this area.

    const AmbientAudio &ambientAudio() const { return _ambientAudio; }
    void playMusic(bool play);
    void setMusicDelay(int delay);
    void setMusicDayTrack(int track);
    void setMusicNightTrack(int track);
    void playBattleMusic(bool play);
    void setBattleMusicTrack(int track);
    void playAmbientSound(bool play);
    void setAmbientSoundDayTrack(int track);
    void setAmbientSoundNightTrack(int track);
    void setAmbientSoundDayVolume(int volume);
    void setAmbientSoundNightVolume(int volume);

    // END Music and ambient sound

    // Objects

    /**
     * @param appear whether a created creature appears, holding its actions for a while
     */
    std::shared_ptr<Object> createObject(ObjectType type, const std::string &blueprintResRef, const std::shared_ptr<Location> &location, bool appear = false);

    /**
     * End this Area's ownership of an exact still-live runtime Object.
     *
     * This removes Area indexes, Room/Trigger tenancy and presentation
     * attachment without firing authored exit behavior or ending semantic
     * object lifetime. It is the transfer seam for a world object moving into
     * another owning graph.
     */
    bool releaseObject(const std::shared_ptr<Object> &object);

    /** Whether this exact Object is currently owned by this Area. */
    bool isObjectResident(const Object &object) const;

    /** Whether this exact runtime Object is queued for semantic destruction. */
    bool isObjectPendingDestruction(const Object &object) const;

    /** Whether a corpse the area keeps passes picks to this body bag. */
    bool hasCorpseWithBodyBag(uint32_t bagId) const;
    /** The body a corpse bag takes over, out of the scene until the bag is added. */
    std::shared_ptr<scene::ModelSceneNode> takeCorpseBagBody(uint32_t bagId);

    bool isObjectSeen(const Creature &subject, const Object &object) const;
    void refreshPerceptionFor(Creature &creature);
    /**
     * Listen penalty for what lies between two creatures' eyes, or none when
     * no sound reaches from one to the other.
     */
    std::optional<int> soundOcclusion(const Creature &listener, const Creature &source,
                                      const glm::vec3 &listenerEye, const glm::vec3 &sourceEye) const;
    int modListenCheck() const { return _modListenCheck; }
    /** Have a creature look around at once, as when it stops leading the party. */
    void perceiveNow(const std::shared_ptr<Creature> &observer);

    ObjectList &getObjectsByType(ObjectType type);
    std::shared_ptr<Object> getObjectByTag(const std::string &tag, int nth = 0) const;

    /**
     * Step through the creatures of a faction whose player-character flag (0
     * or 1) equals pc. Each faction keeps one cursor, shared by every caller; a
     * step that finds no member leaves it where it was.
     */
    std::shared_ptr<Creature> getFirstFactionMember(Faction faction, int pc);
    std::shared_ptr<Creature> getNextFactionMember(Faction faction, int pc);

    // END Objects

    // Object Search

    /**
     * Find the nth nearest object around a target standing in the area. The
     * target itself is never a result.
     *
     * @param nth a 0-based object index
     */
    std::optional<std::reference_wrapper<const Object>> getNearestObject(const Object &target, int nth, const std::function<bool(const Object &)> &matches) const;

    /**
     * Find the nth nearest object to a position. The first object at or beyond
     * the position along the x axis is never a result.
     *
     * @param nth a 0-based object index
     */
    std::optional<std::reference_wrapper<const Object>> getNearestObjectToLocation(const glm::vec3 &position, int nth, const std::function<bool(const Object &)> &matches) const;

    /**
     * @param nth 0-based index of the creature
     * @return nth nearest creature to the target object, that matches the specified criterias
     */
    std::shared_ptr<Creature> getNearestCreature(const std::shared_ptr<Object> &target, const SearchCriteriaList &criterias, int nth = 0);

    /**
     * @param nth 0-based index of the creature
     * @return nth nearest creature to the location, that matches the specified criterias
     */
    std::shared_ptr<Creature> getNearestCreatureToLocation(const Location &location, const SearchCriteriaList &criterias, int nth = 0);

    // END Object Search

    // Cameras

    Camera *getCamera(CameraType type);

    void setStaticCamera(int cameraId);
    /** Switch the third-person camera between the area style and the combat style and behavior. */
    void setThirdPersonCombat(bool combat);

    template <class T>
    T *getCamera(CameraType type) {
        return static_cast<T *>(getCamera(type));
    };

    // END Cameras

    // Party

    /** End one retained creature's current Area lifetime. */
    void retireCreatureAreaRuntime(const std::shared_ptr<Creature> &creature);
    void retirePartyMemberAreaRuntime(const std::shared_ptr<Creature> &member);
    void loadParty(
        const glm::vec3 &position,
        float facing,
        bool preserveSavedPlacement = false);
    /** End every session-owned creature's residency in this Area. */
    void retirePartyAreaRuntime();
    /** Bring an actor taking control into this Area at the given spot and announce it. */
    void placeControlledCreature(
        const std::shared_ptr<Creature> &creature,
        const glm::vec3 &position,
        float facing);
    /** Reconcile/position the selected party without ending this Area lifetime. */
    void repositionParty(const glm::vec3 &position, float facing);
    void repositionParty();

    // END Party

    // Perception

    void updatePerception(float dt);

    // END Perception

    // Object Selection

    void hilightObject(std::shared_ptr<Object> object);
    void selectObject(std::shared_ptr<Object> object, bool force = false);

    std::shared_ptr<Object> hilightedObject() const { return _hilightedObject; }
    std::shared_ptr<Object> selectedObject() const { return _selectedObject; }
    bool isSelectionForced() const { return _forceSelection; }

    // END Object Selection

    // Stealth

    bool isStealthXPEnabled() const { return _stealthXPEnabled; }

    int maxStealthXP() const { return _maxStealthXP; }
    int currentStealthXP() const { return _currentStealthXP; }
    int stealthXPDecrement() const { return _stealthXPDecrement; }

    void setStealthXPEnabled(bool value);
    void setMaxStealthXP(int value);
    void setCurrentStealthXP(int value);
    void setStealthXPDecrement(int value);

    // END Stealth

    // Fog

    bool fogEnabled() const { return _fogEnabled; }
    float fogNear() const { return _fogNear; }
    float fogFar() const { return _fogFar; }
    /** The fog colour, red, green and blue from 0 to 1. */
    glm::vec3 fogColor() const { return resource::Gff::colorFromUint32(_sunFogColor); }

    // END Fog

    // Scripts

    /** Runs the creation script of every creature here that has not run it yet. */
    void runSpawnScripts();
    void runOnExitScript();
    /**
     * Announce a creature that came into the area: the area's OnEnter runs
     * for it when the event is due.
     */
    void signalEntered(Creature &creature);
    /**
     * Run OnEnter for the entering creature. While it runs, the load-from-save
     * answer is the one the event captured; the live answer is restored after.
     */
    void receiveEnteredSignal(const std::shared_ptr<Object> &entering, bool loadFromSaveGame);
    /** The object the area's last enter event was for, kept until the next one. */
    uint32_t lastEnteredId() const { return _lastEnteredId; }
    /** The object the area's last exit event was for, kept until the next one. */
    uint32_t lastLeftId() const { return _lastLeftId; }

    // END Scripts

    // Listeners

    /**
     * The speaker's words reach every object in the area, the speaker aside,
     * that listens, is within the volume's range, perceives the speaker as the
     * volume needs and has a pattern that matches. Each such object is sent
     * one conversation event with its first matching pattern.
     */
    void broadcastDialog(Object &speaker, const std::string &message, int talkVolume);

    // END Listeners

    // Scene

    scene::ISceneGraph &graph();

    // END Scene

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;
    std::string _sceneName;

    Pathfinder _pathfinder;
    std::string _localizedName;
    resource::generated::ARE_Map _map;
    RoomMap _rooms;
    /**
     * Room lookup is first walkable hit in LYT order, not nearest
     * visible room. Keep that order separately from the name index.
     */
    std::vector<Room *> _roomOrder;
    std::unordered_map<std::string, int> _roomForceRatings;
    resource::Visibility _visibility;
    CameraStyle _camStyleDefault;
    CameraStyle _camStyleCombat;
    AmbientAudio _ambientAudio;
    bool _playerRestrictMode {false};
    uint32_t _lastEnteredId {script::kObjectInvalid};
    uint32_t _lastLeftId {script::kObjectInvalid};
    bool _transitionPending {false};
    /** The ids of the pending transition's next and current steps. */
    uint8_t _transitionPendingNextId {0};
    uint8_t _transitionPendingCurrentId {0};
    bool _unescapable {false};
    /** TSL: whether transit out of the area is disabled, as the area file says. */
    bool _transitDisabled {false};
    Grass _grass;
    std::optional<MinigameSpec> _miniGameSpec;
    glm::vec3 _ambientColor {0.0f};
    /** Sound finds its way around the walls of an interior or underground area. */
    bool _interior {false};
    bool _underground {false};
    int _modListenCheck {0};
    struct PerceptionNotice {
        std::shared_ptr<Creature> observer;
        std::shared_ptr<Object> target;
        PerceptionEvent event;
    };
    std::vector<PerceptionNotice> _perceptionNotices;
    bool _perceptionPassActive {false};
    /** The IDs an observer already perceives, kept between perception passes to reuse its storage. */
    std::vector<uint32_t> _perceivedIds;
    std::shared_ptr<Object> _hilightedObject;
    std::shared_ptr<Object> _selectedObject;
    bool _forceSelection {false};

    // Scripts

    std::string _onEnter;
    std::string _onExit;

    // END Scripts

    // Cameras

    std::shared_ptr<FirstPersonCamera> _firstPersonCamera;
    std::shared_ptr<ThirdPersonCamera> _thirdPersonCamera;
    std::shared_ptr<DialogCamera> _dialogCamera;
    std::shared_ptr<AnimatedCamera> _animatedCamera;
    StaticCamera *_staticCamera {nullptr};

    // END Cameras

    // Objects

    ObjectList _objects;
    /** Every object in the area, in X order. */
    std::vector<Object *> _objectsByX;
    /** Where the last shape query found its object: every shape query shares this cursor. */
    std::optional<size_t> _shapeQueryIndex;
    /** Where each persistent object's resident query resumes, by object id. */
    std::map<uint32_t, size_t> _persistentQueryIndex;

    /** Where the next faction member step resumes in the faction's members. */
    std::unordered_map<Faction, size_t> _factionMemberCursors;

    /**
     * The first creature with the player-character flag from a place on in
     * the faction's members, which are in the order they joined.
     */
    std::shared_ptr<Creature> findFactionMember(Faction faction, int pc, size_t from);

    /** Place an object in X order, before those at the same X, and make this its area. */
    void addToSpatialIndex(Object &object);
    void removeFromSpatialIndex(Object &object);

    std::unordered_map<ObjectType, ObjectList> _objectsByType;
    std::unordered_map<std::string, ObjectList> _objectsByTag;
    std::set<uint32_t> _objectsToDestroy;

    /**
     * The bodies of destroyed objects stay in the scene after the objects are
     * gone: a body waits and then fades over two seconds, and the area keeps
     * the last three corpses, fading the oldest when a fourth arrives. A kept
     * corpse with a body bag passes picks to it, and the bag shows when the
     * corpse fades. A corpse bag instead takes over its creature's body.
     */
    struct ReleasedBody {
        std::shared_ptr<Object> owner;
        std::shared_ptr<scene::ModelSceneNode> model;
        /** The room the body stood in, whose visibility it follows. */
        std::optional<std::reference_wrapper<const Room>> room;
        float delay {0.0f};
        float hold {0.0f};
        float elapsed {0.0f};
        uint32_t bodyBagId {script::kObjectInvalid};
    };
    std::vector<ReleasedBody> _fadingBodies;
    std::deque<ReleasedBody> _corpses;
    std::vector<ReleasedBody> _corpseBagBodies;
    std::vector<std::shared_ptr<scene::ModelSceneNode>> _releasedEffectModels;
    std::vector<std::shared_ptr<scene::ModelSceneNode>> _presentedVisuals;
    struct HitSpark {
        std::weak_ptr<scene::ModelSceneNode> body;
        std::string hook;
        glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
        std::shared_ptr<scene::ModelSceneNode> model;
    };
    std::vector<HitSpark> _hitSparks;

    /** Keep the body of a destroyed object in the scene, as a fading body, a kept corpse or a corpse bag's body. */
    void releaseDestroyedBody(const std::shared_ptr<Object> &object, std::optional<std::reference_wrapper<const Room>> room);
    /** Fade and remove released bodies and effect models, and show kept ones with their rooms. */
    void updateReleasedPresentation(float dt);

    // END Objects

    // Stealth

    bool _stealthXPEnabled {false};
    int _maxStealthXP {0};
    int _currentStealthXP {0};
    int _stealthXPDecrement {0};

    // END Stealth

    // Fog

    bool _fogEnabled {false};
    float _fogNear {0.0f};
    float _fogFar {0.0f};
    /** The fog colour as area files record it: red, green and blue bytes from the lowest. */
    uint32_t _sunFogColor {0};

    // END Fog

    void init();

    void loadLYT();
    void loadVIS();
    void applySceneProperties();
    void attachRoomToSceneGraph(Room &room);
    void attachObjectToSceneGraph(const std::shared_ptr<Object> &object);
    void detachObjectRuntime(const std::shared_ptr<Object> &object);

    void doDestroyObject(uint32_t objectId, bool destroyRuntimeObject = true);
    void doDestroyObjects();
    void updateVisibility();
    /** The first room in order with walkable surface under or over a point. */
    std::optional<std::reference_wrapper<const Room>> getRoomUnder(const glm::vec3 &position) const;

    void loadPartyMember(
        const std::shared_ptr<Creature> &member,
        int index,
        bool preserveSavedPlacement);
    void repositionPartyMember(
        const std::shared_ptr<Creature> &member,
        int index);

    struct CreatureCollision {
        std::optional<std::reference_wrapper<const Creature>> creature;
        float time {0.0f};
        glm::vec2 normal {0.0f};
    };

    bool findCreatureCollision(
        const Creature &creature,
        const glm::vec3 &origin,
        const glm::vec3 &destination,
        CreatureCollision &outCollision,
        std::optional<std::reference_wrapper<const Creature>> ignoredCreature = std::nullopt) const;

    /**
     * Visit ground spots on square rings around center, starting halfWidth
     * out and widening by step while continues(halfWidth) holds: along the
     * bottom and top rows, then up the left and right columns. The first spot
     * accept returns wins.
     */
    std::optional<glm::vec3> searchSquareRings(
        const glm::vec3 &center,
        float halfWidth,
        float step,
        const std::function<bool(float)> &continues,
        const std::function<std::optional<glm::vec3>(const glm::vec3 &, bool)> &accept) const;

    /** Each creature's perception pass that is due, or every creature's with all. */
    void updatePerceptionPasses(float dt, bool all);
    /**
     * One creature looks at the party members, with partyTargets, and on a
     * full pass at those it perceives and the others in range.
     */
    void updatePerceptionPass(const std::shared_ptr<Creature> &observer, bool partyTargets, bool fullPass);
    void updatePerceptionPair(
        const std::shared_ptr<Creature> &observer,
        const std::shared_ptr<Creature> &target);
    /** Post a perception notice for the observer, to run after the pass. */
    void notifyPerception(const std::shared_ptr<Creature> &observer, const std::shared_ptr<Object> &target,
                          PerceptionEvent event);
    /** Run the notices of the pass that found them. */
    void dispatchPerceptionNotices();
    /** Mark the creatures the player is aware of. */
    void updateClientPresence();
    void updateObjectSelection();

    /**
     * The nth nearest creature to origin that matches the criteria, the
     * excluded object never among them. Reputation and perception criteria
     * are judged by the searching object.
     */
    std::shared_ptr<Creature> findNearestCreature(const glm::vec3 &origin, const Object &excluded,
                                                  std::optional<std::reference_wrapper<const Object>> searching,
                                                  const SearchCriteriaList &criterias, int nth);
    /**
     * The nth nearest object to origin that matches, walking out from the
     * base entry of the x-ordered objects; see getNearestObject.
     */
    std::optional<std::reference_wrapper<const Object>> findNearestObject(const glm::vec3 &origin, size_t base, int nth, const std::function<bool(const Object &)> &matches) const;
    bool matchesCriterias(const Creature &creature, const SearchCriteriaList &criterias,
                          std::optional<std::reference_wrapper<const Object>> searching) const;

    /**
     * Make room visibility symmetric when a VIS file supplies only one direction of a room
     * relationship.
     */
    resource::Visibility fixVisibility(const resource::Visibility &visiblity);

    void checkTriggersIntersection(const std::shared_ptr<Object> &triggerrer, bool fireTransitions = true);

    // Fire OnEnter for non-transition triggers the party leader currently
    // occupies (so triggers fire when the leader is placed/spawned inside them,
    // not only when moving across the boundary). Module-transition triggers are
    // left to movement-based firing to avoid spawn bounce.
    void updateLeaderTriggerOccupancy();

    // Loading ARE

    void loadARE(const resource::generated::ARE &are);

    void loadCameraStyle(const resource::generated::ARE &are);
    void loadAmbientColor(const resource::generated::ARE &are);
    void loadScripts(const resource::generated::ARE &are);
    void loadMap(const resource::generated::ARE &are);
    void loadStealthXP(const resource::generated::ARE &are);
    void loadHearing(const resource::generated::ARE &are);
    void loadGrass(const resource::generated::ARE &are);
    void loadFog(const resource::generated::ARE &are);
    void loadMiniGame(const resource::generated::ARE &are);

    // END Loading ARE

    // Loading GIT

    void loadGIT(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    void loadProperties(const resource::Gff &git);
    void loadCreatures(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadDoors(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadPlaceables(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadWaypoints(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadTriggers(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadSounds(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadCameras(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadEncounters(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadStores(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadItems(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadAreaEffects(const resource::Gff &gff, const SerializedIdentityContext &identityContext);

    // END Loading GIT

    // User input

    bool handleKeyDown(const input::KeyEvent &event);

    // END User input
};

} // namespace game

} // namespace reone
