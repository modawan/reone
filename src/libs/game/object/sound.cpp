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
#include "reone/resource/provider/audioclips.h"
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
    deserialize(*uts, SerializedIdentityContext::templateResource(resRef));
}

void Sound::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto uts = _services.resource.gffs.get(templateRes, ResType::Uts)) {
            deserializeAll(*uts, SerializedIdentityContext::templateResource(templateRes));
            captureAuthoredRecord(*uts, gff);
        }
    }
    deserializeAll(gff, identityContext);
    loadAppearance();
    updateTransform();
}

void Sound::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
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
    sceneNode->setPositional(_positional);
    _sceneNode = std::move(sceneNode);
}

void Sound::update(float dt) {
    Object::update(dt);
    if (!_active) {
        return;
    }
    if (_fade) {
        advanceFade(dt);
        if (!_active) {
            return;
        }
    }
    auto &sceneNode = static_cast<SoundSceneNode &>(*_sceneNode);
    // Only an audible sound is heard, and one that stops being audible is
    // stopped by the scene. A continuous clip loops while it is audible and
    // waits while it is not. Any other clip keeps its own schedule, heard or
    // not: the next one is due its length and the interval after it starts.
    // Nothing starts before the scene has first told whether the sound is
    // audible.
    const bool audible = sceneNode.auidible();
    const bool continuous = _sounds.size() == 1 && _continuous;
    if (!sceneNode.isAudibilityKnown() || (continuous && (!audible || sceneNode.isSoundPlaying()))) {
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
    _timeout = _interval / 1000.0f;
    if (!continuous) {
        if (auto clip = _services.resource.audioClips.get(sounds[_soundIdx])) {
            _timeout += clip->duration();
        }
    }
    if (audible) {
        _playedGain = currentGain();
        sceneNode.playSound(sounds[_soundIdx], _playedGain, _positional, continuous);
    }
}

float Sound::currentGain() const {
    return _volume / 127.0f * fadeScale();
}

float Sound::fadeScale() const {
    if (!_fade || _fade->duration <= 0.0f) {
        return 1.0f;
    }
    return glm::clamp(_fade->remaining / _fade->duration, 0.0f, 1.0f);
}

void Sound::setVolume(int volume) {
    _volume = static_cast<uint8_t>(glm::clamp(volume, 0, 127));
    applyGainToPlayingClip();
}

void Sound::fadeAndStop(float seconds) {
    if (!_active || !(seconds > 0.0f)) {
        setActive(false);
        return;
    }
    _fade = Fade {seconds, seconds};
}

void Sound::advanceFade(float dt) {
    if (!_fade) {
        return;
    }
    _fade->remaining -= dt;
    if (_fade->remaining <= 0.0f) {
        setActive(false);
        return;
    }
    applyGainToPlayingClip();
}

// A clip keeps the gain it started with, so a change of volume or a fade
// scales it from there. A clip started silent cannot be scaled up; a
// continuous one is stopped so the next update starts it again at the new
// gain, and any other waits for its next clip.
void Sound::applyGainToPlayingClip() {
    if (!_sceneNode) {
        return;
    }
    auto &sceneNode = static_cast<SoundSceneNode &>(*_sceneNode);
    if (!sceneNode.isSoundPlaying()) {
        return;
    }
    if (_playedGain > 0.0f) {
        sceneNode.setGainScale(currentGain() / _playedGain);
    } else if (currentGain() > 0.0f && _sounds.size() == 1 && _continuous) {
        sceneNode.stopSound();
    }
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
    if (!active) {
        _fade.reset();
    }
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
