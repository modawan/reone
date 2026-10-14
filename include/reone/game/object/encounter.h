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

#include <set>

#include "reone/resource/gff.h"

#include "../object.h"

namespace reone {

namespace game {

class Creature;

/**
 * A region that spawns a wave of creatures when a creature hostile to its
 * faction is inside it, and may re-arm once the wave has been destroyed.
 */
class Encounter : public Object {
public:
    /** Runtime state, saved with the encounter. */
    struct SavedRuntimeState {
        int32_t areaListMaxSize {0};
        int32_t areaListSize {0};
        /** AreaList as read; bookkeeping only, written back unchanged. */
        std::vector<uint32_t> areaObjectIds;
        float areaPoints {0.0f};
        int32_t currentSpawns {0};
        int32_t customScriptId {0};
        bool exhausted {false};
        uint32_t heartbeatDay {0};
        uint32_t heartbeatTime {0};
        /** LastEntered and LastLeft as read; the live references are lastEntered() and lastLeft(). */
        uint32_t lastEntered {0};
        uint32_t lastLeft {0};
        /** When the last wave was destroyed, if the encounter resets. */
        uint32_t lastSpawnDay {0};
        uint32_t lastSpawnTime {0};
        /** Spawned creatures still counted against the wave. */
        int32_t numberSpawned {0};
        float spawnPoolActive {0.0f};
        /** A wave has been chosen; cleared when it is destroyed. */
        bool started {false};
    };

    /** A chosen creature still to be spawned. */
    struct PendingSpawn {
        std::string resRef;
        float challengeRating {0.0f};
    };

    Encounter(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Encounter,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Encounter;
    }

    void update(float dt) override;

    Faction faction() const { return _faction; }

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    const SavedRuntimeState &savedRuntimeState() const { return _savedRuntimeState; }
    std::shared_ptr<Object> savedAreaObject(size_t index) const;
    const std::vector<PendingSpawn> &pendingSpawns() const { return _pendingSpawns; }

    bool isActive() const { return _active; }
    /** Activating the encounter spawns at once when a hostile creature is already inside. */
    void setActive(bool active);
    int respawns() const { return _respawns; }
    void setRespawns(int respawns) { _respawns = respawns; }
    int currentSpawns() const { return _savedRuntimeState.currentSpawns; }
    void setCurrentSpawns(int spawns) { _savedRuntimeState.currentSpawns = spawns; }
    /** The difficulty value of the encdifficulty row the encounter was given. */
    int difficulty() const { return _difficulty; }
    void setDifficulty(int difficultyIndex);

    std::shared_ptr<Object> lastEntered() const { return savedReference(kLastEnteredReference); }
    std::shared_ptr<Object> lastLeft() const { return savedReference(kLastLeftReference); }

    /** Outline of the encounter's area, relative to its position. */
    const std::vector<glm::vec3> &geometry() const { return _geometry; }
    /** Whether a point lies within the encounter's area, in the XY plane. */
    bool contains(const glm::vec3 &point) const;
    /**
     * Note where a creature now stands. Entering or leaving the area signals
     * the encounter when announce is set; entering may start a wave.
     */
    void updateOccupancy(const std::shared_ptr<Creature> &creature, bool announce);
    /** Forget a creature leaving the world, without running OnExit. */
    void forgetOccupant(uint32_t objectId) { _occupants.erase(objectId); }
    /**
     * One of the encounter's creatures no longer counts against its wave. The
     * last one exhausts the encounter: it deactivates, may be re-armed later,
     * and signals itself to run OnExhausted.
     */
    void removeSpawnedCreature(float challengeRating);
    void receiveEnteredSignal(const std::shared_ptr<Object> &entering);
    void receiveExitedSignal(const std::shared_ptr<Object> &exiting);
    void receiveExhaustedSignal();
    void receiveHeartbeatSignal();

protected:
    bool canExecuteActions() const override { return _active; }

private:
    friend class ModuleSnapshotBuilder;
    struct SpawnPoint {
        glm::vec3 position {0.0f};
        float orientation {0.0f};
    };

    struct EncounterCreature {
        uint32_t _appearance {0};
        float _cr {0.0f};
        std::string _resRef;
        bool _singleSpawn {false};
        int32_t _guaranteedCount {0};
        /** Choice marks for the first title's wave budget. */
        bool _used {false};
        bool _tried {false};

        PendingSpawn pendingSpawn() const { return PendingSpawn {_resRef, _cr}; }
    };

    static constexpr const char *kLastEnteredReference = "LastEntered";
    static constexpr const char *kLastLeftReference = "LastLeft";

    // Serializable
    resource::LocString _locName;
    bool _active {false};
    bool _reset {false};
    int32_t _resetTime {0};
    int32_t _respawns {0};
    int32_t _spawnOption {0};
    int32_t _maxCreatures {0};
    int32_t _recCreatures {0};
    bool _playerOnly {false};
    Faction _faction {Faction::Invalid};
    int32_t _difficultyIndex {-1};
    int32_t _difficulty {0};

    std::string _onEntered;
    std::string _onExit;
    std::string _onExhausted;

    std::vector<EncounterCreature> _creatures;
    std::vector<glm::vec3> _geometry;
    std::vector<SpawnPoint> _spawnPoints;
    std::vector<PendingSpawn> _pendingSpawns;
    SavedRuntimeState _savedRuntimeState;
    // END Serializable

    std::set<uint32_t> _occupants;
    /** The sequel's spawn points each take an equal share of a wave in turn. */
    int _spawnPointIndex {0};
    int _spawnsAtPoint {0};
    int _spawnsPerPoint {0};
    /** The first title's wave budget, in creature points. */
    float _spawnPool {0.0f};

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void deserializeSavedRuntimeState(const resource::Gff &gff);
    void deserializeCreatures(const resource::Gff &gff);
    void deserializeGeometry(const resource::Gff &gff);
    void deserializeSpawnPoints(const resource::Gff &gff);
    void deserializePendingSpawns(const resource::Gff &gff);

    /** Choose a wave, once per activation, when a hostile creature stands inside. */
    void spawnIfAppropriate(uint32_t triggeringId);
    /** Whether a living creature hostile to the encounter stands inside it. */
    bool updateActivation(uint32_t triggeringId);
    bool isHostileTo(const Creature &creature) const;
    /** The sequel's wave: a count between the recommended and the maximum. */
    void chooseWaveByCount();
    /** Place the next chosen creature, at most one an update. */
    void spawnNext();
    /** Re-arm a continuous encounter that resets once its reset time has passed. */
    void updateRespawn();
    void updateHeartbeat();

    // The first title's wave budget

    /** A creature's points in the budget for its challenge rating. */
    float creaturePoints(float challengeRating) const;
    /** The points of the cheapest creature. */
    float minimumCreaturePoints() const;
    void addToSpawnPool();
    /** The points of the hostile creatures around whoever entered. */
    float tallyHostilePoints() const;
    /** The first title's wave: spent from the budget, then filled up to the recommended count. */
    void chooseWaveByPoints();
    void chooseInitialWave(std::vector<PendingSpawn> &wave);
    void replaceWithCheaper(std::vector<PendingSpawn> &wave, size_t index);
    void padWave(std::vector<PendingSpawn> &wave);
};

} // namespace game

} // namespace reone
