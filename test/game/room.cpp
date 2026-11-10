/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/game/room.h"

using namespace reone;
using namespace reone::game;

TEST(RoomEnvironment, env_audio_names_the_eax_room_types_in_order) {
    EXPECT_EQ(environmentOfEnvAudio(0), audio::Environment::Generic);
    EXPECT_EQ(environmentOfEnvAudio(2), audio::Environment::Room);
    EXPECT_EQ(environmentOfEnvAudio(22), audio::Environment::Underwater);
}

TEST(RoomEnvironment, env_audio_23_repeats_generic) {
    EXPECT_EQ(environmentOfEnvAudio(23), audio::Environment::Generic);
}

TEST(RoomEnvironment, env_audio_past_23_or_below_0_names_none) {
    // 40 and 92 are among the values K1's areas use.
    EXPECT_EQ(environmentOfEnvAudio(24), std::nullopt);
    EXPECT_EQ(environmentOfEnvAudio(40), std::nullopt);
    EXPECT_EQ(environmentOfEnvAudio(92), std::nullopt);
    EXPECT_EQ(environmentOfEnvAudio(-1), std::nullopt);
}
