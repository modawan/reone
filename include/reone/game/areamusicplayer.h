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

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/noncopyable.hpp>

namespace reone {

namespace audio {

class AudioSource;

}

namespace game {

class Area;

struct ServicesView;

/**
 * Plays the current area's background and battle music and its area-wide
 * ambient loop. The area owns the settings: its tracks, delay and volume,
 * and whether each part is asked to play. The player reads them from the area
 * and owns only what is playing: it fades tracks out, hands over to battle
 * music, repeats tracks after a gap and retries a track that fails to play.
 *
 * The area tells the player when a setting changes; the player follows only
 * the area it was loaded with.
 */
class AreaMusicPlayer : boost::noncopyable {
public:
    AreaMusicPlayer(ServicesView &services) :
        _services(services) {
    }

    /** Reads the music and ambient sound tables once, at start-up. */
    void init(bool tsl);

    /**
     * Night falls (dusk and night) or day breaks (dawn and day). Music and an
     * ambient loop playing for the other part of the day start again with
     * this part's track; the ambient loop takes this part's volume.
     */
    void setNight(bool night);

    /**
     * What hands over the playing load-screen music when the background
     * music asks for the same track; it returns none when it does not. Only
     * TSL sets it.
     */
    void setLoadMusicHandover(std::function<std::shared_ptr<audio::AudioSource>(const std::string &)> handover) {
        _loadMusicHandover = std::move(handover);
    }

    /**
     * Take the area being entered. Its music and ambient loop wait for the
     * start; whether each is on starts from what the area asks for.
     */
    void load(std::shared_ptr<const Area> area);
    /**
     * Begin playing, or play again, what the player last had on: the battle
     * music, else the background music, and the ambient loop; what is off
     * fades out. Music and an ambient loop that a stop turned off stay off.
     * In TSL background music that has to start the loading screen's track
     * carries that music on.
     */
    void start();
    /** Fade out the music, end the ambient loop and forget the pending restart and battle state. */
    void stopSounds();
    /**
     * A movie starts. As stopSounds once the area's sounds have started;
     * before then the music is left to start on arrival, but the battle
     * music, the pending restart and the ambient loop are switched off.
     */
    void stopSoundsForMovie();
    /** Stop for the area being left; nothing plays until the next area is loaded and started. */
    void unload();
    /**
     * Advance the fades, repeat ended tracks and restart an ended ambient loop.
     *
     * @param clockRuns whether the gap before a track repeats counts down; it
     *        does not while every sound waits
     */
    void update(bool clockRuns = true);
    /** The window has lost focus: the gap before a track repeats does not count down. */
    void holdClock();

    /** The area turned its background music on or off. */
    void playMusic(const Area &area, bool play);
    /** The area set its music delay; a delay of zero or less leaves the one in use. */
    void musicDelayChanged(const Area &area);
    /** The area turned its battle music on or off. */
    void playBattleMusic(const Area &area, bool play);
    /**
     * The area set its day track, restarting the background music.
     *
     * @param previousTrack the row it replaced
     * @param playsThroughMovies whether the background music plays on through
     *        a movie that lets music play
     */
    void musicDayTrackChanged(const Area &area, int previousTrack, bool playsThroughMovies);
    /**
     * The area set its night track, restarting the background music.
     *
     * @param previousTrack the row it replaced
     * @param playsThroughMovies whether the background music plays on through
     *        a movie that lets music play
     */
    void musicNightTrackChanged(const Area &area, int previousTrack, bool playsThroughMovies);
    /** The background music that plays on through a movie that lets music play; none when it does not. */
    std::shared_ptr<audio::AudioSource> musicPlayingThroughMovies() const {
        return _musicPlaysThroughMovies ? _music : nullptr;
    }
    /** Advance the music's fade, as it goes on while a movie plays. */
    void updateMusicFade() {
        isMusicPlaying();
        isDialogAmbientPlaying();
    }
    /** The area set its battle track; previousTrack is the row it replaced. */
    void battleMusicTrackChanged(const Area &area, int previousTrack);

    /** The area turned its ambient loop on or off. */
    void playAmbientSound(const Area &area, bool play);
    /** The area set its ambient day track: the loop fades out and the new one follows. */
    void ambientDayTrackChanged(const Area &area);
    /** The area set its ambient night track: at night the loop fades out and the new one follows. */
    void ambientNightTrackChanged(const Area &area);
    /** The area set its ambient day volume. */
    void ambientDayVolumeChanged(const Area &area);
    /** The area set its ambient night volume. */
    void ambientNightVolumeChanged(const Area &area);
    /**
     * The end credits roll: the music fades out and the ambient loop fades
     * out over its usual fade, then nothing restarts.
     */
    void fadeOutSounds();
    /**
     * TSL: lightning strikes, and while the ambient loop plays its thunder is
     * heard everywhere, close or distant.
     */
    void thunder(bool close);

    /**
     * A conversation shows its first entry: the track a conversation started
     * before stops at once, and the conversation's own plays through once, on
     * after it ends.
     *
     * @param resRef the conversation's track; empty for none
     */
    void playDialogAmbientTrack(const std::string &resRef);
    /** The conversation's track fades out. */
    void fadeDialogAmbientTrack();

private:
    /** An ambientmusic.2da row: the track and the stingers that may end it. */
    struct Track {
        /** The track's sound; empty for none. */
        std::string resRef;
        /** The stingers, the empty ones last. */
        std::array<std::string, 3> stingers;
    };

    ServicesView &_services;
    bool _tsl {false};
    /** Dusk or night: the night tracks and the night ambient volume are in use. */
    bool _night {false};
    std::function<std::shared_ptr<audio::AudioSource>(const std::string &)> _loadMusicHandover;

    // Tables

    /** The ambientmusic.2da tracks, by row. */
    std::vector<Track> _tracks;
    /** The ambientsound.2da sounds, by row; empty for none. */
    std::vector<std::string> _ambientTracks;

    // END Tables

    /** The area being played, from its load until it is unloaded. */
    std::shared_ptr<const Area> _area;
    /** The track the last conversation started, which outlives it. */
    std::shared_ptr<audio::AudioSource> _dialogAmbient;
    /** When the conversation's fading track ends. */
    std::optional<uint32_t> _dialogAmbientFadeEnd;
    /** The area has been arrived in, and its music and ambient loop play. */
    bool _started {false};
    /** The background music plays on through a movie that lets music play; set by the last track change. */
    bool _musicPlaysThroughMovies {false};

    // Playing state. Whether each part is on starts from the area's settings
    // on load and then follows what plays: a hand-over to battle music turns
    // the background music off, and menu music or a movie turns everything off.

    bool _musicOn {false};
    bool _battleOn {false};
    bool _ambientOn {false};
    /** The ambient track failed to play, and is not retried until the area asks again. */
    bool _ambientFailed {false};

    /**
     * Milliseconds before the background music repeats. It is taken from the
     * area on load, where zero is one and a negative delay is read as unsigned,
     * and follows later changes above zero.
     */
    uint32_t _musicDelay {0};
    /** Milliseconds until the music track is repeated, retried or handed over. */
    uint32_t _countdown {0};
    /** The clock time of the last update, in milliseconds. */
    uint32_t _lastUpdateTime {0};
    /** The stinger playing, if any; a new one cuts it. */
    std::shared_ptr<audio::AudioSource> _stinger;
    /** The music track playing or fading, if any. */
    std::shared_ptr<audio::AudioSource> _music;
    /** The sound of the music track last started. */
    std::string _playingResRef;
    /** The clock time the music's fade ends, while it fades. */
    std::optional<uint32_t> _fadeEnd;
    /** The ambient loop playing or fading, if any. */
    std::shared_ptr<audio::AudioSource> _ambient;
    /** The clock time the ambient loop's fade ends, while it fades. */
    std::optional<uint32_t> _ambientFadeEnd;

    /** Whether \p area is the one the player follows. */
    bool plays(const Area &area) const { return _area.get() == &area; }

    /** The ambientmusic.2da row \p row; a row outside the table is blank. */
    const Track &track(int row) const;
    /** The area's battle track. */
    const Track &battleTrack() const;
    /** The sound of the ambientsound.2da row \p row; empty for a row outside the table. */
    const std::string &ambientTrack(int row) const;
    /** The area's ambient volume for the part of the day, as a share of the loudest. */
    float ambientVolumeScale() const;
    /** The background music's row for the part of the day. */
    int musicTrackRow() const;
    /** The ambient loop's row for the part of the day. */
    int ambientTrackRow() const;
    /** The background music's sound for the part of the day; empty for none. */
    std::string backgroundTrack() const;

    /** Whether music sounds, a fading track until its fade ends. */
    bool isMusicPlaying();
    /** Whether the ambient loop sounds, a fading loop until its fade ends. */
    bool isAmbientPlaying();
    /** Whether the conversation's track sounds, a fading one until its fade ends. */
    bool isDialogAmbientPlaying();
    /**
     * Plays the music track \p resRef; false when it does not play.
     * @param takeLoadMusic whether the playing load-screen music of the same
     *        track is taken over instead, as the background music does
     */
    bool playTrack(const std::string &resRef, bool takeLoadMusic = false);
    /** Fades out the music that is playing. */
    void fadeAndStop();
    /** Plays one of the track's stingers at random, once the area has been arrived in. */
    void playStinger(const Track &track);

    /** Turns the background music on or off; before arrival the request waits for the start. */
    void switchMusic(bool play);
    /** Turns the battle music of \p battle on or off; an area without one plays none. */
    void switchBattleMusic(bool play, const Track &battle);
    /** Turns the ambient loop on or off; before arrival the request waits for the start. */
    void switchAmbientSound(bool play);
    /** Restarts the background music, stopping a track of \p previousTrack that plays. */
    void restartMusic(int previousTrack);
};

} // namespace game

} // namespace reone
