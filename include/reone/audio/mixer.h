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

#include <chrono>
#include <set>

#include "arbiter.h"
#include "options.h"
#include "source.h"
#include "types.h"

namespace reone {

namespace audio {

class AudioClip;
class IContext;

class IAudioMixer {
public:
    virtual ~IAudioMixer() = default;

    virtual void render() = 0;
    virtual void stop(AudioType type) = 0;
    virtual void stopAll() = 0;
    /**
     * Holds every sound while a movie plays, or lets them go on. Movie audio
     * and the source that plays through the movie keep playing; every other
     * source, music included, waits paused, and a source started while sounds
     * are held starts held.
     *
     * @param held whether sounds are held for a movie
     * @param playingThrough the source that keeps playing when the hold
     *        begins; none for a movie that holds every sound
     */
    virtual void setSoundsHeldForMovie(bool held, const std::shared_ptr<AudioSource> &playingThrough) = 0;
    /**
     * Pauses the sources playing now, apart from those in one of the kept
     * priority groups. Until resumeSources, a source started later starts
     * paused unless it is in a kept group.
     */
    virtual void pauseSources(const std::set<int> &keptGroups) = 0;
    /** Resumes the sources pauseSources paused, and lets new sources play. */
    virtual void resumeSources() = 0;
    /** Sets the priority groups' rows, by group. */
    virtual void setPriorityGroups(std::vector<PriorityGroup> groups) = 0;
    /**
     * Sets the priority groups whose sources play on streamed voices, as the
     * game streams music, ambience and speech; the others play on positional
     * or 2D voices.
     */
    virtual void setStreamedGroups(std::set<int> groups) = 0;
    /** The row of priority group \p group. */
    virtual PriorityGroup priorityGroup(int group) const = 0;
    /**
     * Pauses the sources while the window has lost focus, and a source
     * started meanwhile starts paused. An unstreamed source playing in a
     * group the sound mode in force keeps plays on; streams and movie audio
     * wait with the rest. Regaining focus resumes them, apart from those a
     * sound mode or a movie holds.
     */
    virtual void setFocusLost(bool lost) = 0;
    /** Fades every sound out over half a second, or back in. */
    virtual void setMuted(bool muted) = 0;
    /** Renders the sources until the fade setMuted began has ended. */
    virtual void waitForFade() = 0;
    /**
     * Fades \p source out and then stops it, rendering the sources while the
     * caller waits. The gain falls at the rate of a fade to silence over
     * \p fadeMilliseconds, and the source stops once \p heardMilliseconds
     * have passed. With no source the wait is the same.
     */
    virtual void waitForSourceFade(const std::shared_ptr<AudioSource> &source, uint32_t fadeMilliseconds, uint32_t heardMilliseconds) = 0;
    /**
     * Sets the volume of one kind of audio, from 0 to AudioOptions::kMaxVolume,
     * and applies it to the sources of that kind, those already playing and
     * those to come. The volume is kept in the options.
     */
    virtual void setVolume(AudioType type, int volume) = 0;
    /**
     * Sets the music's volume, from 0 to 1, in place of the music volume the
     * options keep, until that is next set. Music already playing takes it at
     * once. The options are not changed.
     */
    virtual void setMusicVolume(float volume) = 0;
    /**
     * Ducks music and the sounds of \p groups to a fifth of their volume, or
     * lifts the ducking. Sources already playing take it at once.
     */
    virtual void setDucked(bool ducked, const std::set<int> &groups) = 0;

    /**
     * Starts a source in its priority group: the group's volume scales its
     * gain, a positional source falls off over the group's distances unless
     * it has its own, and the pitch strays at random by the group's playback
     * variance unless the source has its own. Under a sound mode it starts paused unless the
     * mode keeps its group. A positional one-shot that starts beyond its
     * distance from the listener is not played; a looping one waits paused
     * until the listener comes within it. Its group's room and the voices
     * are shared as SourceArbiter tells: a one-shot without room or a voice
     * is not played, and a loop without them waits paused. A source not
     * played has no OpenAL source.
     *
     * @param group the source's priority group; none for movie audio
     * @param distances the positional source's own distances, which take the
     *        place of its group's
     * @param pitchVariance how far at random the source strays in pitch, in
     *        place of its group's playback variance: up by up to this
     *        fraction, or down by up to half of it, at most 1; none takes the
     *        group's
     * @return the source, played or not, or none when there is no clip to play
     */
    virtual std::shared_ptr<AudioSource> play(
        std::shared_ptr<AudioClip> clip,
        AudioType type,
        std::optional<int> group,
        float gain = 1.0f,
        bool loop = false,
        std::optional<glm::vec3> position = std::nullopt,
        std::optional<SoundDistances> distances = std::nullopt,
        std::optional<float> pitchVariance = std::nullopt) = 0;
};

class AudioMixer : public IAudioMixer, boost::noncopyable {
public:
    AudioMixer(AudioOptions &options, IContext &context) :
        _options(options),
        _context(context) {
        _arbiter.setVoiceCounts(options.voices2D, options.voices3D);
    }

    void render() override;
    void stop(AudioType type) override;
    void stopAll() override;
    void setSoundsHeldForMovie(bool held, const std::shared_ptr<AudioSource> &playingThrough) override;
    void pauseSources(const std::set<int> &keptGroups) override;
    void resumeSources() override;
    void setPriorityGroups(std::vector<PriorityGroup> groups) override;
    void setStreamedGroups(std::set<int> groups) override;
    PriorityGroup priorityGroup(int group) const override;
    void setFocusLost(bool lost) override;
    void setMuted(bool muted) override;
    void waitForFade() override;
    void waitForSourceFade(const std::shared_ptr<AudioSource> &source, uint32_t fadeMilliseconds, uint32_t heardMilliseconds) override;
    void setVolume(AudioType type, int volume) override;
    void setMusicVolume(float volume) override;
    void setDucked(bool ducked, const std::set<int> &groups) override;

    std::shared_ptr<AudioSource> play(
        std::shared_ptr<AudioClip> clip,
        AudioType type,
        std::optional<int> group,
        float gain = 1.0f,
        bool loop = false,
        std::optional<glm::vec3> position = std::nullopt,
        std::optional<SoundDistances> distances = std::nullopt,
        std::optional<float> pitchVariance = std::nullopt) override;

private:
    struct ActiveSource {
        std::shared_ptr<AudioSource> source;
        AudioType type {AudioType::Sound};
        /** The priority group a sound mode keeps or pauses the source by, if it is in one. */
        std::optional<int> group;
        /** Paused by a sound mode. */
        bool modePaused {false};
        /** Paused while the window has lost focus. */
        bool focusPaused {false};
        /** Held while a movie plays. */
        bool movieHeld {false};
        /** A looping positional source waiting for the listener to come within its distance. */
        bool outOfRange {false};
        /** Its id with the arbiter; none for a source in no group, or one the arbiter has let go. */
        std::optional<uint32_t> slotId;
        /** The gain asked for at play, before the volume of its kind. */
        float gain {1.0f};
        /** The source was played at a position. */
        bool positional {false};
    };

    AudioOptions &_options;
    IContext &_context;

    std::vector<ActiveSource> _sources;
    /** Shares the groups' room and the voices among the sources in a group. */
    SourceArbiter _arbiter;
    std::set<int> _streamedGroups;
    bool _soundsHeldForMovie {false};
    /** The priority groups the sound mode in force keeps playing; none without a mode. */
    std::optional<std::set<int>> _modeKeptGroups;
    /** The window has lost focus, so the sources are paused and new ones start paused. */
    bool _focusLost {false};
    /** Sound is faded out, or fading out. */
    bool _muted {false};
    /** The music's volume a script set, while it stands in for the options'. */
    std::optional<float> _musicVolume;
    /** Music and the sounds of _duckedGroups play ducked. */
    bool _ducked {false};
    std::set<int> _duckedGroups;
    /** When the fade setMuted began started; none once it has ended. */
    std::optional<std::chrono::steady_clock::time_point> _fadeStart;

    /**
     * The gain a source plays at: the volume of its kind times its priority
     * group's volume and the gain asked for, weighed by the 2D3D bias. A
     * positional source raises that to a power below one, so it rises
     * steeply at first; the others keep it linear.
     */
    float gainByType(AudioType type, float gain, bool positional, std::optional<int> group) const;
    /** The sources of \p type take its volume. */
    void applyVolume(AudioType type);
    /** Resumes a source that nothing holds paused any more. */
    void resumeIfFree(ActiveSource &active);
    /** The arbiter holds the source back: it waits for room or a voice, or is out of range. */
    bool heldBySlot(const ActiveSource &active) const;
    /** Lets the arbiter go of the source. */
    void release(ActiveSource &active);
    /**
     * Tells the arbiter which sources their owners keep, and lets go of those
     * stopped since the last render.
     */
    void syncArbiter();
    /** Stops, pauses or resumes the sources the arbiter has changed. */
    void applyChanges(const std::vector<SlotChange> &changes);
    /**
     * Holds or ends a positional source the listener at \p listener is beyond
     * the distance of, and resumes a held one it is within again.
     *
     * @return false when the source has ended
     */
    bool updateRange(ActiveSource &active, const glm::vec3 &listener);
    /** Sets the overall gain from the fade under way. */
    void renderFade();
};

} // namespace audio

} // namespace reone
