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

#include "../decodedcache.h"
#include "../types.h"

namespace reone {

namespace audio {

class AudioClip;

}

namespace resource {

class IAudioClips {
public:
    virtual ~IAudioClips() = default;

    virtual void clear() = 0;

    virtual std::shared_ptr<audio::AudioClip> get(const std::string &key) = 0;

    /** Whether an MP3 named \p resRef is there, streamed or not; nothing is decoded. */
    virtual bool hasMp3(const std::string &resRef) = 0;
};

/**
 * Audio clips, read from the streamed audio directories first and from
 * ordinary resources second.
 *
 * The streaming directories are not part of the Odyssey raw lookup order, so
 * they are held separately and consulted explicitly rather than being given a
 * bucket they have no evidence for. Format preference is unchanged: MP3 is
 * still preferred over WAV across both, not within each.
 */
class AudioClips : public IAudioClips {
public:
    AudioClips(IResources &resources, IResources &streamResources) :
        _resources(resources),
        _streamResources(streamResources) {
    }

    void clear() override {
        _objects.clear();
    }

    bool hasMp3(const std::string &resRef) override {
        ResourceId id(resRef, ResType::Mp3);
        return _streamResources.locate(id) || _resources.locate(id);
    }

    std::shared_ptr<audio::AudioClip> get(const std::string &key) override {
        return entry(key).value;
    }

    /// The cached clip with the lookups it was read from, for a value built from it.
    const DecodedCache<std::string, audio::AudioClip>::Entry &entry(const std::string &key) {
        return _objects.get(key, [this, &key](ResourceReads &reads) {
            return doGet(key, reads);
        });
    }

private:
    IResources &_resources;
    IResources &_streamResources;

    DecodedCache<std::string, audio::AudioClip> _objects;

    std::shared_ptr<audio::AudioClip> doGet(const std::string &resRef, ResourceReads &reads);
    std::optional<Resource> findClipData(const std::string &resRef, ResType type, ResourceReads &reads);
};

} // namespace resource

} // namespace reone
