/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/audio.h"

#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"
#include "reone/system/threadutil.h"

using namespace reone;
using namespace reone::audio;

using testing::_;
using testing::NiceMock;

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

std::shared_ptr<AudioClip> silentClip() {
    auto clip = std::make_shared<AudioClip>();
    AudioClip::Frame frame;
    frame.format = AudioFormat::Mono8;
    frame.sampleRate = 22050;
    frame.samples.assign(64, 0);
    clip->add(std::move(frame));
    return clip;
}

// Records the send gain the mixer gives each source. Group 1 streams and
// group 2 stays dry.
class ReverbSendTest : public testing::Test {
protected:
    ReverbSendTest() :
        mixer(options, context) {
        markMainThread();
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

    AudioOptions options;
    NiceMock<MockContext> context;
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
