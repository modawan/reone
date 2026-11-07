/*
 * Copyright (c) 2026 The reone project contributors
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits>
#include <optional>

#include "../fixtures/engine.h"
#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/game.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"
#include "reone/system/threadutil.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

using testing::_;
using testing::AnyNumber;
using testing::FloatEq;

namespace {

std::shared_ptr<AudioClip> makeClip() {
    auto clip = std::make_shared<AudioClip>();
    AudioClip::Frame frame;
    frame.sampleRate = 100;
    frame.samples.resize(100);
    clip->add(std::move(frame));
    return clip;
}

float g_deviceGain = -1.0f;

void recordDeviceGain(uint32_t, float gain) {
    g_deviceGain = gain;
}

// Sources start only on the main thread, so the tests run as it; the device
// gain each source is given is recorded rather than sent to OpenAL.
class MusicVolumeScale : public testing::Test {
protected:
    void SetUp() override {
        _mainThread.emplace();
        setDeviceGainSetter(&recordDeviceGain);
        g_deviceGain = -1.0f;
        _options.musicVolume = 100;
        _options.soundVolume = 100;
        _mixer = std::make_unique<AudioMixer>(_options);
    }

    void TearDown() override {
        _mixer.reset();
        setDeviceGainSetter(nullptr);
        _mainThread.reset();
    }

    std::optional<ScopedMainThread> _mainThread;
    AudioOptions _options;
    std::unique_ptr<AudioMixer> _mixer;
};

class MusicVolumeRoutine : public testing::Test {
protected:
    void SetUp() override {
        _engine.init();
        // Retiring the session clears the scene.
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(testing::ReturnRef(_sceneGraph));
        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
    }

    void setMusicVolume(std::vector<Variable> args) {
        Routine &routine = _routines->get(_routines->getIndexByName("SetMusicVolume"));
        ExecutionContext execution;
        execution.routines = _routines.get();
        routine.invoke(args, execution);
    }

    TestEngine _engine;
    testing::NiceMock<scene::MockSceneGraph> _sceneGraph;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
};

} // namespace

// The scale applies to music playing and music started later, and to nothing else.
TEST_F(MusicVolumeScale, scale_of_one_leaves_music_as_the_options_set_it) {
    auto &mixer = *_mixer;
    auto music = mixer.play(makeClip(), AudioType::Music, 0.8f);
    ASSERT_TRUE(music);

    mixer.setMusicVolumeScale(1.0f);
    EXPECT_FLOAT_EQ(0.8f, music->gain());
    EXPECT_FLOAT_EQ(0.8f, g_deviceGain);
}

TEST_F(MusicVolumeScale, scale_halves_playing_and_later_music_but_not_sounds) {
    auto &mixer = *_mixer;
    auto playing = mixer.play(makeClip(), AudioType::Music, 0.8f);
    auto sound = mixer.play(makeClip(), AudioType::Sound, 0.6f);
    ASSERT_TRUE(playing);
    ASSERT_TRUE(sound);

    mixer.setMusicVolumeScale(0.5f);
    auto later = mixer.play(makeClip(), AudioType::Music, 0.6f);
    ASSERT_TRUE(later);

    EXPECT_FLOAT_EQ(0.4f, playing->gain());
    EXPECT_FLOAT_EQ(0.3f, later->gain());
    EXPECT_FLOAT_EQ(0.6f, sound->gain());
}

TEST_F(MusicVolumeScale, scale_of_zero_silences_music_until_restored) {
    auto &mixer = *_mixer;
    auto music = mixer.play(makeClip(), AudioType::Music, 0.8f);
    ASSERT_TRUE(music);

    mixer.setMusicVolumeScale(0.0f);
    EXPECT_FLOAT_EQ(0.0f, music->gain());
    mixer.setMusicVolumeScale(1.0f);
    EXPECT_FLOAT_EQ(0.8f, music->gain());
}

// A fade and the volume scale combine rather than replace each other.
TEST_F(MusicVolumeScale, fade_and_scale_combine) {
    auto &mixer = *_mixer;
    auto music = mixer.play(makeClip(), AudioType::Music, 0.8f);
    ASSERT_TRUE(music);

    mixer.setMusicVolumeScale(0.5f);
    music->setGainScale(0.5f);
    EXPECT_FLOAT_EQ(0.2f, music->gain());
}

// 765: the volume passed is a scale on music from 0 to 1.
TEST_F(MusicVolumeRoutine, volume_scales_music) {
    auto &mixer = _engine.audioModule().mixer();
    EXPECT_CALL(mixer, setMusicVolumeScale(_)).Times(AnyNumber());
    EXPECT_CALL(mixer, setMusicVolumeScale(FloatEq(1.0f))).Times(2);
    EXPECT_CALL(mixer, setMusicVolumeScale(FloatEq(0.5f))).Times(1);

    setMusicVolume({Variable::ofFloat(1.0f)});
    setMusicVolume({Variable::ofFloat(0.5f)});
    setMusicVolume({});
}

TEST_F(MusicVolumeRoutine, volume_outside_zero_to_one_is_clamped) {
    auto &mixer = _engine.audioModule().mixer();
    EXPECT_CALL(mixer, setMusicVolumeScale(_)).Times(AnyNumber());
    EXPECT_CALL(mixer, setMusicVolumeScale(FloatEq(0.0f))).Times(2);
    EXPECT_CALL(mixer, setMusicVolumeScale(FloatEq(1.0f))).Times(1);

    setMusicVolume({Variable::ofFloat(-1.0f)});
    setMusicVolume({Variable::ofFloat(std::numeric_limits<float>::quiet_NaN())});
    setMusicVolume({Variable::ofFloat(3.0f)});
}

// What the device plays each source at: a new source is given its gain as it
// is made ready, a playing one as the scale changes.
TEST_F(MusicVolumeScale, device_plays_each_source_at_its_scaled_gain) {
    auto &mixer = *_mixer;
    auto sound = mixer.play(makeClip(), AudioType::Sound, 0.6f);
    ASSERT_TRUE(sound);
    EXPECT_FLOAT_EQ(0.6f, g_deviceGain);

    auto music = mixer.play(makeClip(), AudioType::Music, 0.8f);
    ASSERT_TRUE(music);
    mixer.setMusicVolumeScale(0.5f);
    EXPECT_FLOAT_EQ(0.4f, g_deviceGain);

    mixer.play(makeClip(), AudioType::Music, 0.6f);
    EXPECT_FLOAT_EQ(0.3f, g_deviceGain);
}

// A source not yet made ready has no device source to give a gain to.
TEST_F(MusicVolumeScale, source_not_ready_gives_the_device_nothing) {
    AudioSource source(makeClip(), 0.8f);
    source.setVolumeScale(0.5f);
    source.setGainScale(0.5f);

    EXPECT_FLOAT_EQ(-1.0f, g_deviceGain);
    EXPECT_FLOAT_EQ(0.2f, source.gain());
}

TEST(ScopedMainThread, puts_back_the_main_thread_and_the_name) {
    const bool wasMain = isMainThread();
    const std::string name = threadName();
    {
        ScopedMainThread scope;
        EXPECT_TRUE(isMainThread());
        EXPECT_EQ("main", threadName());
    }
    EXPECT_EQ(wasMain, isMainThread());
    EXPECT_EQ(name, threadName());
}

// A new or loaded game plays music as the options set it.
TEST_F(MusicVolumeRoutine, retired_session_restores_music_volume) {
    auto &mixer = _engine.audioModule().mixer();
    EXPECT_CALL(mixer, setMusicVolumeScale(_)).Times(AnyNumber());
    EXPECT_CALL(mixer, setMusicVolumeScale(FloatEq(1.0f))).Times(1);

    _game->retireRuntimeSession();
}
