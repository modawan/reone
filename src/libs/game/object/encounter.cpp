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

#include "reone/game/object/encounter.h"

#include <algorithm>
#include <cmath>

#include "reone/game/combattables.h"
#include "reone/game/di/services.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/strings.h"
#include "reone/system/randomutil.h"

using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

void Encounter::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto ute = _services.resource.gffs.get(templateRes, ResType::Ute)) {
            deserializeAll(*ute, SerializedIdentityContext::templateResource(templateRes));
            captureAuthoredRecord(*ute, gff);
        }
    }
    deserializeAll(gff, identityContext);
    // The creature list is kept in descending challenge rating.
    std::stable_sort(_creatures.begin(), _creatures.end(), [](const auto &a, const auto &b) { return a._cr > b._cr; });
}

void Encounter::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
    deserializeSavedRuntimeState(gff);
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }

    if (gff.readLocString(_locName, "LocalizedName", _services.resource.strings)) {
        _name = _locName.str();
    }

    gff.readBool(_active, "Active");
    gff.readBool(_reset, "Reset");
    gff.readInt(_resetTime, "ResetTime");
    gff.readInt(_respawns, "Respawns");
    gff.readInt(_spawnOption, "SpawnOption");
    gff.readInt(_maxCreatures, "MaxCreatures");
    gff.readInt(_recCreatures, "RecCreatures");
    gff.readBool(_playerOnly, "PlayerOnly");
    gff.readEnum(_faction, "Faction");

    // index into encdifficulty.2da
    gff.readInt(_difficultyIndex, "DifficultyIndex");
    // The difficulty comes from the table row; a row without a value leaves
    // it as it was.
    if (const auto difficulty = _services.game.combatTables.encounterDifficulty(_difficultyIndex)) {
        _difficulty = *difficulty;
    }

    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");

    gff.readResRef(_onEntered, "OnEntered");
    gff.readResRef(_onExit, "OnExit");
    gff.readResRef(_onExhausted, "OnExhausted");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onUserDefined, "OnUserDefined");

    deserializeCreatures(gff);
    deserializeGeometry(gff);
    deserializeSpawnPoints(gff);
    deserializePendingSpawns(gff);

    updateTransform();
}

void Encounter::deserializeSavedRuntimeState(const resource::Gff &gff) {
    _savedRuntimeState = SavedRuntimeState {};
    gff.readInt(_savedRuntimeState.areaListMaxSize, "AreaListMaxSize");
    gff.readInt(_savedRuntimeState.areaListSize, "AreaListSize");
    gff.readFloat(_savedRuntimeState.areaPoints, "AreaPoints");
    gff.readInt(_savedRuntimeState.currentSpawns, "CurrentSpawns");
    gff.readInt(_savedRuntimeState.customScriptId, "CustomScriptId");
    gff.readBool(_savedRuntimeState.exhausted, "Exhausted");
    gff.readDword(_savedRuntimeState.heartbeatDay, "HeartbeatDay");
    gff.readDword(_savedRuntimeState.heartbeatTime, "HeartbeatTime");
    gff.readDword(_savedRuntimeState.lastEntered, "LastEntered");
    gff.readDword(_savedRuntimeState.lastLeft, "LastLeft");
    gff.readDword(_savedRuntimeState.lastSpawnDay, "LastSpawnDay");
    gff.readDword(_savedRuntimeState.lastSpawnTime, "LastSpawnTime");
    gff.readInt(_savedRuntimeState.numberSpawned, "NumberSpawned");
    gff.readFloat(_savedRuntimeState.spawnPoolActive, "SpawnPoolActive");
    gff.readBool(_savedRuntimeState.started, "Started");
    if (_savedRuntimeState.lastEntered) {
        _savedReferenceIds.insert_or_assign(kLastEnteredReference, _savedRuntimeState.lastEntered);
    }
    if (_savedRuntimeState.lastLeft) {
        _savedReferenceIds.insert_or_assign(kLastLeftReference, _savedRuntimeState.lastLeft);
    }

    size_t index = 0;
    for (const auto &entry : gff.getList("AreaList")) {
        uint32_t id = 0;
        if (entry->readDword(id, "AreaObject")) {
            _savedRuntimeState.areaObjectIds.push_back(id);
            _savedReferenceIds.insert_or_assign(
                "EncounterArea/" + std::to_string(index), id);
        }
        ++index;
    }
}

std::shared_ptr<Object> Encounter::savedAreaObject(size_t index) const {
    return savedReference("EncounterArea/" + std::to_string(index));
}

void Encounter::deserializeCreatures(const resource::Gff &gff) {
    for (const auto &creatureGff : gff.getList("CreatureList")) {
        EncounterCreature creature;
        creatureGff->readDword(creature._appearance, "Appearance");
        creatureGff->readFloat(creature._cr, "CR");
        creatureGff->readResRef(creature._resRef, "ResRef");
        creatureGff->readBool(creature._singleSpawn, "SingleSpawn");
        creatureGff->readInt(creature._guaranteedCount, "GuaranteedCount");
        _creatures.push_back(std::move(creature));
    }
}

void Encounter::deserializeGeometry(const resource::Gff &gff) {
    for (const auto &pointGff : gff.getList("Geometry")) {
        glm::vec3 point;
        pointGff->readFloat(point[0], "X");
        pointGff->readFloat(point[1], "Y");
        pointGff->readFloat(point[2], "Z");
        _geometry.push_back(point);
    }
}

void Encounter::deserializeSpawnPoints(const resource::Gff &gff) {
    for (const auto &pointGff : gff.getList("SpawnPointList")) {
        SpawnPoint point;
        pointGff->readFloat(point.position[0], "X");
        pointGff->readFloat(point.position[1], "Y");
        pointGff->readFloat(point.position[2], "Z");
        pointGff->readFloat(point.orientation, "Orientation");
        _spawnPoints.push_back(point);
    }
}

void Encounter::deserializePendingSpawns(const resource::Gff &gff) {
    const auto list = gff.getList("SpawnList");
    if (list.empty()) {
        return;
    }
    _pendingSpawns.clear();
    for (const auto &entryGff : list) {
        PendingSpawn entry;
        entryGff->readResRef(entry.resRef, "SpawnResRef");
        entryGff->readFloat(entry.challengeRating, "SpawnCR");
        _pendingSpawns.push_back(std::move(entry));
    }
}

// Runtime

// An encounter creature's points in the first title's wave budget grow by
// half with each challenge rating; fractional ratings take the fractionalcr
// thresholds.
static constexpr float kCreaturePointBase = 1.5f;
static constexpr int kCreaturePointTableSize = 50;
// A creature is hostile to the encounter below this reputation.
static constexpr int kHostileReputation = 11;
// A spawn point hands its creature to the safe-location search within this radius.
static constexpr float kSpawnPointPlacementRadius = 20.0f;
// The first title's hostile tally counts creatures within this box around the entrant.
static constexpr float kHostileTallyExtent = 30.0f;
// The spawn option of an encounter that spawns again; its respawn limit when it has none.
static constexpr int kContinuousSpawnOption = 1;
static constexpr int kUnlimitedRespawns = -1;
static constexpr uint32_t kMillisecondsPerSecond = 1000;
// The hostiles' points count at a quarter toward the level a wave is chosen for.
static constexpr double kHostilePointsShare = 0.25;
// A wave may overspend what is left of the budget by a tenth of it.
static constexpr float kBudgetToleranceDivisor = 10.0f;
// A cheaper creature costs at least this much less than the one it replaces.
static constexpr float kCheaperMargin = 0.001f;
static constexpr int kHeartbeatEvent = static_cast<int>(SavedSignalEventType::Heartbeat);
static constexpr int kEnteredEvent = static_cast<int>(SavedSignalEventType::Entered);
static constexpr int kExitedEvent = static_cast<int>(SavedSignalEventType::Exited);
static constexpr int kExhaustedEvent = static_cast<int>(SavedSignalEventType::EncounterExhausted);

void Encounter::update(float dt) {
    spawnNext();
    updateRespawn();
    if (_active) {
        updateHeartbeat();
    }
    Object::update(dt);
}

bool Encounter::contains(const glm::vec3 &point) const {
    return isInPolygon2D(glm::vec2(point - _position), _geometry);
}

void Encounter::setActive(bool active) {
    _active = active;
    if (active) {
        spawnIfAppropriate(script::kObjectInvalid);
    }
}

// A difficulty outside the table, or a blank one, leaves the encounter's as it was.
void Encounter::setDifficulty(int difficultyIndex) {
    if (const auto difficulty = _services.game.combatTables.encounterDifficulty(difficultyIndex)) {
        _difficulty = *difficulty;
    }
}

// The creature signals the encounter when it enters or leaves; the signal is
// handled with the other queued events rather than in the middle of its move.
void Encounter::updateOccupancy(const std::shared_ptr<Creature> &creature, bool announce) {
    const bool inside = contains(creature->position());
    const bool known = _occupants.count(creature->id()) != 0;
    if (inside == known) {
        return;
    }
    if (inside) {
        _occupants.insert(creature->id());
    } else {
        _occupants.erase(creature->id());
    }
    if (announce) {
        _game.queueScriptEvent(*this, *creature,
                               Event(inside ? kEnteredEvent : kExitedEvent, {}, {}, {}, {creature}));
    }
}

void Encounter::receiveEnteredSignal(const std::shared_ptr<Object> &entering) {
    _savedReferences[kLastEnteredReference] = entering;
    const uint32_t enteringId = entering ? entering->id() : script::kObjectInvalid;
    spawnIfAppropriate(enteringId);
    _game.scriptRunner().run(_onEntered, _id);
}

void Encounter::receiveExitedSignal(const std::shared_ptr<Object> &exiting) {
    _savedReferences[kLastLeftReference] = exiting;
    _game.scriptRunner().run(_onExit, _id);
}

// A wave is chosen once per activation, when a creature hostile to the
// encounter stands inside it. The first title also needs points in its
// budget.
void Encounter::spawnIfAppropriate(uint32_t triggeringId) {
    if (!_active || _savedRuntimeState.started) {
        return;
    }
    if (!updateActivation(triggeringId)) {
        return;
    }
    if (_game.isTSL()) {
        chooseWaveByCount();
        _savedRuntimeState.started = true;
        return;
    }
    addToSpawnPool();
    if (_spawnPool > 0.0f) {
        chooseWaveByPoints();
        _savedRuntimeState.started = true;
    }
}

bool Encounter::isHostileTo(const Creature &creature) const {
    return creature.getReputationFrom(_faction) < kHostileReputation;
}

// Whether a living creature hostile to the encounter is inside it. The
// creature that just entered counts wherever it stands within the area's
// width. A player-only encounter needs a player character or party member.
bool Encounter::updateActivation(uint32_t triggeringId) {
    if (_geometry.empty()) {
        return false;
    }
    auto area = _game.module()->area();
    float minX = _geometry.front().x;
    float maxX = minX;
    float minY = _geometry.front().y;
    float maxY = minY;
    for (const auto &point : _geometry) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    bool activated = false;
    for (const auto &object : area->getObjectsByType(ObjectType::Creature)) {
        const auto &creature = static_cast<const Creature &>(*object);
        const glm::vec3 offset {creature.position() - _position};
        if (offset.x < minX || offset.x > maxX) {
            continue;
        }
        if (!isHostileTo(creature) || creature.isDead()) {
            continue;
        }
        if (creature.id() != triggeringId && (offset.y < minY || offset.y > maxY || !contains(creature.position()))) {
            continue;
        }
        if (_playerOnly && !creature.isPC() && !_game.party().isMember(creature)) {
            continue;
        }
        activated = true;
    }
    return activated;
}

void Encounter::chooseWaveByCount() {
    std::vector<PendingSpawn> wave;
    int wanted = _recCreatures + randomInt(0, _maxCreatures - _recCreatures);
    if (wanted > 0 && !_creatures.empty()) {
        // Single-spawn creatures come first, one each.
        bool allSingle = true;
        for (const auto &creature : _creatures) {
            if (wanted == 0) {
                break;
            }
            if (!creature._singleSpawn) {
                allSingle = false;
                continue;
            }
            wave.push_back(creature.pendingSpawn());
            --wanted;
        }
        if (wanted != 0 && !allSingle) {
            // Then each creature's guaranteed count.
            bool filled = false;
            for (const auto &creature : _creatures) {
                if (wanted == 0) {
                    break;
                }
                if (creature._singleSpawn) {
                    continue;
                }
                for (int copy = 0; copy < creature._guaranteedCount; ++copy) {
                    wave.push_back(creature.pendingSpawn());
                    if (copy == wanted - 1) {
                        filled = true;
                        break;
                    }
                }
                if (filled) {
                    break;
                }
                wanted -= creature._guaranteedCount;
            }
            // The rest are drawn at random among the others.
            if (!filled) {
                std::vector<PendingSpawn> pool;
                for (const auto &creature : _creatures) {
                    if (!creature._singleSpawn) {
                        pool.push_back(creature.pendingSpawn());
                    }
                }
                for (; wanted > 0; --wanted) {
                    wave.push_back(pool[randomInt(0, static_cast<int>(pool.size()) - 1)]);
                }
            }
        }
    }
    if (wave.empty()) {
        return;
    }
    _pendingSpawns.insert(_pendingSpawns.end(), wave.begin(), wave.end());
    if (!_spawnPoints.empty()) {
        _spawnsPerPoint = static_cast<int>(_pendingSpawns.size()) / static_cast<int>(_spawnPoints.size());
    }
}

// One chosen creature appears per update. When the encounter has spawn points
// it appears round the point chosen for whoever entered, or round the world
// origin when no one did; failing that, or without spawn points, it appears at
// the first open spot in sight of the encounter's position. A creature that
// cannot appear yet is tried again next update.
void Encounter::spawnNext() {
    if (_pendingSpawns.empty()) {
        return;
    }
    auto area = _game.module()->area();
    const auto entrant = std::dynamic_pointer_cast<Creature>(lastEntered());
    glm::vec3 spawnPosition {0.0f};
    std::optional<float> spawnFacing;
    bool atSpawnPoint = false;
    if (!_spawnPoints.empty()) {
        int index;
        if (_game.isTSL()) {
            if (_spawnsPerPoint == 0) {
                _spawnsPerPoint = static_cast<int>(_pendingSpawns.size()) / static_cast<int>(_spawnPoints.size());
            }
            if (_spawnsAtPoint == _spawnsPerPoint) {
                ++_spawnPointIndex;
                _spawnsAtPoint = 0;
                if (_spawnPointIndex >= static_cast<int>(_spawnPoints.size())) {
                    _spawnPointIndex = 0;
                }
            }
            index = _spawnPointIndex;
            ++_spawnsAtPoint;
        } else {
            // The first title uses the point furthest from whoever entered.
            index = 0;
            if (entrant) {
                float furthest = -1.0f;
                for (size_t i = 0; i < _spawnPoints.size(); ++i) {
                    const glm::vec3 offset {_spawnPoints[i].position - entrant->position()};
                    const float distance2 = glm::dot(offset, offset);
                    if (distance2 > furthest) {
                        furthest = distance2;
                        index = static_cast<int>(i);
                    }
                }
            }
        }
        if (entrant) {
            spawnPosition = _spawnPoints[index].position;
            // A spawn point's orientation is its clockwise bearing from north.
            spawnFacing = -_spawnPoints[index].orientation;
        }
        atSpawnPoint = true;
    }
    const auto &next = _pendingSpawns.front();
    auto creature = _game.newCreatureFromBlueprint(next.resRef);
    if (!creature) {
        return;
    }
    std::optional<glm::vec3> placed;
    if (atSpawnPoint) {
        placed = area->computeSafeLocation(spawnPosition, kSpawnPointPlacementRadius, *creature, false);
    }
    if (!placed) {
        placed = area->findOpenSpotInSight(_position, *creature);
    }
    if (!placed) {
        _game.destroyRuntimeObjectGraph(creature);
        return;
    }
    creature->joinEncounter(*this);
    creature->setPosition(*placed);
    area->landObject(*creature);
    if (atSpawnPoint && spawnFacing) {
        creature->setFacing(*spawnFacing);
    }
    ++_savedRuntimeState.numberSpawned;
    _pendingSpawns.erase(_pendingSpawns.begin());
    area->add(creature);
    area->signalEntered(*creature);
}

// A continuous encounter that resets re-arms once its reset time has passed
// since its wave was destroyed, up to its respawn limit (-1: without limit).
// Both parts of the destruction stamp must be set. The reset time is
// compared, in milliseconds, with the time part of the world time since then,
// and any whole day passed re-arms it too.
void Encounter::updateRespawn() {
    auto &state = _savedRuntimeState;
    if (_spawnOption != kContinuousSpawnOption || !_reset || state.lastSpawnDay == 0 || state.lastSpawnTime == 0) {
        return;
    }
    uint32_t days = 0;
    uint32_t milliseconds = 0;
    _game.subtractWorldTimes(_game.worldTimeDay(), _game.worldTimeOfDay(), state.lastSpawnDay, state.lastSpawnTime,
                             days, milliseconds);
    if (milliseconds < static_cast<uint32_t>(_resetTime) * kMillisecondsPerSecond && days == 0) {
        return;
    }
    if (state.currentSpawns >= _respawns && _respawns != kUnlimitedRespawns) {
        return;
    }
    ++state.currentSpawns;
    state.lastSpawnDay = 0;
    state.lastSpawnTime = 0;
    _active = true;
    spawnIfAppropriate(script::kObjectInvalid);
    _spawnPool = 0.0f;
    state.spawnPoolActive = 0.0f;
}

// A due heartbeat is signalled as an event; the first check only starts the
// clock.
void Encounter::updateHeartbeat() {
    auto &state = _savedRuntimeState;
    if (!isHeartbeatDueSince(state.heartbeatDay, state.heartbeatTime)) {
        return;
    }
    const bool running = state.heartbeatTime != 0;
    state.heartbeatDay = _game.worldTimeDay();
    state.heartbeatTime = _game.worldTimeOfDay();
    if (running) {
        _game.queueScriptEvent(*this, *this, Event(kHeartbeatEvent));
    }
}

void Encounter::receiveHeartbeatSignal() {
    if (!_onHeartbeat.empty()) {
        _game.scriptRunner().run(_onHeartbeat, _id);
    }
}

void Encounter::removeSpawnedCreature(float challengeRating) {
    auto &state = _savedRuntimeState;
    if (state.numberSpawned == 0) {
        return;
    }
    if (!_game.isTSL()) {
        state.spawnPoolActive = std::max(0.0f, state.spawnPoolActive - creaturePoints(challengeRating));
    }
    --state.numberSpawned;
    if (state.numberSpawned != 0) {
        return;
    }
    _game.queueScriptEvent(*this, *this, Event(kExhaustedEvent));
    if (_reset) {
        state.lastSpawnDay = _game.worldTimeDay();
        state.lastSpawnTime = _game.worldTimeOfDay();
    }
    for (auto &creature : _creatures) {
        creature._used = false;
        creature._tried = false;
    }
    _active = false;
    state.started = false;
}

void Encounter::receiveExhaustedSignal() {
    _game.scriptRunner().run(_onExhausted, _id);
}

// The first title's wave budget

float Encounter::creaturePoints(float challengeRating) const {
    if (challengeRating > 0.0f && challengeRating < 1.0f) {
        const auto &tables = _services.game.combatTables;
        if (challengeRating >= tables.fractionalChallengeMinimum(0)) {
            return 1.0f;
        }
        if (challengeRating >= tables.fractionalChallengeMinimum(1)) {
            return 1.0f / kCreaturePointBase;
        }
        if (challengeRating >= tables.fractionalChallengeMinimum(2)) {
            return std::pow(kCreaturePointBase, -2.0f);
        }
        if (challengeRating >= tables.fractionalChallengeMinimum(3)) {
            return std::pow(kCreaturePointBase, -3.0f);
        }
        return std::pow(kCreaturePointBase, -4.0f);
    }
    if (challengeRating >= 0.0f && challengeRating <= kCreaturePointTableSize) {
        return std::pow(kCreaturePointBase, std::trunc(challengeRating));
    }
    return std::pow(kCreaturePointBase, challengeRating);
}

float Encounter::minimumCreaturePoints() const {
    return _creatures.empty() ? 0.0f : creaturePoints(_creatures.back()._cr);
}

// Each activation adds points for a level two below the hostiles present
// (by their points' weight), raised by the encounter's difficulty.
void Encounter::addToSpawnPool() {
    auto &state = _savedRuntimeState;
    state.areaPoints = tallyHostilePoints();
    if (state.areaPoints == 0.0f) {
        return;
    }
    const double hostileLevel = std::log(static_cast<double>(state.areaPoints) * kHostilePointsShare) / std::log(static_cast<double>(kCreaturePointBase));
    const int level = static_cast<int>(hostileLevel + 0.5) + _difficulty;
    _spawnPool += creaturePoints(static_cast<float>(level - 1));
}

// Hostile living creatures around whoever entered, at their level's points;
// creatures other than player characters count at half their level.
float Encounter::tallyHostilePoints() const {
    const auto entrant = std::dynamic_pointer_cast<Creature>(lastEntered());
    if (!entrant) {
        return 0.0f;
    }
    auto area = _game.module()->area();
    float points = 0.0f;
    for (const auto &object : area->getObjectsByType(ObjectType::Creature)) {
        const auto &creature = static_cast<const Creature &>(*object);
        const glm::vec3 offset {creature.position() - entrant->position()};
        if (std::abs(offset.x) > kHostileTallyExtent || std::abs(offset.y) > kHostileTallyExtent) {
            continue;
        }
        if (!isHostileTo(creature) || creature.isDead()) {
            continue;
        }
        int level = creature.attributes().getAggregateLevel();
        if (!creature.actsAsPC()) {
            level >>= 1;
        }
        points += creaturePoints(static_cast<float>(level));
    }
    return points;
}

void Encounter::chooseWaveByPoints() {
    const int recommended = _recCreatures - _savedRuntimeState.numberSpawned;
    std::vector<PendingSpawn> wave;
    chooseInitialWave(wave);
    if (recommended > static_cast<int>(wave.size())) {
        if (_spawnPool > minimumCreaturePoints()) {
            int index = static_cast<int>(wave.size()) - 1;
            for (; index >= 0 && recommended > static_cast<int>(wave.size()); --index) {
                replaceWithCheaper(wave, static_cast<size_t>(index));
            }
            if (index < 0 && recommended > static_cast<int>(wave.size())) {
                padWave(wave);
            }
        } else {
            padWave(wave);
        }
    }
    if (minimumCreaturePoints() > _spawnPool) {
        _spawnPool = 0.0f;
    }
    _pendingSpawns.insert(_pendingSpawns.end(), wave.begin(), wave.end());
}

// Spend the budget from the highest challenge rating down: single-spawn
// creatures cost nothing, a creature alone at its rating is repeated while
// the budget lasts, and creatures sharing a rating are drawn among at random.
void Encounter::chooseInitialWave(std::vector<PendingSpawn> &wave) {
    const int limit = _maxCreatures - _savedRuntimeState.numberSpawned;
    float remaining = _spawnPool - _savedRuntimeState.spawnPoolActive;
    const float tolerance = remaining / kBudgetToleranceDivisor;
    size_t index = 0;
    while (index < _creatures.size() && static_cast<int>(wave.size()) < limit && remaining > 0.0f) {
        const auto &creature = _creatures[index];
        const float points = creaturePoints(creature._cr);
        if (creature._singleSpawn && !creature._used) {
            wave.push_back(creature.pendingSpawn());
            ++index;
            continue;
        }
        if (creature._cr == 0.0f || points > remaining + tolerance) {
            ++index;
            continue;
        }
        if (index + 1 >= _creatures.size() || _creatures[index + 1]._cr != creature._cr) {
            bool used = creature._used;
            while (remaining + tolerance >= points && static_cast<int>(wave.size()) < limit) {
                if (creature._singleSpawn && used) {
                    break;
                }
                wave.push_back(creature.pendingSpawn());
                remaining -= points;
                used = true;
            }
            ++index;
            continue;
        }
        size_t last = index;
        int singles = 1;
        while (last + 1 < _creatures.size() && _creatures[last + 1]._cr == creature._cr) {
            ++last;
            _creatures[last]._tried = false;
            if (_creatures[last]._singleSpawn) {
                ++singles;
            }
        }
        const int groupSize = static_cast<int>(last - index + 1);
        int tried = 0;
        size_t pick;
        do {
            pick = index + randomInt(0, groupSize - 1);
            auto &candidate = _creatures[pick];
            if (!candidate._singleSpawn || !candidate._used) {
                wave.push_back(candidate.pendingSpawn());
                candidate._used = true;
                remaining -= creaturePoints(candidate._cr);
            }
            if (!candidate._tried) {
                candidate._tried = true;
                ++tried;
            }
            if (singles == groupSize && tried == groupSize) {
                break;
            }
        } while (remaining + tolerance >= creaturePoints(_creatures[pick]._cr) && static_cast<int>(wave.size()) < limit);
        index = last + 1;
    }
}

// Swap a chosen creature for cheaper ones worth less than it in total.
void Encounter::replaceWithCheaper(std::vector<PendingSpawn> &wave, size_t index) {
    const float budgetPoints = creaturePoints(wave[index].challengeRating);
    if (budgetPoints == minimumCreaturePoints()) {
        return;
    }
    wave.erase(wave.begin() + static_cast<std::ptrdiff_t>(index));
    float budget = budgetPoints;
    const float ceiling = budgetPoints - kCheaperMargin;
    const int recommended = _recCreatures - _savedRuntimeState.numberSpawned;
    for (size_t i = 0; i < _creatures.size() && static_cast<int>(wave.size()) <= recommended; ++i) {
        auto &creature = _creatures[i];
        const float points = creaturePoints(creature._cr);
        while (budget >= points && ceiling >= points && static_cast<int>(wave.size()) < _maxCreatures) {
            if (creature._singleSpawn && creature._used) {
                break;
            }
            wave.push_back(creature.pendingSpawn());
            creature._used = true;
            budget -= points;
        }
    }
}

// Fill up to the recommended count from the cheapest creatures up, drawing
// at random among those of equal points.
void Encounter::padWave(std::vector<PendingSpawn> &wave) {
    int needed = _recCreatures - _savedRuntimeState.numberSpawned - static_cast<int>(wave.size());
    float previous = 0.0f;
    while (needed > 0) {
        int found = -1;
        for (int i = static_cast<int>(_creatures.size()) - 1; i >= 0; --i) {
            const auto &creature = _creatures[i];
            if (creaturePoints(creature._cr) > previous && (!creature._singleSpawn || !creature._used)) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            return;
        }
        const float points = creaturePoints(_creatures[found]._cr);
        previous = points;
        int first = -1;
        int last = -1;
        int singles = 0;
        for (int i = 0; i < static_cast<int>(_creatures.size()); ++i) {
            if (creaturePoints(_creatures[i]._cr) != points) {
                continue;
            }
            if (first < 0) {
                first = i;
            }
            last = i;
            _creatures[i]._tried = false;
            if (_creatures[i]._singleSpawn) {
                ++singles;
            }
        }
        const int groupSize = last - first + 1;
        int tried = 0;
        bool exhaustedGroup = false;
        do {
            auto &candidate = _creatures[first + randomInt(0, groupSize - 1)];
            if (!candidate._singleSpawn || !candidate._used) {
                wave.push_back(candidate.pendingSpawn());
                candidate._used = true;
                --needed;
            }
            if (!candidate._tried) {
                candidate._tried = true;
                ++tried;
            }
            if (groupSize == singles && tried == singles) {
                exhaustedGroup = true;
            }
        } while (!exhaustedGroup && needed > 0);
    }
}

} // namespace game

} // namespace reone
