/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "../fixtures/audio.h"

#include "reone/audio/clip.h"
#include "reone/audio/context.h"
#include "reone/audio/source.h"

#include "al.h"
#ifdef R_ENABLE_EFX
#define AL_ALEXT_PROTOTYPES
#include "efx-presets.h"
#include "efx.h"
#endif

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

#ifdef R_ENABLE_EFX
static float eaxReverbProperty(ALuint effect, ALenum param) {
    float value = 0.0f;
    alGetEffectf(effect, param, &value);
    return value;
}
#endif

TEST(AudioContext, loads_an_environment_s_preset_into_the_reverb) {
#ifndef R_ENABLE_EFX
    GTEST_SKIP() << "the OpenAL found has no effects extension";
#else
    Context context;
    if (!openTestContext(context)) {
        GTEST_SKIP() << "no OpenAL device opens";
    }
    ASSERT_EQ(context.reverbTier(), ReverbTier::EaxReverb);
    // OpenAL Soft names effects per device from 1, and this device holds only the reverb.
    const ALuint effect = 1;
    ASSERT_TRUE(alIsEffect(effect));
    const EFXEAXREVERBPROPERTIES generic = EFX_REVERB_PRESET_GENERIC;
    EXPECT_FLOAT_EQ(eaxReverbProperty(effect, AL_EAXREVERB_DECAY_TIME), generic.flDecayTime);

    context.setEnvironment(Environment::Underwater);

    const EFXEAXREVERBPROPERTIES underwater = EFX_REVERB_PRESET_UNDERWATER;
    EXPECT_FLOAT_EQ(eaxReverbProperty(effect, AL_EAXREVERB_GAINHF), underwater.flGainHF);
    EXPECT_FLOAT_EQ(eaxReverbProperty(effect, AL_EAXREVERB_MODULATION_DEPTH), underwater.flModulationDepth);
    EXPECT_EQ(alGetError(), AL_NO_ERROR);
#endif
}

TEST(AudioContext, attaches_and_detaches_a_source_s_reverb_send_and_obstruction) {
    Context context;
    if (!openTestContext(context)) {
        GTEST_SKIP() << "no OpenAL device opens";
    }
    auto clip = std::make_shared<AudioClip>();
    AudioClip::Frame frame;
    frame.format = AudioFormat::Mono8;
    frame.sampleRate = 22050;
    frame.samples.assign(64, 0);
    clip->add(std::move(frame));
    AudioSource source(clip, 1.0f, false, glm::vec3(0.0f));
    source.init();
    ASSERT_NE(source.alSource(), 0u);

    context.setReverbSend(source, 0.5f);
    EXPECT_EQ(alGetError(), AL_NO_ERROR);
    context.setReverbSend(source, 0.0f);
    EXPECT_EQ(alGetError(), AL_NO_ERROR);
#ifdef R_ENABLE_EFX
    EXPECT_TRUE(context.rendersObstruction());
#else
    EXPECT_FALSE(context.rendersObstruction());
#endif
    context.setObstruction(source, 0.4f);
    EXPECT_EQ(alGetError(), AL_NO_ERROR);
    context.setObstruction(source, 0.0f);
    EXPECT_EQ(alGetError(), AL_NO_ERROR);
}
