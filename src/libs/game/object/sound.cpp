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

#include "reone/game/object/sound.h"

#include "reone/game/di/services.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/sound.h"

using namespace reone::audio;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

void Sound::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> uts(_services.resource.gffs.get(resRef, ResType::Uts));
    if (!uts) {
        return;
    }
    deserialize(*uts);
}

void Sound::deserialize(const resource::Gff &gff) {
    std::string templateRes;
    if (gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto uts = _services.resource.gffs.get(templateRes, ResType::Uts)) {
            deserializeAll(*uts);
        }
    }
    deserializeAll(gff);
    loadAppearance();
    updateTransform();
}

void Sound::deserializeAll(const resource::Gff &gff) {
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }

    gff.readBool(_active, "Active");
    gff.readBool(_positional, "Positional");
    gff.readBool(_looping, "Looping");
    gff.readByte(_volume, "Volume");
    gff.readByte(_volumeVrtn, "VolumeVrtn");
    gff.readByte(_times, "Times");
    gff.readFloat(_pitchVariation, "PitchVariation");
    gff.readDword(_hours, "Hours");
    gff.readDword(_generatedType, "GeneratedType");
    gff.readDword(_interval, "Interval");
    gff.readDword(_intervalVrtn, "IntervalVrtn");
    gff.readFloat(_minDistance, "MinDistance");
    gff.readFloat(_maxDistance, "MaxDistance");
    gff.readBool(_continuous, "Continuous");
    gff.readByte(_random, "Random");
    gff.readFloat(_fixedVariance, "FixedVariance");
    gff.readBool(_randomPosition, "RandomPosition");
    gff.readFloat(_randomRangeX, "RandomRangeX");
    gff.readFloat(_randomRangeY, "RandomRangeY");
    gff.readFloat(_elevation, "Elevation");

    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");

    for (auto &soundGff : gff.getList("Sounds")) {
        std::string sound;
        if (soundGff->readResRef(sound, "Sound")) {
            _sounds.push_back(sound);
        }
    }

    if (gff.readByte(_priorityId, "Priority")) {
        std::shared_ptr<TwoDA> priorityGroups(_services.resource.twoDas.get("prioritygroups"));
        _priority = priorityGroups->getInt(_priorityId, "priority");
    }
}

void Sound::loadAppearance() {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto sceneNode = sceneGraph.newSound();
    sceneNode->setEnabled(_active);
    sceneNode->setPriority(_priority);
    sceneNode->setMaxDistance(_maxDistance);
    _sceneNode = std::move(sceneNode);
}

void Sound::update(float dt) {
    Object::update(dt);
    if (!_active) {
        return;
    }
    auto &sceneNode = static_cast<SoundSceneNode &>(*_sceneNode);
    if (sceneNode.isSoundPlaying()) {
        return;
    }
    if (_timeout > 0.0f) {
        _timeout = glm::max(0.0f, _timeout - dt);
        return;
    }
    const std::vector<std::string> &sounds = _sounds;
    int soundCount = static_cast<int>(sounds.size());
    if (sounds.empty()) {
        setActive(false);
        return;
    } else if (++_soundIdx >= soundCount) {
        if (_looping) {
            _soundIdx = 0;
        } else {
            setActive(false);
            return;
        }
    }
    float gain = _volume / 127.0f;
    bool loop = soundCount == 1 && _continuous;
    sceneNode.playSound(sounds[_soundIdx], gain, _positional, loop);
    _timeout = _interval / 1000.0f;
}

void Sound::updateTransform() {
    Object::updateTransform();
    if (!_sceneNode) {
        return;
    }
    glm::mat4 transform(_transform);
    transform *= glm::translate(glm::vec3(0.0f, 0.0f, _elevation));
    _sceneNode->setLocalTransform(_transform);
}

void Sound::setActive(bool active) {
    if (_active == active) {
        return;
    }
    if (_sceneNode) {
        _sceneNode->setEnabled(active);
    }
    if (active) {
        _timeout = 0.0f;
    }
    _active = active;
}

} // namespace game

} // namespace reone
