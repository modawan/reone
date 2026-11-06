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

#include "reone/game/areamusicplayer.h"

#include <algorithm>

#include <boost/algorithm/string/case_conv.hpp>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/di/services.h"
#include "reone/game/object/area.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/system/clock.h"
#include "reone/system/di/services.h"
#include "reone/system/randomutil.h"

using namespace reone::audio;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr uint32_t kMusicFadeMilliseconds = 800;
static constexpr uint32_t kBattleHandOffMilliseconds = 10;
static constexpr uint32_t kBattleRepeatGapMilliseconds = 1000;
static constexpr uint32_t kMusicRetryMilliseconds = 10000;
// Setting a track restarts the background music on the next update.
static constexpr uint32_t kTrackChangeRestartMilliseconds = 1;
static constexpr uint32_t kAmbientFadeMilliseconds = 6000;
static constexpr int kStingerPriorityGroup = 1;
static constexpr int kMusicPriorityGroup = 2;
static constexpr int kAreaWideAmbientPriorityGroup = 4;
static constexpr int kMaxVolume = 127;
// TSL plays the streamed music group louder than its table volume.
static constexpr float kTslMusicGroupBoost = 1.4f;

// Music, battle music and menu music play at the volume of the streamed music
// priority group, stingers at the stinger group's and the area-wide ambient
// loop at the area-wide ambient group's. A group without a volume plays at the
// loudest.
void AreaMusicPlayer::init(bool tsl) {
    auto &twoDas = _services.resource.twoDas;

    auto music = getRequiredTwoDA(twoDas, "ambientmusic");
    _tracks.clear();
    for (int row = 0; row < music->getRowCount(); ++row) {
        Track entry;
        entry.resRef = boost::to_lower_copy(music->getString(row, "resource"));
        for (size_t i = 0; i < entry.stingers.size(); ++i) {
            entry.stingers[i] = boost::to_lower_copy(music->getString(row, "stinger" + std::to_string(i + 1)));
        }
        _tracks.push_back(std::move(entry));
    }

    auto sounds = getRequiredTwoDA(twoDas, "ambientsound");
    _ambientTracks.clear();
    for (int row = 0; row < sounds->getRowCount(); ++row) {
        _ambientTracks.push_back(boost::to_lower_copy(sounds->getString(row, "resource")));
    }

    auto groups = getRequiredTwoDA(twoDas, "prioritygroups");
    const std::string volumeColumn {tsl ? "volume_pc" : "volume"};
    auto groupGain = [&](int group, float boost) {
        auto volume = groups->getIntOpt(group, volumeColumn);
        return (volume ? static_cast<int>(*volume * boost) : kMaxVolume) / static_cast<float>(kMaxVolume);
    };
    _musicGroupGain = groupGain(kMusicPriorityGroup, tsl ? kTslMusicGroupBoost : 1.0f);
    _stingerGroupGain = groupGain(kStingerPriorityGroup, 1.0f);
    _ambientGroupGain = groupGain(kAreaWideAmbientPriorityGroup, 1.0f);
}

// A row outside a table reads as blank, as every 2DA row outside its table does.
const AreaMusicPlayer::Track &AreaMusicPlayer::track(int row) const {
    static const Track kNoTrack;
    return row >= 0 && row < static_cast<int>(_tracks.size()) ? _tracks[row] : kNoTrack;
}

const AreaMusicPlayer::Track &AreaMusicPlayer::battleTrack() const {
    return track(_area->ambientAudio().musicBattle);
}

const std::string &AreaMusicPlayer::ambientTrack(int row) const {
    static const std::string kNoSound;
    return row >= 0 && row < static_cast<int>(_ambientTracks.size()) ? _ambientTracks[row] : kNoSound;
}

float AreaMusicPlayer::ambientVolumeScale() const {
    const int volume = _area->ambientAudio().ambientSoundDayVolume;
    return std::min(volume, kMaxVolume) / static_cast<float>(kMaxVolume);
}

// A fading track sounds until its fade ends.
bool AreaMusicPlayer::isMusicPlaying() {
    if (_fadeEnd) {
        uint32_t now = _services.system.clock.millis();
        if (now >= *_fadeEnd) {
            _music->stop();
            _fadeEnd.reset();
        } else {
            _music->setGainScale(static_cast<float>(*_fadeEnd - now) / kMusicFadeMilliseconds);
        }
    }
    return _music && _music->isPlaying();
}

// A fading loop sounds until its fade ends.
bool AreaMusicPlayer::isAmbientPlaying() {
    if (_ambientFadeEnd) {
        uint32_t now = _services.system.clock.millis();
        if (now >= *_ambientFadeEnd) {
            _ambient->stop();
            _ambientFadeEnd.reset();
        } else {
            _ambient->setGainScale(ambientVolumeScale() * (*_ambientFadeEnd - now) / kAmbientFadeMilliseconds);
        }
    }
    return _ambient && _ambient->isPlaying();
}

bool AreaMusicPlayer::playTrack(const std::string &resRef, bool takeLoadMusic) {
    if (resRef.empty()) {
        return false;
    }
    // TSL's background music takes over the load-screen music when it is the
    // same track, and lets it play on, once through.
    if (takeLoadMusic && _loadMusicHandover) {
        if (auto loadMusic = _loadMusicHandover(resRef)) {
            _music = std::move(loadMusic);
            _fadeEnd.reset();
            _playingResRef = resRef;
            return true;
        }
    }
    _music = _services.audio.mixer.play(_services.resource.audioClips.get(resRef), AudioType::Music, _musicGroupGain);
    _fadeEnd.reset();
    _playingResRef = resRef;
    return static_cast<bool>(_music);
}

void AreaMusicPlayer::fadeAndStop() {
    if (isMusicPlaying() && !_fadeEnd) {
        _fadeEnd = _services.system.clock.millis() + kMusicFadeMilliseconds;
    }
}

void AreaMusicPlayer::playStinger(const Track &track) {
    if (!_started || track.stingers[0].empty()) {
        return;
    }
    int count = track.stingers[1].empty() ? 1 : (track.stingers[2].empty() ? 2 : 3);
    const std::string &resRef = track.stingers[randomInt(0, count - 1)];
    _services.audio.mixer.play(_services.resource.audioClips.get(resRef), AudioType::Music, _stingerGroupGain);
}

// The delay is taken from the area as it is entered; a delay of zero repeats
// after a millisecond, and a negative one is read as unsigned, so the music
// does not come back.
void AreaMusicPlayer::load(std::shared_ptr<const Area> area) {
    _area = std::move(area);
    _musicPlaysThroughMovies = false;
    const auto &audio = _area->ambientAudio();
    _musicDelay = audio.musicDelay == 0 ? 1 : static_cast<uint32_t>(audio.musicDelay);
    _musicOn = audio.musicPlaying;
    _battleOn = audio.battleMusicPlaying;
    _ambientOn = audio.ambientSoundPlaying;
    _ambientFailed = false;
}

void AreaMusicPlayer::start() {
    _started = true;
    if (_battleOn) {
        switchBattleMusic(true, battleTrack());
    } else {
        switchMusic(_musicOn);
    }
    switchAmbientSound(_ambientOn);
}

// Music fades out; the ambient loop ends at once. Before arrival there is
// nothing to stop.
void AreaMusicPlayer::stopSounds() {
    if (!_started) {
        return;
    }
    switchMusic(false);
    _battleOn = false;
    _countdown = 0;
    if (_ambient) {
        _ambient->stop();
    }
    _ambientFadeEnd.reset();
    _ambientOn = false;
    _ambientFailed = false;
}

// A movie turns the music off only once the area's sounds have started: before
// that, the music starts on arrival as the area asks. The battle music, the
// pending restart and the ambient loop are switched off either way, so an area
// whose enter script plays a movie before arrival has no ambient loop.
void AreaMusicPlayer::stopSoundsForMovie() {
    if (_started) {
        stopSounds();
        return;
    }
    _battleOn = false;
    _countdown = 0;
    if (_ambient) {
        _ambient->stop();
    }
    _ambientFadeEnd.reset();
    _ambientOn = false;
    _ambientFailed = false;
}

// Nothing is on once the area is left, so nothing restarts until the next one
// is loaded; a fading track still fades out.
void AreaMusicPlayer::unload() {
    stopSounds();
    _started = false;
    _musicOn = false;
    _battleOn = false;
    _ambientOn = false;
    _ambientFailed = false;
    _countdown = 0;
    _area.reset();
}

// There is no time of day, so the day track is the background music. Before
// arrival the request is kept for the start.
void AreaMusicPlayer::switchMusic(bool play) {
    if (!_started) {
        _musicOn = play;
        return;
    }
    if (play) {
        if (!isMusicPlaying()) {
            _countdown = 0;
            if (!playTrack(track(_area->ambientAudio().musicDay).resRef, true)) {
                _countdown = kMusicRetryMilliseconds;
            }
        }
        _musicOn = true;
    } else {
        // A pending restart survives the fade.
        fadeAndStop();
        _musicOn = false;
    }
}

// An area without a battle track plays no battle music.
void AreaMusicPlayer::switchBattleMusic(bool play, const Track &battle) {
    if (play) {
        if (battle.resRef.empty()) {
            return;
        }
        _battleOn = true;
        if (!_started) {
            return;
        }
        if (isMusicPlaying()) {
            if (_playingResRef == battle.resRef) {
                return;
            }
            // The playing track fades out and battle music follows it.
            fadeAndStop();
            _musicOn = false;
            _countdown = kBattleHandOffMilliseconds;
        } else {
            _countdown = playTrack(battle.resRef) ? 0 : kMusicRetryMilliseconds;
        }
    } else if (_battleOn) {
        // Battle music ends on a stinger and background music returns after the delay.
        fadeAndStop();
        playStinger(battle);
        switchMusic(true);
        _battleOn = false;
    }
}

// The area-wide ambient is a non-positional loop at its priority group's
// volume, scaled by the area volume. A playing loop is left alone; a track
// that fails to play is not retried until the area asks again. Before arrival
// the request is kept for the start.
void AreaMusicPlayer::switchAmbientSound(bool play) {
    if (!play) {
        if (isAmbientPlaying() && !_ambientFadeEnd) {
            _ambientFadeEnd = _services.system.clock.millis() + kAmbientFadeMilliseconds;
        }
        _ambientOn = false;
        _ambientFailed = false;
        return;
    }
    if (!_started) {
        _ambientOn = true;
        return;
    }
    if (!isAmbientPlaying()) {
        _ambient = _services.audio.mixer.play(
            _services.resource.audioClips.get(ambientTrack(_area->ambientAudio().ambientSoundDay)),
            AudioType::Sound, _ambientGroupGain, true);
        _ambientFadeEnd.reset();
        _ambientFailed = !_ambient;
        if (_ambient) {
            _ambient->setGainScale(ambientVolumeScale());
        }
    }
    _ambientOn = true;
}

// A track that was playing stops and the background music starts again.
void AreaMusicPlayer::restartMusic(int previousTrack) {
    if (isMusicPlaying() && _playingResRef == track(previousTrack).resRef) {
        switchMusic(false);
    }
    switchMusic(true);
    _countdown = kTrackChangeRestartMilliseconds;
}

void AreaMusicPlayer::playMusic(const Area &area, bool play) {
    if (plays(area)) {
        switchMusic(play);
    }
}

void AreaMusicPlayer::playBattleMusic(const Area &area, bool play) {
    if (plays(area)) {
        switchBattleMusic(play, battleTrack());
    }
}

// A later delay of zero or less is ignored and the previous one is kept.
void AreaMusicPlayer::musicDelayChanged(const Area &area) {
    const int delay = area.ambientAudio().musicDelay;
    if (plays(area) && delay > 0) {
        _musicDelay = static_cast<uint32_t>(delay);
    }
}

void AreaMusicPlayer::musicDayTrackChanged(const Area &area, int previousTrack, bool playsThroughMovies) {
    if (plays(area)) {
        _musicPlaysThroughMovies = playsThroughMovies;
        restartMusic(previousTrack);
    }
}

void AreaMusicPlayer::musicNightTrackChanged(const Area &area, int previousTrack, bool playsThroughMovies) {
    if (plays(area)) {
        _musicPlaysThroughMovies = playsThroughMovies;
        restartMusic(previousTrack);
    }
}

// Battle music that was playing hands over to the new battle track.
void AreaMusicPlayer::battleMusicTrackChanged(const Area &area, int previousTrack) {
    if (!plays(area)) {
        return;
    }
    const auto &previous = track(previousTrack);
    if (!isMusicPlaying() || _playingResRef != previous.resRef) {
        return;
    }
    switchBattleMusic(false, previous);
    switchBattleMusic(true, battleTrack());
}

void AreaMusicPlayer::playAmbientSound(const Area &area, bool play) {
    if (plays(area)) {
        switchAmbientSound(play);
    }
}

// A new track waits for the old one to fade out; update() starts it.
void AreaMusicPlayer::ambientDayTrackChanged(const Area &area) {
    if (plays(area)) {
        switchAmbientSound(false);
        _ambientOn = true;
    }
}

void AreaMusicPlayer::ambientNightTrackChanged(const Area &area) {
    if (plays(area)) {
        switchAmbientSound(true);
    }
}

void AreaMusicPlayer::ambientDayVolumeChanged(const Area &area) {
    if (plays(area) && isAmbientPlaying() && !_ambientFadeEnd) {
        _ambient->setGainScale(ambientVolumeScale());
    }
}

// A track that ends is repeated after a gap: one second for battle music,
// the music delay for background music. Music is on only while an area is
// loaded.
void AreaMusicPlayer::update() {
    // An ambient loop that is on but has ended, after a fade or a track
    // change, starts again.
    if (_started && _ambientOn && !_ambientFailed && !isAmbientPlaying()) {
        switchAmbientSound(true);
    }
    if (isMusicPlaying()) {
        return;
    }
    uint32_t now = _services.system.clock.millis();
    if (_countdown > 0) {
        uint32_t elapsed = now - _lastUpdateTime;
        if (_countdown > elapsed) {
            _countdown -= elapsed;
        } else {
            _countdown = 0;
            if (_battleOn) {
                switchBattleMusic(true, battleTrack());
            } else {
                switchMusic(true);
            }
        }
    } else if (_musicOn || _battleOn) {
        _countdown = _battleOn ? kBattleRepeatGapMilliseconds : _musicDelay;
    }
    _lastUpdateTime = now;
}

} // namespace game

} // namespace reone
