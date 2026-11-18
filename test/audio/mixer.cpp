/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/audio.h"

#include "reone/audio/clip.h"
#include "reone/audio/context.h"
#include "reone/audio/mixer.h"

#include "al.h"

using namespace reone;
using namespace reone::audio;

using testing::_;
using testing::NiceMock;
using testing::Return;

TEST(AudioMixer, missing_or_empty_clip_is_not_started) {
    AudioOptions options;
    MockContext context;
    AudioMixer mixer(options, context);

    EXPECT_FALSE(mixer.play(nullptr, AudioType::Sound, std::nullopt));
    EXPECT_FALSE(mixer.play(
        std::make_shared<AudioClip>(),
        AudioType::Sound,
        std::nullopt));
}

namespace {

// What a wet sound sends to the reverb.
float wetSend() {
    return std::pow(0.6f, 5.0f / 3.0f);
}

// Records the send gain the mixer gives each source. Group 1 streams and
// group 2 stays dry. A real context lets the sources play, while the mock
// takes the mixer's calls.
class ReverbSendTest : public testing::Test {
protected:
    ReverbSendTest() :
        mixer(options, context, [this]() { return now; }) {
    }

    void SetUp() override {
        if (!openTestContext(openAL)) {
            GTEST_SKIP() << "no OpenAL device opens";
        }
        ON_CALL(context, setReverbSend(_, _)).WillByDefault([this](AudioSource &, float gain) {
            sends.push_back(gain);
        });
        mixer.setStreamedGroups({1});
        mixer.setDryGroups({2});
    }

    float playPositional(std::optional<int> group) {
        sends.clear();
        mixer.play(silentClip(), AudioType::Sound, group, 1.0f, true, glm::vec3(1.0f, 0.0f, 0.0f));
        EXPECT_EQ(sends.size(), 1u);
        return sends.empty() ? -1.0f : sends.back();
    }

    Context openAL;
    AudioOptions options;
    NiceMock<MockContext> context;
    std::chrono::steady_clock::time_point now;
    AudioMixer mixer;
    std::vector<float> sends;
};

} // namespace

TEST_F(ReverbSendTest, unstreamed_positional_sound_is_wet_only_where_the_room_reverberates) {
    EXPECT_EQ(playPositional(std::nullopt), 0.0f);

    sends.clear();
    mixer.setRoomReverb(true);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_FLOAT_EQ(sends.back(), wetSend());

    EXPECT_FLOAT_EQ(playPositional(std::nullopt), wetSend());
}

TEST_F(ReverbSendTest, streamed_positional_sound_is_wet_whatever_the_room) {
    EXPECT_FLOAT_EQ(playPositional(1), wetSend());

    sends.clear();
    mixer.setRoomReverb(true);
    EXPECT_TRUE(sends.empty());
}

TEST_F(ReverbSendTest, two_d_and_dry_group_sounds_stay_dry) {
    mixer.setRoomReverb(true);

    EXPECT_EQ(playPositional(2), 0.0f);
    sends.clear();
    mixer.play(silentClip(), AudioType::Sound, std::nullopt, 1.0f, true);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_EQ(sends.back(), 0.0f);
}

TEST_F(ReverbSendTest, effects_options_turn_their_sends_off) {
    options.streamedReverb = false;
    options.unstreamedReverb = false;
    mixer.setRoomReverb(true);

    EXPECT_EQ(playPositional(1), 0.0f);
    EXPECT_EQ(playPositional(std::nullopt), 0.0f);
}

TEST_F(ReverbSendTest, only_positional_sounds_take_air_absorption) {
    EXPECT_CALL(context, setAirAbsorption(_, 1.0f)).Times(1);

    playPositional(std::nullopt);
    mixer.play(silentClip(), AudioType::Sound, std::nullopt, 1.0f, true);
}

TEST_F(ReverbSendTest, positional_sound_takes_its_first_obstruction_at_once_and_eases_to_later_ones) {
    std::vector<float> obstructions;
    ON_CALL(context, setObstruction(_, _)).WillByDefault([&obstructions](AudioSource &, float obstruction) {
        obstructions.push_back(obstruction);
    });
    ON_CALL(context, rendersObstruction()).WillByDefault(Return(true));
    float blocked = 0.4f;
    mixer.setObstructionTest([&blocked](const glm::vec3 &, const glm::vec3 &) { return blocked; });

    playPositional(std::nullopt);
    mixer.play(silentClip(), AudioType::Sound, std::nullopt, 1.0f, true);
    ASSERT_EQ(obstructions.size(), 1u);
    EXPECT_FLOAT_EQ(obstructions.back(), 0.4f);

    // Nothing moved, so the change only counts once the obstructions are
    // invalidated, as when a door between them shuts.
    blocked = 1.0f;
    mixer.invalidateObstruction();
    mixer.render();
    EXPECT_EQ(obstructions.size(), 1u);

    // Full obstruction takes half a second to come in.
    now += std::chrono::milliseconds(100);
    mixer.render();
    ASSERT_EQ(obstructions.size(), 2u);
    EXPECT_NEAR(obstructions.back(), 0.6f, 1e-4f);

    now += std::chrono::milliseconds(1000);
    mixer.render();
    ASSERT_EQ(obstructions.size(), 3u);
    EXPECT_FLOAT_EQ(obstructions.back(), 1.0f);

    now += std::chrono::milliseconds(100);
    mixer.render();
    EXPECT_EQ(obstructions.size(), 3u);
}

TEST_F(ReverbSendTest, obstruction_is_tested_again_only_once_something_has_changed) {
    ON_CALL(context, rendersObstruction()).WillByDefault(Return(true));
    glm::vec3 listener {0.0f};
    ON_CALL(context, listenerPosition()).WillByDefault([&listener]() { return listener; });
    int tests = 0;
    mixer.setObstructionTest([&tests](const glm::vec3 &, const glm::vec3 &) {
        ++tests;
        return 0.0f;
    });
    playPositional(std::nullopt);
    ASSERT_EQ(tests, 1);

    auto renderFor = [this](int renders) {
        for (int i = 0; i < renders; ++i) {
            now += std::chrono::milliseconds(100);
            mixer.render();
        }
    };
    renderFor(10);
    EXPECT_EQ(tests, 1);

    // A step shorter than the move distance changes nothing.
    listener.x = 0.1f;
    renderFor(1);
    EXPECT_EQ(tests, 1);

    listener.x = 0.5f;
    renderFor(1);
    EXPECT_EQ(tests, 2);

    mixer.invalidateObstruction();
    renderFor(1);
    EXPECT_EQ(tests, 3);
}

TEST_F(ReverbSendTest, obstruction_tests_a_second_do_not_grow_with_the_render_rate) {
    ON_CALL(context, rendersObstruction()).WillByDefault(Return(true));
    glm::vec3 listener {0.0f};
    ON_CALL(context, listenerPosition()).WillByDefault([&listener]() { return listener; });
    int tests = 0;
    mixer.setObstructionTest([&tests](const glm::vec3 &, const glm::vec3 &) {
        ++tests;
        return 0.0f;
    });
    playPositional(std::nullopt);
    // The first render starts the mixer's clock.
    mixer.render();

    // The listener keeps walking, so the source is stale at every render.
    auto testsInASecond = [&](int renders) {
        const int before = tests;
        for (int i = 0; i < renders; ++i) {
            now += std::chrono::microseconds(1000000 / renders);
            listener.x += 1.0f;
            mixer.render();
        }
        return tests - before;
    };
    const int atThirty = testsInASecond(30);
    const int atTwoForty = testsInASecond(240);
    EXPECT_NEAR(atThirty, 30, 1);
    EXPECT_NEAR(atTwoForty, atThirty, 1);
}

TEST_F(ReverbSendTest, an_invalidation_spreads_its_tests_over_the_renders_that_follow) {
    ON_CALL(context, rendersObstruction()).WillByDefault(Return(true));
    int tests = 0;
    mixer.setObstructionTest([&tests](const glm::vec3 &, const glm::vec3 &) {
        ++tests;
        return 0.0f;
    });
    for (int i = 0; i < 10; ++i) {
        playPositional(std::nullopt);
    }
    ASSERT_EQ(tests, 10);
    auto render = [this]() {
        now += std::chrono::microseconds(1000000 / 30);
        mixer.render();
    };
    // Standing still saves up no more than the burst.
    for (int i = 0; i < 60; ++i) {
        render();
    }
    ASSERT_EQ(tests, 10);

    mixer.invalidateObstruction();
    render();
    EXPECT_EQ(tests, 14);
    for (int i = 0; i < 10; ++i) {
        render();
    }
    EXPECT_EQ(tests, 20);
}

TEST(AudioMixer, ambient_scale_eases_the_ambient_groups_2d_and_streamed_sources_over_a_second) {
    Context openAL;
    if (!openTestContext(openAL)) {
        GTEST_SKIP() << "no OpenAL device opens";
    }
    AudioOptions options;
    NiceMock<MockContext> context;
    auto now = std::chrono::steady_clock::time_point();
    AudioMixer mixer(options, context, [&now]() { return now; });
    // Groups 4 and 21 are ambient here, and group 4 streams.
    mixer.setAmbientGroups({4, 21});
    mixer.setStreamedGroups({4});
    auto ambient = mixer.play(silentClip(), AudioType::Sound, 4, 1.0f, true);
    auto positionalStream = mixer.play(silentClip(), AudioType::Sound, 4, 1.0f, true, glm::vec3(1.0f, 0.0f, 0.0f));
    auto positionalShot = mixer.play(silentClip(), AudioType::Sound, 21, 1.0f, true, glm::vec3(1.0f, 0.0f, 0.0f));
    auto gainOf = [](const AudioSource &source) {
        float gain = 0.0f;
        alGetSourcef(source.alSource(), AL_GAIN, &gain);
        return gain;
    };
    const float full = gainOf(*ambient);
    const float shotFull = gainOf(*positionalShot);
    ASSERT_GT(full, 0.0f);
    ASSERT_GT(gainOf(*positionalStream), 0.0f);

    mixer.setAmbientScale(0.0f);
    now += std::chrono::milliseconds(500);
    mixer.render();
    EXPECT_NEAR(gainOf(*ambient), 0.5f * full, 1e-4f);

    now += std::chrono::milliseconds(600);
    mixer.render();
    EXPECT_NEAR(gainOf(*ambient), 0.0f, 1e-4f);
    EXPECT_NEAR(gainOf(*positionalStream), 0.0f, 1e-4f);
    // A positional sound that is not streamed is never scaled.
    EXPECT_FLOAT_EQ(gainOf(*positionalShot), shotFull);

    mixer.jumpAmbientScale(1.0f);
    EXPECT_FLOAT_EQ(gainOf(*ambient), full);
}

TEST(AudioMixer, streamed_positional_sound_falls_off_by_its_own_line_and_facing) {
    Context openAL;
    if (!openTestContext(openAL)) {
        GTEST_SKIP() << "no OpenAL device opens";
    }
    AudioOptions options;
    NiceMock<MockContext> context;
    ON_CALL(context, listenerForward()).WillByDefault(Return(glm::vec3(0.0f, 1.0f, 0.0f)));
    AudioMixer mixer(options, context);
    // Group 1 streams and group 2 does not; both fall off from 2 to 10 metres.
    PriorityGroup row;
    row.fullVolumeDistance = 2.0f;
    row.cutoffDistance = 10.0f;
    mixer.setPriorityGroups({row, row, row});
    mixer.setStreamedGroups({1});
    auto at = [&mixer](int group, const glm::vec3 &position) {
        return mixer.play(silentClip(), AudioType::Sound, group, 1.0f, true, position);
    };
    auto gainOf = [](const AudioSource &source) {
        float gain = 0.0f;
        alGetSourcef(source.alSource(), AL_GAIN, &gain);
        return gain;
    };
    std::vector<const AudioSource *> unattenuated;
    ON_CALL(context, stopDistanceAttenuation(_)).WillByDefault([&unattenuated](AudioSource &source) {
        unattenuated.push_back(&source);
    });
    auto attenuatedByOpenAL = [&unattenuated](const AudioSource &source) {
        return std::find(unattenuated.begin(), unattenuated.end(), &source) == unattenuated.end();
    };

    auto near = at(1, glm::vec3(0.0f, 1.0f, 0.0f));
    auto ahead = at(1, glm::vec3(0.0f, 6.0f, 0.0f));
    auto behind = at(1, glm::vec3(0.0f, -6.0f, 0.0f));
    auto unstreamed = at(2, glm::vec3(0.0f, 6.0f, 0.0f));
    const float full = gainOf(*near);

    EXPECT_NEAR(gainOf(*ahead), full * std::pow(0.5f, 0.6f), 1e-4f);
    EXPECT_NEAR(gainOf(*behind), full * std::pow(0.5f * 0.875f, 0.6f), 1e-4f);
    EXPECT_FALSE(attenuatedByOpenAL(*ahead));
    EXPECT_FLOAT_EQ(gainOf(*unstreamed), full);
    EXPECT_TRUE(attenuatedByOpenAL(*unstreamed));

    ahead->setPosition(glm::vec3(0.0f, 1.5f, 0.0f));
    mixer.render();
    EXPECT_FLOAT_EQ(gainOf(*ahead), full);
}
