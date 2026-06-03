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

#include "../object.h"
#include "reone/resource/gff.h"

namespace reone {

namespace game {

class Encounter : public Object {
public:
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

    void deserialize(const resource::Gff &gff);

private:
    struct SpawnPoint {
        glm::vec3 position {0.0f};
        float orientation {0.0f};
    };

    struct EncounterCreature {
        uint32_t _appearance {0};
        float _cr {0.0f};
        std::string _resRef;
        bool _singleSpawn {false};
    };

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
    int32_t _difficultyIndex {0};
    int32_t _difficulty {0};

    std::string _onEntered;
    std::string _onExit;
    std::string _onExhausted;
    std::string _onHeartbeat;
    std::string _onUserDefined;

    std::vector<EncounterCreature> _creatures;
    std::vector<glm::vec3> _geometry;
    std::vector<SpawnPoint> _spawnPoints;
    // END Serializable

    void deserializeAll(const resource::Gff &gff);
    void deserializeCreatures(const resource::Gff &gff);
    void deserializeGeometry(const resource::Gff &gff);
    void deserializeSpawnPoints(const resource::Gff &gff);
};

} // namespace game

} // namespace reone
