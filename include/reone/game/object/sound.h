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
#include "reone/resource/strings.h"

namespace reone {

namespace resource {

class Gff;

}

namespace game {

class Sound : public Object {
public:
    Sound(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Sound,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Sound;
    }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(const resource::Gff &gff);

    void update(float dt) override;

    bool isActive() const { return _active; }

    glm::vec3 getPosition() const;

    int priority() const { return _priority; }
    float maxDistance() const { return _maxDistance; }
    float minDistance() const { return _minDistance; }
    bool continuous() const { return _continuous; }
    float elevation() const { return _elevation; }
    bool looping() const { return _looping; }
    bool positional() const { return _positional; }

    void setActive(bool active);

private:
    // Serializable
    resource::LocString _locName;
    bool _active {false};
    bool _positional {false};
    bool _looping {false};
    uint8_t _volume {0};
    uint8_t _volumeVrtn {0};
    uint8_t _times {0};
    float _pitchVariation {0.0f};
    uint32_t _hours {0};
    uint32_t _generatedType {0};
    uint32_t _interval {0};
    uint32_t _intervalVrtn {0};
    float _minDistance {0.0f};
    float _maxDistance {0.0f};
    bool _continuous {false};
    uint8_t _random {0};
    float _fixedVariance {0.0f};
    bool _randomPosition {false};
    float _randomRangeX {0.0f};
    float _randomRangeY {0.0f};
    uint8_t _priorityId {0};
    float _elevation {0.0f};
    // END Serializable

    int _priority {0};
    int _soundIdx {-1};
    float _timeout {0.0f};
    std::vector<std::string> _sounds;

    void deserializeAll(const resource::Gff &gff);
    void loadAppearance();
    void updateTransform() override;
};

} // namespace game

} // namespace reone
