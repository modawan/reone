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

#include <algorithm>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/gffs.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/sound.h"
#include "reone/system/randomutil.h"

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

// The fields a sound placed from its template takes from its area entry.
static const std::vector<std::string> kEntryFields {"GeneratedType", "XPosition", "YPosition", "ZPosition"};

// A sound placed from its template takes everything from the template but
// what its entry gives: its generated type and its position, 0 where the entry
// has none. A saved sound takes everything from its record.
void Sound::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    std::shared_ptr<Gff> uts;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        uts = _services.resource.gffs.get(templateRes, ResType::Uts);
    }
    if (uts) {
        deserializeAll(*uts, SerializedIdentityContext::templateResource(templateRes));
        captureAuthoredRecord(*uts, gff, kEntryFields);
        _generatedType = gff.getUint("GeneratedType", 0);
        placeAtEntryPosition(gff);
    } else {
        deserializeAll(gff, identityContext);
    }
    loadAppearance();
    updateTransform();
    // A list with an interval waits one before its first sound, so that the
    // area's sounds don't all start together.
    _timeout = nextInterval();
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

    // An instance's own list takes the place of its template's.
    const auto soundList = gff.getList("Sounds");
    if (!soundList.empty()) {
        _sounds.clear();
    }
    for (auto &soundGff : soundList) {
        std::string sound;
        if (soundGff->readResRef(sound, "Sound")) {
            _sounds.push_back(sound);
        }
    }

    gff.readByte(_priorityId, "Priority");
}

// A looping sound object is an ambient, positional or area-wide; any other
// is a one-shot, positional or heard everywhere.
void Sound::loadAppearance() {
    int group;
    if (_looping) {
        group = _positional ? kLoopingPositionalAmbientSoundGroup : kAreaWideAmbientSoundGroup;
    } else {
        group = _positional ? kSingleShotPositionalSoundGroup : kSingleShotGlobalSoundGroup;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto sceneNode = sceneGraph.newSound();
    sceneNode->setEnabled(_active);
    sceneNode->setPriorityGroup(group);
    sceneNode->setMinDistance(_minDistance);
    sceneNode->setMaxDistance(_maxDistance);
    sceneNode->setPositional(_positional);
    _sceneNode = std::move(sceneNode);
}

// Times 1 plays by day and at dawn, 2 at night and at dusk, 3 always; 0
// plays in the hours its Hours mask sets.
bool Sound::playsAtThisTime() const {
    switch (_times) {
    case 0:
        return (_hours >> _game.worldTimeHour() & 1) != 0;
    case 1:
        return !_game.isNightTime();
    case 2:
        return _game.isNightTime();
    default:
        return true;
    }
}

// No interval means none, whatever its variation.
float Sound::nextInterval() const {
    if (_interval == 0) {
        return 0.0f;
    }
    int interval = static_cast<int>(_interval);
    if (_intervalVrtn != 0) {
        interval += randomInt(-static_cast<int>(_intervalVrtn), static_cast<int>(_intervalVrtn) - 1);
    }
    return std::max(0, interval) / 1000.0f;
}

// A volume above the loudest plays at the loudest.
float Sound::nextGain() const {
    int volume = _volume;
    if (_volumeVrtn != 0) {
        volume += randomInt(-static_cast<int>(_volumeVrtn), static_cast<int>(_volumeVrtn) - 1);
    }
    return std::clamp(volume, 0, kMaxVolume) / static_cast<float>(kMaxVolume);
}

// A fixed variance from 0.5 to 2 is the pitch, and the pitch variation is
// then not used; otherwise the variation strays the pitch at random, up by up
// to the variation or down by up to half of it.
float Sound::nextPitch() const {
    if (_fixedVariance >= 0.5f && _fixedVariance <= 2.0f) {
        return _fixedVariance;
    }
    return audio::randomPitch(_pitchVariation);
}

void Sound::update(float dt) {
    Object::update(dt);
    if (!_active) {
        return;
    }
    auto &sceneNode = static_cast<SoundSceneNode &>(*_sceneNode);
    // Out of its time the sound stops and waits.
    if (!playsAtThisTime()) {
        sceneNode.stopSound();
        return;
    }
    if (_sounds.empty()) {
        setActive(false);
        return;
    }
    // A looping sound object plays its first sound as one loop, and so does a
    // single continuous sound with no interval and no random position. The
    // loop plays while it is audible and waits while it is not. Nothing
    // starts before the scene has first told whether the sound is audible.
    const bool loops = _looping || (_sounds.size() == 1 && _continuous && !_randomPosition && _interval == 0);
    const bool audible = sceneNode.auidible();
    if (!sceneNode.isAudibilityKnown()) {
        return;
    }
    // A loop that fails to play ends the sound object.
    if (loops) {
        if (audible && !sceneNode.isSoundPlaying()) {
            if (!_services.resource.audioClips.get(_sounds.front())) {
                setActive(false);
                return;
            }
            sceneNode.playSound(_sounds.front(), std::min(int {_volume}, kMaxVolume) / static_cast<float>(kMaxVolume),
                                _positional, true);
        }
        return;
    }
    // Any other plays its list in order, heard or not: the next sound is due
    // the length of the last and an interval after it starts, counted in
    // seconds of play whatever the frame rate. A continuous list starts again,
    // reshuffled when random; any other ends, and with it the sound object.
    _timeout -= dt;
    if (_timeout > 0.0f) {
        return;
    }
    // How late this sound is; the next is due that much sooner.
    const float late = _timeout;
    const int count = static_cast<int>(_sounds.size());
    if (_restartList) {
        _restartList = false;
        _soundIdx = 0;
    } else if (_soundIdx < count - 1) {
        ++_soundIdx;
    } else if (_continuous) {
        if (_random) {
            std::vector<std::string> shuffled;
            while (!_sounds.empty()) {
                const int index = randomInt(0, static_cast<int>(_sounds.size()) - 1);
                shuffled.push_back(std::move(_sounds[index]));
                _sounds.erase(_sounds.begin() + index);
            }
            _sounds = std::move(shuffled);
        }
        _soundIdx = 0;
    } else {
        setActive(false);
        return;
    }
    // A sound that fails to play leaves the list, unless it is the last one,
    // which ends the sound object.
    auto clip = _services.resource.audioClips.get(_sounds[_soundIdx]);
    if (!clip) {
        // The next one is tried on the next update, as late as this one.
        _timeout = late;
        if (count < 2) {
            setActive(false);
            return;
        }
        _sounds.erase(_sounds.begin() + _soundIdx);
        if (_soundIdx > 0) {
            --_soundIdx;
        } else {
            _restartList = true;
        }
        return;
    }
    float next = clip->duration();
    if (_continuous || _soundIdx < count - 1) {
        next += nextInterval();
    }
    // A stall longer than the whole wait drops the backlog rather than
    // playing it in a burst.
    _timeout = next + late > 0.0f ? next + late : next;
    if (audible) {
        sceneNode.playSound(_sounds[_soundIdx], nextGain(), _positional, false, nextPitch());
    }
}

void Sound::updateTransform() {
    Object::updateTransform();
    if (!_sceneNode) {
        return;
    }
    glm::mat4 transform(_transform);
    transform *= glm::translate(glm::vec3(0.0f, 0.0f, _elevation));
    _sceneNode->setLocalTransform(transform);
}

void Sound::setActive(bool active) {
    if (_active == active) {
        return;
    }
    if (_sceneNode) {
        _sceneNode->setEnabled(active);
        // The node was found inaudible while it was disabled, so the scene
        // tells again before the first sound plays.
        if (active) {
            static_cast<SoundSceneNode &>(*_sceneNode).forgetAudibility();
        }
    }
    // An activated sound object starts its list again.
    if (active) {
        _timeout = 0.0f;
        _restartList = true;
    }
    _active = active;
}

} // namespace game

} // namespace reone
