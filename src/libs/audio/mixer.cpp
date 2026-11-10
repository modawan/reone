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

#include "reone/audio/mixer.h"

#include <algorithm>
#include <cmath>

#include "reone/audio/clip.h"
#include "reone/audio/context.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace audio {

// A fade between silence and full gain takes half a second.
static constexpr std::chrono::milliseconds kMuteFadeTime {500};

float randomPitch(float variance) {
    const float clamped = std::clamp(variance, 0.0f, 1.0f);
    if (clamped <= 0.0f) {
        return 1.0f;
    }
    const bool up = randomInt(0, 1) == 1;
    const float stray = randomFloat(0.0f, clamped);
    return up ? 1.0f + stray : 1.0f - 0.5f * stray;
}

void AudioMixer::render() {
    renderFade();
    const glm::vec3 listener {_context.listenerPosition()};
    for (auto it = _sources.begin(); it != _sources.end();) {
        auto &source = it->source;
        source->render();
        if (!source->isPlaying() || !updateRange(*it, listener)) {
            release(*it);
            it = _sources.erase(it);
        } else {
            ++it;
        }
    }
    applyChanges(_arbiter.update());
    refreshObstruction();
}

void AudioMixer::setObstructionTest(ObstructionTest test) {
    _obstructionTest = std::move(test);
}

void AudioMixer::obstruct(AudioSource &source) {
    if (_obstructionTest && _context.rendersObstruction() && source.position()) {
        _context.setObstruction(source, _obstructionTest(*source.position(), _context.listenerPosition()));
    }
}

// Arbitrary, but at least one so every positional source has its turn, and
// few enough that the rays stay cheap each render.
static constexpr size_t kObstructionsPerRender = 4;

// Only positional sources within range take a turn; the rest are passed over.
void AudioMixer::refreshObstruction() {
    if (!_obstructionTest || !_context.rendersObstruction()) {
        return;
    }
    size_t obstructed = 0;
    for (size_t step = 0; step < _sources.size() && obstructed < kObstructionsPerRender; ++step) {
        const size_t index = _nextObstructed % _sources.size();
        _nextObstructed = index + 1;
        auto &active = _sources[index];
        if (active.positional && !active.outOfRange) {
            obstruct(*active.source);
            ++obstructed;
        }
    }
}

bool AudioMixer::heldBySlot(const ActiveSource &active) const {
    if (!active.slotId) {
        return false;
    }
    const auto slot = _arbiter.slot(*active.slotId);
    return slot && *slot != SourceSlot::Playing;
}

void AudioMixer::release(ActiveSource &active) {
    if (active.slotId) {
        _arbiter.remove(*active.slotId);
        active.slotId.reset();
    }
}

void AudioMixer::syncArbiter() {
    for (auto &active : _sources) {
        if (!active.slotId) {
            continue;
        }
        if (!active.source->isPlaying()) {
            release(active);
            continue;
        }
        _arbiter.setOwned(*active.slotId, active.source->isOwned());
    }
}

void AudioMixer::applyChanges(const std::vector<SlotChange> &changes) {
    for (const auto &change : changes) {
        auto it = std::find_if(_sources.begin(), _sources.end(), [&change](const ActiveSource &active) {
            return active.slotId == change.id;
        });
        if (it == _sources.end()) {
            continue;
        }
        switch (change.kind) {
        case SlotChange::Kind::Stop:
            it->slotId.reset();
            it->source->stop();
            break;
        case SlotChange::Kind::Wait:
            it->source->pause();
            break;
        case SlotChange::Kind::Start:
            resumeIfFree(*it);
            break;
        }
    }
}

// Only a source that is playing is tested: one a mode, the window's focus or
// a movie holds paused is left as it is. Beyond its distance a one-shot ends,
// and a looping source waits paused until the listener comes back within it.
bool AudioMixer::updateRange(ActiveSource &active, const glm::vec3 &listener) {
    if (active.modePaused || active.focusPaused || active.movieHeld) {
        return true;
    }
    if (active.source->isWithinRange(listener)) {
        if (active.outOfRange) {
            active.outOfRange = false;
            // Back within range, a loop in a group needs room and a voice
            // again; the arbiter starts it when it has them.
            if (active.slotId) {
                _arbiter.enterRange(*active.slotId);
            } else {
                resumeIfFree(active);
            }
        }
        return true;
    }
    if (!active.source->isLooping()) {
        active.source->stop();
        return false;
    }
    if (!active.outOfRange) {
        active.source->pause();
        active.outOfRange = true;
        if (active.slotId) {
            _arbiter.leaveRange(*active.slotId);
        }
    }
    return true;
}

void AudioMixer::renderFade() {
    if (!_fadeStart) {
        return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - *_fadeStart;
    const float fraction = std::clamp(
        std::chrono::duration<float>(elapsed).count() / std::chrono::duration<float>(kMuteFadeTime).count(),
        0.0f, 1.0f);
    _context.setListenerGain(_muted ? 1.0f - fraction : fraction);
    if (fraction >= 1.0f) {
        _fadeStart.reset();
    }
}

void AudioMixer::stop(AudioType type) {
    for (auto it = _sources.begin(); it != _sources.end();) {
        if (it->type != type) {
            ++it;
            continue;
        }

        it->source->stop();
        release(*it);
        it = _sources.erase(it);
    }
}

void AudioMixer::stopAll() {
    for (auto &source : _sources) {
        source.source->stop();
        release(source);
    }
    _sources.clear();
}

void AudioMixer::resumeIfFree(ActiveSource &active) {
    if (!active.modePaused && !active.focusPaused && !active.movieHeld && !active.outOfRange && !heldBySlot(active)) {
        active.source->resume();
    }
}

void AudioMixer::setSoundsHeldForMovie(bool held, const std::shared_ptr<AudioSource> &playingThrough) {
    if (_soundsHeldForMovie == held) {
        return;
    }
    _soundsHeldForMovie = held;
    for (auto &active : _sources) {
        if (held) {
            if (active.type == AudioType::Movie || active.source == playingThrough) {
                continue;
            }
            active.source->pause();
            active.movieHeld = true;
        } else if (active.movieHeld) {
            active.movieHeld = false;
            resumeIfFree(active);
        }
    }
}

void AudioMixer::pauseSources(const std::set<int> &keptGroups) {
    _modeKeptGroups = keptGroups;
    for (auto &active : _sources) {
        if (active.type == AudioType::Movie || active.modePaused) {
            continue;
        }
        if (active.group && keptGroups.count(*active.group) > 0) {
            continue;
        }
        active.source->pause();
        active.modePaused = true;
    }
}

void AudioMixer::resumeSources() {
    _modeKeptGroups.reset();
    for (auto &active : _sources) {
        if (!active.modePaused) {
            continue;
        }
        active.modePaused = false;
        resumeIfFree(active);
    }
}

void AudioMixer::setPriorityGroups(std::vector<PriorityGroup> groups) {
    _arbiter.setPriorityGroups(std::move(groups));
}

void AudioMixer::setStreamedGroups(std::set<int> groups) {
    _streamedGroups = std::move(groups);
}

void AudioMixer::setDryGroups(std::set<int> groups) {
    _dryGroups = std::move(groups);
}

void AudioMixer::setRoomReverb(bool reverb) {
    if (_roomReverb == reverb) {
        return;
    }
    _roomReverb = reverb;
    for (auto &active : _sources) {
        if (active.positional && !isStreamed(active)) {
            _context.setReverbSend(*active.source, reverbSend(active));
        }
    }
}

bool AudioMixer::isStreamed(const ActiveSource &active) const {
    return active.group && _streamedGroups.count(*active.group) > 0;
}

// A wet sound sends a little under half of itself to the reverb.
static const float kReverbSendFactor = std::pow(0.6f, 5.0f / 3.0f);

// Only positional sounds reach the reverb; 2D sounds stay dry for now.
float AudioMixer::reverbSend(const ActiveSource &active) const {
    if (!active.positional || (active.group && _dryGroups.count(*active.group) > 0)) {
        return 0.0f;
    }
    if (isStreamed(active)) {
        return _options.streamedReverb ? kReverbSendFactor : 0.0f;
    }
    return _options.unstreamedReverb && _roomReverb ? kReverbSendFactor : 0.0f;
}

// A group the table has no row for takes a blank row.
PriorityGroup AudioMixer::priorityGroup(int group) const {
    return _arbiter.priorityGroup(group);
}

// Losing focus pauses every source but an unstreamed one playing in a group
// the sound mode in force keeps; streams and movie audio wait with the rest.
void AudioMixer::setFocusLost(bool lost) {
    if (_focusLost == lost) {
        return;
    }
    _focusLost = lost;
    for (auto &active : _sources) {
        if (lost) {
            const bool kept = _modeKeptGroups && active.group && _modeKeptGroups->count(*active.group) > 0 &&
                              _streamedGroups.count(*active.group) == 0 && !active.modePaused;
            if (kept) {
                continue;
            }
            active.source->pause();
            active.focusPaused = true;
        } else if (active.focusPaused) {
            active.focusPaused = false;
            resumeIfFree(active);
        }
    }
}

void AudioMixer::setMuted(bool muted) {
    if (_muted == muted) {
        return;
    }
    _muted = muted;
    _fadeStart = std::chrono::steady_clock::now();
}

void AudioMixer::setVolume(AudioType type, int volume) {
    switch (type) {
    case AudioType::Music:
        _options.musicVolume = volume;
        _musicVolume.reset();
        break;
    case AudioType::Voice:
        _options.voiceVolume = volume;
        break;
    case AudioType::Sound:
        _options.soundVolume = volume;
        break;
    case AudioType::Movie:
        _options.movieVolume = volume;
        break;
    }
    applyVolume(type);
}

void AudioMixer::setMusicVolume(float volume) {
    _musicVolume = volume;
    applyVolume(AudioType::Music);
}

void AudioMixer::setDucked(bool ducked, const std::set<int> &groups) {
    if (_ducked == ducked && _duckedGroups == groups) {
        return;
    }
    _ducked = ducked;
    _duckedGroups = groups;
    applyVolume(AudioType::Music);
    applyVolume(AudioType::Sound);
}

void AudioMixer::applyVolume(AudioType type) {
    for (auto &active : _sources) {
        if (active.type == type) {
            active.source->setGain(gainByType(type, active.gain, active.positional, active.group));
        }
    }
}

// A source takes its group's volume, in its gain, and, unless it has its own,
// its group's distances before it starts. Under a sound mode it starts paused
// unless the mode keeps its group; movie audio plays on under a mode. Range is
// tested first: a source that starts out of range is dropped when it is a
// one-shot, held when it loops, whatever else holds it. A source in a group
// then needs room and a voice from the arbiter. A source not played is handed
// back without an OpenAL source.
std::shared_ptr<AudioSource> AudioMixer::play(std::shared_ptr<AudioClip> clip,
                                              AudioType type,
                                              std::optional<int> group,
                                              float gain,
                                              bool loop,
                                              std::optional<glm::vec3> position,
                                              std::optional<SoundDistances> distances,
                                              std::optional<float> pitchVariance) {
    if (!clip || clip->getFrameCount() == 0) {
        return nullptr;
    }

    const bool positional = position.has_value();
    float variance = 0.0f;
    if (group) {
        const PriorityGroup row = priorityGroup(*group);
        if (!distances) {
            distances = SoundDistances {row.fullVolumeDistance, row.cutoffDistance};
        }
        variance = row.playbackVariance;
    }
    const bool inRange = !position || !distances ||
                         AudioSource::isWithinRange(*position, distances->cutoffDistance, _context.listenerPosition());
    auto source = std::make_shared<AudioSource>(
        std::move(clip),
        gainByType(type, gain, positional, group),
        loop,
        std::move(position));

    ActiveSource active {source, type, group};
    active.gain = gain;
    active.positional = positional;
    active.movieHeld = _soundsHeldForMovie && type != AudioType::Movie;
    active.modePaused = _modeKeptGroups && type != AudioType::Movie && !(group && _modeKeptGroups->count(*group) > 0);
    active.focusPaused = _focusLost;
    if (group) {
        syncArbiter();
        ArbitedSource arbited;
        arbited.group = *group;
        arbited.looping = loop;
        arbited.streamed = _streamedGroups.count(*group) > 0;
        arbited.positional = positional;
        Admission admission = _arbiter.admit(arbited, inRange);
        // A sound the admission displaced stops or waits, even when the new
        // sound finds no voice after all.
        applyChanges(admission.changes);
        if (!admission.id) {
            return source;
        }
        active.slotId = admission.id;
        active.outOfRange = admission.slot == SourceSlot::Away;
    } else if (!inRange) {
        if (!loop) {
            return source;
        }
        active.outOfRange = true;
    }

    source->init();
    _context.setReverbSend(*source, reverbSend(active));
    obstruct(*source);
    if (distances) {
        source->setDistances(distances->fullVolumeDistance, distances->cutoffDistance);
    }
    if (pitchVariance) {
        variance = *pitchVariance;
    }
    if (variance > 0.0f) {
        source->setPitch(randomPitch(variance));
    }
    if (active.movieHeld || active.modePaused || active.focusPaused || active.outOfRange || heldBySlot(active)) {
        source->startPaused();
    } else {
        source->play();
    }
    _sources.push_back(std::move(active));
    return source;
}

// The volume of a positional sound rises steeply at first.
static constexpr float kPositionalGainExponent = 0.6f;

float AudioMixer::gainByType(AudioType type, float gain, bool positional, std::optional<int> group) const {
    if (_options.muted) {
        return 0.0f;
    }
    int volume;
    switch (type) {
    case AudioType::Music:
        volume = _options.musicVolume;
        break;
    case AudioType::Voice:
        volume = _options.voiceVolume;
        break;
    case AudioType::Sound:
        volume = _options.soundVolume;
        break;
    case AudioType::Movie:
        volume = _options.movieVolume;
        break;
    default:
        volume = 85.0f;
        break;
    }
    float slider = type == AudioType::Music && _musicVolume ? *_musicVolume : volume / static_cast<float>(AudioOptions::kMaxVolume);
    // A sound in a priority group plays at the group's share of the loudest
    // volume, taken with the slider.
    if (group) {
        slider *= priorityGroup(*group).volume / static_cast<float>(kMaxVolume);
    }
    // Ducked sounds play at a fifth of their volume.
    static constexpr float kDuckingFactor = 0.2f;
    if (_ducked && (type == AudioType::Music || (group && _duckedGroups.count(*group) != 0))) {
        slider *= kDuckingFactor;
    }
    const float bias = _options.bias2D3D;
    if (positional) {
        if (bias < 1.0f) {
            slider *= bias;
        }
        return std::pow(slider * gain, kPositionalGainExponent);
    }
    if (bias > 1.0f) {
        slider *= 2.0f - bias;
    }
    return slider * gain;
}

} // namespace audio

} // namespace reone
