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

namespace reone {

namespace audio {

class AudioClip;

class AudioSource : boost::noncopyable {
public:
    AudioSource(std::shared_ptr<AudioClip> clip,
                float gain = 1.0f,
                bool loop = false,
                std::optional<glm::vec3> position = std::nullopt) :
        _stream(std::move(clip)),
        _gain(gain),
        _loop(loop),
        _position(std::move(position)) {
    }

    ~AudioSource() { deinit(); }

    /**
     * Prepares the sound. A clip whose frames share one format and rate goes
     * into one buffer whole and plays on the device's clock; any other clip is
     * streamed a few frames at a time.
     */
    void init();
    /**
     * Refills a stream's queue with the sound that follows. A stream that ran
     * out of queued sound since the last refill, because the frame came late,
     * plays on from where it ran out.
     */
    void render();

    void play();
    void stop();
    /** Pauses a playing source; resume() continues it. */
    void pause();
    /**
     * Continues a paused source, or starts one that was created paused. A
     * source that was stopped, cancelled or has ended is left as it is.
     */
    void resume();
    /** Counts the source as playing without starting it; resume() starts it. */
    void startPaused();

    void setPosition(glm::vec3 position);
    /** Sets whether the source starts again from the beginning when it ends, also while it plays. */
    void setLooping(bool loop);
    /** Scales the gain the source was created with; used to fade it out. */
    void setGainScale(float scale);
    /**
     * Sets the distances a positional source falls off over: full volume
     * within \p fullVolumeDistance of the listener, falling off with the
     * inverse of the distance beyond it, and not played beyond
     * \p cutoffDistance.
     */
    void setDistances(float fullVolumeDistance, float cutoffDistance);
    /** Plays the source at \p pitch times its own rate, which raises or lowers its pitch. */
    void setPitch(float pitch);
    /** Replaces the gain the source was created with, keeping its fade. */
    void setGain(float gain);

    /**
     * Marks the source as its owner's, which keeps it and may play it again,
     * rather than fire-and-forget. A full priority group that interrupts stops
     * an owned one-shot; a fire-and-forget one plays out.
     */
    void setOwned(bool owned) { _owned = owned; }

    bool isPlaying() const { return _playing; }
    bool isOwned() const { return _owned; }
    /** Whether the source plays its clip over and over. */
    bool isLooping() const { return _loop; }
    /**
     * Whether the source is within the distance it is played to from a
     * listener at \p listener. A source that isn't positional, or has no
     * distances, always is.
     */
    bool isWithinRange(const glm::vec3 &listener) const;
    /**
     * Whether a positional sound at \p position, not played beyond
     * \p cutoffDistance, is within range of a listener at \p listener.
     */
    static bool isWithinRange(const glm::vec3 &position, float cutoffDistance, const glm::vec3 &listener);

    float duration() const;

private:
    std::shared_ptr<AudioClip> _stream;
    float _gain;
    /** The fade applied over the gain. */
    float _gainScale {1.0f};
    bool _loop;
    std::optional<glm::vec3> _position;
    /** Beyond this distance from the listener the source is not played; none when it has no distances. */
    std::optional<float> _cutoffDistance;

    bool _inited {false};
    bool _owned {false};
    bool _playing {false};
    /** Paused, or created paused, and not resumed since. */
    bool _paused {false};

    std::vector<uint32_t> _buffers;
    bool _streaming {false};
    uint32_t _source {0};
    int _nextFrame {0};
    int _nextBuffer {0};

    bool _playingDirty {false};
    bool _positionDirty {false};

    void deinit();
    /** Sets the source's gain from its own, its group's and its fade. */
    void applyGain();
};

} // namespace audio

} // namespace reone
