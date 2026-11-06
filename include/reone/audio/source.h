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
    /** Sets whether the source starts again from the beginning when it ends. */
    void setLooping(bool loop);
    /** Scales the gain the source was created with; used to fade it out. */
    void setGainScale(float scale);
    /** Plays the source at \p pitch times its own rate, which raises or lowers its pitch. */
    void setPitch(float pitch);

    bool isPlaying() const { return _playing; }

    float duration() const;

private:
    std::shared_ptr<AudioClip> _stream;
    float _gain;
    bool _loop;
    std::optional<glm::vec3> _position;

    bool _inited {false};
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
};

} // namespace audio

} // namespace reone
