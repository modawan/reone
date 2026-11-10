/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/audio.h"

#include "reone/audio/context.h"

#include "al.h"

using namespace reone;
using namespace reone::audio;

TEST(AudioContext, opens_the_fullest_reverb_the_device_renders_and_frees_it) {
#ifndef R_ENABLE_EFX
    GTEST_SKIP() << "the OpenAL found has no effects extension";
#endif
    Context context;
    if (!openTestContext(context)) {
        GTEST_SKIP() << "no OpenAL device opens";
    }
    EXPECT_EQ(context.reverbTier(), ReverbTier::EaxReverb);
    EXPECT_EQ(alGetError(), AL_NO_ERROR);

    context.deinit();
    EXPECT_EQ(context.reverbTier(), ReverbTier::None);
}

TEST(AudioContext, turns_the_listener_to_the_orientation_given) {
    Context context;
    if (!openTestContext(context)) {
        GTEST_SKIP() << "no OpenAL device opens";
    }

    context.setListenerOrientation(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    float orientation[6] {};
    alGetListenerfv(AL_ORIENTATION, orientation);
    EXPECT_FLOAT_EQ(orientation[1], 1.0f);
    EXPECT_FLOAT_EQ(orientation[2], 0.0f);
    EXPECT_FLOAT_EQ(orientation[5], 1.0f);
}
