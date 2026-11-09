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

#include <gmock/gmock.h>

#include "reone/audio/context.h"
#include "reone/audio/di/services.h"
#include "reone/audio/format/mp3reader.h"
#include "reone/audio/mixer.h"

namespace reone {

namespace audio {

class MockMp3Reader : public Mp3Reader {
public:
    MockMp3Reader();
    ~MockMp3Reader();

    MOCK_METHOD(void, load, (IInputStream & stream), (override));
};

class MockMp3ReaderFactory : public IMp3ReaderFactory, boost::noncopyable {
public:
    MockMp3ReaderFactory();
    ~MockMp3ReaderFactory();

    MOCK_METHOD(std::shared_ptr<Mp3Reader>, create, (), (override));
};

class MockContext : public IContext, boost::noncopyable {
public:
    MockContext();
    ~MockContext() override;

    MOCK_METHOD(glm::vec3, listenerPosition, (), (const, override));
    MOCK_METHOD(void, setListenerPosition, (glm::vec3), (override));
    MOCK_METHOD(void, setListenerGain, (float), (override));
};

class MockAudioMixer : public IAudioMixer, boost::noncopyable {
public:
    MockAudioMixer();
    ~MockAudioMixer() override;

    MOCK_METHOD(void, render, (), (override));
    MOCK_METHOD(void, stop, (AudioType), (override));
    MOCK_METHOD(void, stopAll, (), (override));
    MOCK_METHOD(void, setSoundsHeldForMovie, (bool, const std::shared_ptr<AudioSource> &), (override));
    MOCK_METHOD(void, pauseSources, (const std::set<int> &), (override));
    MOCK_METHOD(void, resumeSources, (), (override));
    MOCK_METHOD(void, setPriorityGroups, (std::vector<PriorityGroup>), (override));
    MOCK_METHOD(void, setStreamedGroups, (std::set<int>), (override));
    MOCK_METHOD(PriorityGroup, priorityGroup, (int), (const, override));
    MOCK_METHOD(void, setFocusLost, (bool), (override));
    MOCK_METHOD(void, setMuted, (bool), (override));
    MOCK_METHOD(void, waitForFade, (), (override));
    MOCK_METHOD(void, waitForSourceFade, (const std::shared_ptr<AudioSource> &, uint32_t, uint32_t), (override));
    MOCK_METHOD(void, setVolume, (AudioType, int), (override));
    MOCK_METHOD(void, setMusicVolume, (float), (override));
    MOCK_METHOD(void, setDucked, (bool, const std::set<int> &), (override));
    MOCK_METHOD(std::shared_ptr<AudioSource>, play, (std::shared_ptr<AudioClip>, AudioType, std::optional<int>, float, bool, std::optional<glm::vec3>, std::optional<SoundDistances>, std::optional<float>), (override));
};

class TestAudioModule : boost::noncopyable {
public:
    void init() {
        _context = std::make_unique<MockContext>();
        _mixer = std::make_unique<MockAudioMixer>();

        _services = std::make_unique<AudioServices>(*_context, *_mixer);
    }

    AudioServices &services() {
        return *_services;
    }

    MockAudioMixer &mixer() {
        return *_mixer;
    }

private:
    std::unique_ptr<MockContext> _context;
    std::unique_ptr<MockAudioMixer> _mixer;

    std::unique_ptr<AudioServices> _services;
};

} // namespace audio

} // namespace reone
