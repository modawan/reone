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

#include "reone/audio/source.h"

#include "reone/audio/clip.h"
#include "reone/system/threadutil.h"

namespace reone {

namespace audio {

static constexpr int kMaxBufferCount = 8;

static int getALFormat(AudioFormat format) {
    switch (format) {
    case AudioFormat::Mono8:
        return AL_FORMAT_MONO8;
    case AudioFormat::Mono16:
        return AL_FORMAT_MONO16;
    case AudioFormat::Stereo8:
        return AL_FORMAT_STEREO8;
    case AudioFormat::Stereo16:
        return AL_FORMAT_STEREO16;
    default:
        throw std::invalid_argument("Invalid audio format: " + std::to_string(static_cast<int>(format)));
    }
}

static void fillBuffer(const AudioClip::Frame &frame, uint32_t buffer) {
    alBufferData(
        buffer,
        getALFormat(frame.format),
        &frame.samples[0],
        static_cast<int>(frame.samples.size()),
        frame.sampleRate);
}

// A clip whose frames share one format and rate goes into one buffer whole, so
// its playback runs on the device's clock whatever the frame rate or however
// long a load holds the main loop; only a clip that changes format part-way is
// streamed a few frames at a time.
static bool fillWholeClip(const AudioClip &clip, uint32_t buffer) {
    int frameCount = clip.getFrameCount();
    if (frameCount < 2) {
        return false;
    }
    const auto &first = clip.getFrame(0);
    size_t size = 0;
    for (int i = 0; i < frameCount; ++i) {
        const auto &frame = clip.getFrame(i);
        if (frame.format != first.format || frame.sampleRate != first.sampleRate) {
            return false;
        }
        size += frame.samples.size();
    }
    if (size == 0) {
        return false;
    }
    ByteBuffer samples;
    samples.reserve(size);
    for (int i = 0; i < frameCount; ++i) {
        const auto &frame = clip.getFrame(i);
        samples.insert(samples.end(), frame.samples.begin(), frame.samples.end());
    }
    alBufferData(
        buffer,
        getALFormat(first.format),
        samples.data(),
        static_cast<int>(samples.size()),
        first.sampleRate);
    return true;
}

void AudioSource::init() {
    if (_inited) {
        return;
    }
    checkMainThread();

    int frameCount = _stream->getFrameCount();
    if (frameCount > 1) {
        uint32_t whole = 0;
        alGenBuffers(1, &whole);
        if (fillWholeClip(*_stream, whole)) {
            _buffers.assign(1, whole);
            _streaming = false;
            alGenSources(1, &_source);
            alSourcef(_source, AL_GAIN, _gain);
            if (_position) {
                alSource3f(_source, AL_POSITION, _position->x, _position->y, _position->z);
            } else {
                alSourcei(_source, AL_SOURCE_RELATIVE, AL_TRUE);
            }
            alSourcei(_source, AL_BUFFER, whole);
            alSourcei(_source, AL_LOOPING, _loop);
            _inited = true;
            return;
        }
        alDeleteBuffers(1, &whole);
    }
    int bufferCount = std::min(std::max(frameCount, 1), kMaxBufferCount);

    _buffers.resize(bufferCount);
    _streaming = bufferCount > 1;

    alGenBuffers(bufferCount, &_buffers[0]);
    alGenSources(1, &_source);
    alSourcef(_source, AL_GAIN, _gain);

    if (_position) {
        alSource3f(_source, AL_POSITION, _position->x, _position->y, _position->z);
    } else {
        alSourcei(_source, AL_SOURCE_RELATIVE, AL_TRUE);
    }
    if (_streaming) {
        for (int i = 0; i < bufferCount; ++i) {
            auto &frame = _stream->getFrame(_nextFrame++);
            fillBuffer(frame, _buffers[i]);
        }
        alSourceQueueBuffers(_source, bufferCount, &_buffers[0]);
    } else {
        auto &frame = _stream->getFrame(0);
        fillBuffer(frame, _buffers[0]);
        alSourcei(_source, AL_BUFFER, _buffers[0]);
        alSourcei(_source, AL_LOOPING, _loop);
    }

    _inited = true;
}

void AudioSource::deinit() {
    if (!_inited) {
        return;
    }
    checkMainThread();
    if (_source) {
        alSourceStop(_source);
        alDeleteSources(1, &_source);
        _source = 0;
    }
    if (!_buffers.empty()) {
        alDeleteBuffers(static_cast<int>(_buffers.size()), &_buffers[0]);
        _buffers.clear();
    }
    _inited = false;
}

void AudioSource::render() {
    if (!_source) {
        return;
    }
    if (!_streaming) {
        ALint state = 0;
        alGetSourcei(_source, AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED) {
            _playing = false;
        }
        return;
    }
    ALint processed = 0;
    alGetSourcei(_source, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0) {
        alSourceUnqueueBuffers(_source, 1, &_buffers[_nextBuffer]);
        if (_loop && _nextFrame == _stream->getFrameCount()) {
            _nextFrame = 0;
        }
        if (_nextFrame < _stream->getFrameCount()) {
            auto &frame = _stream->getFrame(_nextFrame++);
            fillBuffer(frame, _buffers[_nextBuffer]);
            alSourceQueueBuffers(_source, 1, &_buffers[_nextBuffer]);
        }
        _nextBuffer = (_nextBuffer + 1) % static_cast<int>(_buffers.size());
    }
    ALint queued = 0;
    alGetSourcei(_source, AL_BUFFERS_QUEUED, &queued);
    if (queued == 0) {
        _playing = false;
        return;
    }
    // A stream plays on whatever the frame rate: one that ran out of queued
    // sound before this refill carries on from where it ran out, unless it
    // was stopped or is paused.
    if (!_playing || _paused) {
        return;
    }
    ALint state = 0;
    alGetSourcei(_source, AL_SOURCE_STATE, &state);
    if (state == AL_STOPPED) {
        alSourcePlay(_source);
    }
}

void AudioSource::play() {
    if (!_source) {
        return;
    }
    alSourcePlay(_source);
    _playing = true;
    _paused = false;
}

void AudioSource::pause() {
    alSourcePause(_source);
    _paused = true;
}

// Only a source that is still playing, and paused or created paused, resumes:
// one that was stopped, cancelled or ended stays so until the mixer drops it.
// A source cancelled before it ever started still reads as not started, so its
// own state decides.
void AudioSource::resume() {
    if (!_playing) {
        return;
    }
    _paused = false;
    ALint state = 0;
    alGetSourcei(_source, AL_SOURCE_STATE, &state);
    if (state == AL_PAUSED || state == AL_INITIAL) {
        alSourcePlay(_source);
    }
}

void AudioSource::startPaused() {
    _playing = true;
    _paused = true;
}

void AudioSource::stop() {
    _playing = false;
    if (!_source) {
        return;
    }
    alSourceStop(_source);
}

float AudioSource::duration() const {
    return _stream->duration();
}

void AudioSource::setPosition(glm::vec3 position) {
    if (_position == position) {
        return;
    }
    if (_source) {
        alSource3f(_source, AL_POSITION, position.x, position.y, position.z);
    }
    _position = std::move(position);
}

void AudioSource::setLooping(bool loop) {
    _loop = loop;
    if (_source && !_streaming) {
        alSourcei(_source, AL_LOOPING, _loop);
    }
}

void AudioSource::setGainScale(float scale) {
    alSourcef(_source, AL_GAIN, _gain * scale);
}

void AudioSource::setPitch(float pitch) {
    alSourcef(_source, AL_PITCH, pitch);
}

} // namespace audio

} // namespace reone
