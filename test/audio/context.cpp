/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/audio.h"

#include "reone/audio/context.h"
#include "reone/system/threadutil.h"

using namespace reone;
using namespace reone::audio;

TEST(AudioContext, turns_the_listener_to_the_orientation_given) {
    useNullAudioBackend();
    markMainThread();
    Context context;
    context.init();

    context.setListenerOrientation(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    float orientation[6] {};
    alGetListenerfv(AL_ORIENTATION, orientation);
    EXPECT_FLOAT_EQ(orientation[1], 1.0f);
    EXPECT_FLOAT_EQ(orientation[2], 0.0f);
    EXPECT_FLOAT_EQ(orientation[5], 1.0f);
}
