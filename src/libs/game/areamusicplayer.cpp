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
#include <boost/algorithm/string/predicate.hpp>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/di/services.h"
#include "reone/game/object/area.h"
#include "reone/game/twodautil.h"
#include "reone/game/types.h"
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
static constexpr uint32_t kDialogAmbientFadeMilliseconds = 500;

// KotOR never starts this night track.
static constexpr char kKotorSkippedNightTrack[] = "mus_area_townint";

void AreaMusicPlayer::init(bool tsl) {
    _tsl = tsl;
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
    const auto &audio = _area->ambientAudio();
    const int volume = _night ? audio.ambientSoundNightVolume : audio.ambientSoundDayVolume;
    return std::min(volume, kMaxVolume) / static_cast<float>(kMaxVolume);
}

int AreaMusicPlayer::musicTrackRow() const {
    return _night ? _area->ambientAudio().musicNight : _area->ambientAudio().musicDay;
}

int AreaMusicPlayer::ambientTrackRow() const {
    return _night ? _area->ambientAudio().ambientSoundNight : _area->ambientAudio().ambientSoundDay;
}

std::string AreaMusicPlayer::backgroundTrack() const {
    return track(musicTrackRow()).resRef;
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

// A fading conversation track sounds until its fade ends.
bool AreaMusicPlayer::isDialogAmbientPlaying() {
    if (_dialogAmbientFadeEnd) {
        uint32_t now = _services.system.clock.millis();
        if (now >= *_dialogAmbientFadeEnd) {
            _dialogAmbient->stop();
            _dialogAmbientFadeEnd.reset();
        } else {
            _dialogAmbient->setGainScale(static_cast<float>(*_dialogAmbientFadeEnd - now) / kDialogAmbientFadeMilliseconds);
        }
    }
    return _dialogAmbient && _dialogAmbient->isPlaying();
}

// The track plays as a sound effect in the area-wide ambient priority group.
void AreaMusicPlayer::playDialogAmbientTrack(const std::string &resRef) {
    if (_dialogAmbient) {
        _dialogAmbient->stop();
        _dialogAmbient.reset();
    }
    _dialogAmbientFadeEnd.reset();
    if (!resRef.empty()) {
        _dialogAmbient = _services.audio.mixer.play(_services.resource.audioClips.get(resRef), AudioType::Sound, kAreaWideAmbientSoundGroup);
    }
}

void AreaMusicPlayer::fadeDialogAmbientTrack() {
    if (isDialogAmbientPlaying() && !_dialogAmbientFadeEnd) {
        _dialogAmbientFadeEnd = _services.system.clock.millis() + kDialogAmbientFadeMilliseconds;
    }
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
    _music = _services.audio.mixer.play(_services.resource.audioClips.get(resRef), AudioType::Music, kMusicSoundGroup);
    _fadeEnd.reset();
    _playingResRef = resRef;
    return static_cast<bool>(_music);
}

void AreaMusicPlayer::fadeAndStop() {
    if (isMusicPlaying() && !_fadeEnd) {
        _fadeEnd = _services.system.clock.millis() + kMusicFadeMilliseconds;
    }
}

// One stinger plays at a time: a new one cuts the last.
void AreaMusicPlayer::playStinger(const Track &track) {
    if (!_started || track.stingers[0].empty()) {
        return;
    }
    int count = track.stingers[1].empty() ? 1 : (track.stingers[2].empty() ? 2 : 3);
    const std::string &resRef = track.stingers[randomInt(0, count - 1)];
    if (_stinger) {
        _stinger->stop();
    }
    _stinger = _services.audio.mixer.play(_services.resource.audioClips.get(resRef), AudioType::Music, kStingerSoundGroup);
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

// What plays is what the player last had: the area's settings as it was
// entered, as changed since.
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
// is loaded; a fading track still fades out, the conversation's too.
void AreaMusicPlayer::unload() {
    stopSounds();
    fadeDialogAmbientTrack();
    _started = false;
    _musicOn = false;
    _battleOn = false;
    _ambientOn = false;
    _ambientFailed = false;
    _countdown = 0;
    _area.reset();
}

// The background music is the track for the part of the day: the night track
// at dusk and at night, else the day track. KotOR never starts its town
// interior night track. Before arrival the request is kept for the start.
void AreaMusicPlayer::switchMusic(bool play) {
    if (!_started) {
        _musicOn = play;
        return;
    }
    if (play) {
        const std::string resRef = backgroundTrack();
        if (!_tsl && _night && boost::iequals(resRef, kKotorSkippedNightTrack)) {
            _musicOn = true;
            return;
        }
        if (!isMusicPlaying()) {
            _countdown = 0;
            if (!playTrack(resRef, true)) {
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
            _services.resource.audioClips.get(ambientTrack(ambientTrackRow())),
            AudioType::Sound, kAreaWideAmbientSoundGroup, 1.0f, true);
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

// A new track for the part of the day it is waits for the old one to fade
// out; update() starts it. A track for the other part waits for it.
void AreaMusicPlayer::ambientDayTrackChanged(const Area &area) {
    if (plays(area) && !_night) {
        switchAmbientSound(false);
        _ambientOn = true;
    }
}

void AreaMusicPlayer::ambientNightTrackChanged(const Area &area) {
    if (plays(area) && _night) {
        switchAmbientSound(false);
        _ambientOn = true;
    }
}

void AreaMusicPlayer::ambientDayVolumeChanged(const Area &area) {
    if (plays(area) && !_night && isAmbientPlaying() && !_ambientFadeEnd) {
        _ambient->setGainScale(ambientVolumeScale());
    }
}

void AreaMusicPlayer::ambientNightVolumeChanged(const Area &area) {
    if (plays(area) && _night && isAmbientPlaying() && !_ambientFadeEnd) {
        _ambient->setGainScale(ambientVolumeScale());
    }
}

// The background music playing the other part's track starts again with this
// part's; battle music plays on. The ambient loop of another track fades out
// and this part's follows; the same loop takes this part's volume.
void AreaMusicPlayer::setNight(bool night) {
    if (_night == night) {
        return;
    }
    if (!_area) {
        _night = night;
        return;
    }
    const std::string previousMusic = backgroundTrack();
    const std::string previousAmbient = ambientTrack(ambientTrackRow());
    _night = night;
    if (!_started) {
        return;
    }
    // A loop that failed is tried again with this part's track.
    if (previousAmbient != ambientTrack(ambientTrackRow())) {
        _ambientFailed = false;
    }
    if (!_battleOn && _musicOn && isMusicPlaying() && !_fadeEnd && _playingResRef == previousMusic &&
        previousMusic != backgroundTrack()) {
        switchMusic(false);
        switchMusic(true);
        _countdown = kTrackChangeRestartMilliseconds;
    }
    if (_ambientOn && isAmbientPlaying() && !_ambientFadeEnd) {
        if (previousAmbient != ambientTrack(ambientTrackRow())) {
            switchAmbientSound(false);
            _ambientOn = true;
        } else {
            _ambient->setGainScale(ambientVolumeScale());
        }
    }
}

// One of two thunderclaps or two distant rolls, at random, while the area's
// ambient sound is on, whether or not its loop could play.
void AreaMusicPlayer::thunder(bool close) {
    if (!_started || !_ambientOn) {
        return;
    }
    static const std::array<std::string, 2> kClose {"as_wt_thundercl1", "as_wt_thundercl2"};
    static const std::array<std::string, 2> kDistant {"as_wt_thunderds1", "as_wt_thunderds2"};
    const auto &sounds = close ? kClose : kDistant;
    _services.audio.mixer.play(_services.resource.audioClips.get(sounds[randomInt(0, 1)]), AudioType::Sound,
                               kSingleShotGlobalSoundGroup);
}

// Nothing restarts after the fades.
void AreaMusicPlayer::fadeOutSounds() {
    if (!_started) {
        return;
    }
    switchMusic(false);
    _battleOn = false;
    _countdown = 0;
    switchAmbientSound(false);
}

// A track that ends is repeated after a gap: one second for battle music,
// the music delay for background music. Music is on only while an area is
// loaded.
// The repeat gap counts the frame's time, and only while the clock runs: not
// while every sound waits. Music is presentation: it counts through pauses
// and menus, where it keeps playing, unlike the world's sounds, which count
// seconds of play.
void AreaMusicPlayer::update(float dt, bool clockRuns) {
    isDialogAmbientPlaying();
    // An ambient loop that is on but has ended, after a fade or a track
    // change, starts again.
    if (_started && _ambientOn && !_ambientFailed && !isAmbientPlaying()) {
        switchAmbientSound(true);
    }
    if (isMusicPlaying() || !clockRuns) {
        return;
    }
    if (_countdown > 0.0f) {
        _countdown -= dt * 1000.0f;
        if (_countdown <= 0.0f) {
            _countdown = 0.0f;
            if (_battleOn) {
                switchBattleMusic(true, battleTrack());
            } else {
                switchMusic(true);
            }
        }
    } else if (_musicOn || _battleOn) {
        _countdown = static_cast<float>(_battleOn ? kBattleRepeatGapMilliseconds : _musicDelay);
    }
}

} // namespace game

} // namespace reone
