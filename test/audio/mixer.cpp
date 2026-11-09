/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/audio.h"

#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"

using namespace reone::audio;

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
