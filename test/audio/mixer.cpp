/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"

using namespace reone::audio;

TEST(AudioMixer, missing_or_empty_clip_is_not_started) {
    AudioOptions options;
    AudioMixer mixer(options);

    EXPECT_FALSE(mixer.play(nullptr, AudioType::Sound));
    EXPECT_FALSE(mixer.play(
        std::make_shared<AudioClip>(),
        AudioType::Sound));
}

TEST(AudioMixer, set_volume_sets_only_that_type_volume) {
    AudioOptions options;
    AudioMixer mixer(options);

    mixer.setVolume(AudioType::Music, 40);

    EXPECT_EQ(40, options.musicVolume);
    EXPECT_EQ(85, options.voiceVolume);
    EXPECT_EQ(85, options.soundVolume);
    EXPECT_EQ(85, options.movieVolume);

    mixer.setVolume(AudioType::Movie, 10);
    EXPECT_EQ(10, options.movieVolume);
    EXPECT_EQ(40, options.musicVolume);
}
