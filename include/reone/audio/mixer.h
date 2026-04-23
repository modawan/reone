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

#include "options.h"
#include "source.h"
#include "types.h"

namespace reone {

namespace audio {

class AudioClip;

class IAudioMixer {
public:
    virtual ~IAudioMixer() = default;

    virtual void render() = 0;
    virtual void stop(AudioType type) = 0;
    virtual void stopAll() = 0;

    virtual std::shared_ptr<AudioSource> play(
        std::shared_ptr<AudioClip> clip,
        AudioType type,
        float gain = 1.0f,
        bool loop = false,
        std::optional<glm::vec3> position = std::nullopt) = 0;
};

class AudioMixer : public IAudioMixer, boost::noncopyable {
public:
    AudioMixer(AudioOptions &options) :
        _options(options) {
    }

    void render() override;
    void stop(AudioType type) override;
    void stopAll() override;

    std::shared_ptr<AudioSource> play(
        std::shared_ptr<AudioClip> clip,
        AudioType type,
        float gain = 1.0f,
        bool loop = false,
        std::optional<glm::vec3> = std::nullopt) override;

private:
    struct ActiveSource {
        std::shared_ptr<AudioSource> source;
        AudioType type {AudioType::Sound};
    };

    AudioOptions &_options;

    std::vector<ActiveSource> _sources;

    float gainByType(AudioType type, float gain) const;
};

} // namespace audio

} // namespace reone
