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

    /**
     * Reads the music, ambient sound and priority group tables once, at start-up.
     *
     * @param tsl whether the title is TSL, which reads the PC volumes of the
     *            priority groups and plays the music group louder
     */
    void init(bool tsl);

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
    /** Begin playing what the loaded area asks for. */
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
    /** Advance the fades, repeat ended tracks and restart an ended ambient loop. */
    void update();

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
    void updateMusicFade() { isMusicPlaying(); }
    /** The area set its battle track; previousTrack is the row it replaced. */
    void battleMusicTrackChanged(const Area &area, int previousTrack);

    /** The area turned its ambient loop on or off. */
    void playAmbientSound(const Area &area, bool play);
    /** The area set its ambient day track: the loop fades out and the new one follows. */
    void ambientDayTrackChanged(const Area &area);
    /** The area set its ambient night track. There is no night; the ambient loop is turned on again. */
    void ambientNightTrackChanged(const Area &area);
    /** The area set its ambient day volume. */
    void ambientDayVolumeChanged(const Area &area);

    /** The gain of streamed music, menu music included: its priority group's volume as a share of the loudest. */
    float musicGroupGain() const { return _musicGroupGain; }
    /** The gain of the area-wide ambient priority group, which the credits music plays in too. */
    float ambientGroupGain() const { return _ambientGroupGain; }

private:
    /** An ambientmusic.2da row: the track and the stingers that may end it. */
    struct Track {
        /** The track's sound; empty for none. */
        std::string resRef;
        /** The stingers, the empty ones last. */
        std::array<std::string, 3> stingers;
    };

    ServicesView &_services;
    std::function<std::shared_ptr<audio::AudioSource>(const std::string &)> _loadMusicHandover;

    // Tables

    /** The ambientmusic.2da tracks, by row. */
    std::vector<Track> _tracks;
    /** The ambientsound.2da sounds, by row; empty for none. */
    std::vector<std::string> _ambientTracks;
    /** The volume of the streamed music priority group, as a share of the loudest. */
    float _musicGroupGain {0.0f};
    /** The volume of the stinger priority group, as a share of the loudest. */
    float _stingerGroupGain {0.0f};
    /** The volume of the area-wide ambient priority group, as a share of the loudest. */
    float _ambientGroupGain {0.0f};

    // END Tables

    /** The area being played, from its load until it is unloaded. */
    std::shared_ptr<const Area> _area;
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
    /** The area's ambient volume as a share of the loudest. */
    float ambientVolumeScale() const;

    /** Whether music sounds, a fading track until its fade ends. */
    bool isMusicPlaying();
    /** Whether the ambient loop sounds, a fading loop until its fade ends. */
    bool isAmbientPlaying();
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
