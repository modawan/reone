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

#include <gtest/gtest.h>

#include "reone/game/effect/visual.h"
#include "reone/game/visualeffects.h"
#include "reone/graphics/model.h"
#include "reone/resource/types.h"
#include "reone/scene/node/emitter.h"

using namespace reone;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace {

struct GrenadeVisual {
    const char *label;
    const char *impactModel;
};

constexpr GrenadeVisual kKotorGrenadeVisuals[] {
    {"VFX_FNF_GRENADE_FRAGMENTATION", "v_grnfrag_fnf"},
    {"VFX_FNF_GRENADE_STUN", "v_grnstun_fnf"},
    {"VFX_FNF_GRENADE_THERMAL_DETONATOR", "v_grndeto_fnf"},
    {"VFX_FNF_GRENADE_POISON", "v_grnpois_fnf"},
    {"VFX_FNF_GRENADE_SONIC", "v_grnsonc_fnf"},
    {"VFX_FNF_GRENADE_ADHESIVE", "v_grnadhs_fnf"},
    {"VFX_FNF_GRENADE_CRYOBAN", "v_grncryo_fnf"},
    {"VFX_FNF_GRENADE_PLASMA", "v_grnplas_fnf"},
    {"VFX_FNF_GRENADE_ION", "v_grnion_fnf"},
};

VisualEffectDesc visualEffectDesc(const char *label, const char *impactModel) {
    VisualEffectDesc desc;
    desc.label = label;
    desc.impRootMNode = std::make_shared<Model>(
        impactModel,
        0,
        nullptr,
        std::vector<std::shared_ptr<Animation>>(),
        "",
        1.0f);
    return desc;
}

void expectNeutral(const ParticleRenderProfile &profile) {
    EXPECT_FLOAT_EQ(1.0f, profile.largeParticleScale);
    EXPECT_FLOAT_EQ(1.0f, profile.worldZScale);
    EXPECT_FLOAT_EQ(1.0f, profile.opacity);
    EXPECT_FLOAT_EQ(1.0f, profile.worldZOpacity);
    EXPECT_FLOAT_EQ(1.0f, profile.motionLengthScale);
    EXPECT_EQ(std::numeric_limits<float>::max(), profile.motionMaxWidth);
    EXPECT_FLOAT_EQ(1.0f, profile.motionOpacity);
    EXPECT_EQ(glm::vec3(1.0f), profile.colorTint);
    EXPECT_FLOAT_EQ(1.0f, profile.colorIntensity);
    EXPECT_EQ(ParticleReconstruction::Legacy, profile.policy.reconstruction);
    EXPECT_EQ(ParticleAlphaMode::Legacy, profile.policy.alpha);
    EXPECT_EQ(ParticleTrailMode::Legacy, profile.policy.trail);
    EXPECT_EQ(ParticleDiagnosticMode::Composite, profile.policy.diagnostic);
    EXPECT_FLOAT_EQ(0.0f, profile.policy.reconstructionStrength);
    EXPECT_FLOAT_EQ(1.0f, profile.policy.alphaExponent);
    EXPECT_FLOAT_EQ(0.0f, profile.policy.trailCoreIntensity);
    EXPECT_EQ(std::numeric_limits<float>::max(), profile.motionMaxLength);
}

} // namespace

TEST(VisualEffectParticleProfile, should_select_all_nine_kotor_grenade_profiles) {
    for (const auto &entry : kKotorGrenadeVisuals) {
        SCOPED_TRACE(entry.label);
        auto profile = particleRenderProfileForVisualEffect(
            GameID::KotOR,
            visualEffectDesc(entry.label, entry.impactModel));

        EXPECT_EQ(ParticleReconstruction::Cubic, profile.policy.reconstruction);
        EXPECT_EQ(ParticleAlphaMode::AlphaAndLuminance, profile.policy.alpha);
        EXPECT_EQ(ParticleTrailMode::AnalyticCore, profile.policy.trail);
        EXPECT_EQ(ParticleDiagnosticMode::Composite, profile.policy.diagnostic);
        EXPECT_GT(profile.policy.reconstructionStrength, 0.0f);
        EXPECT_LE(profile.policy.reconstructionStrength, 1.0f);
        EXPECT_GE(profile.policy.alphaExponent, 1.0f);
        EXPECT_LE(profile.policy.alphaExponent, 1.25f);
        EXPECT_GT(profile.policy.trailCoreIntensity, 0.0f);
        EXPECT_LE(profile.policy.trailCoreIntensity, 0.1f);
        EXPECT_GT(profile.motionMaxWidth, 0.0f);
        EXPECT_LE(profile.motionMaxWidth, 0.22f);
        EXPECT_GT(profile.motionMaxLength, 0.0f);
        EXPECT_LE(profile.motionMaxLength, 1.5f);

        EXPECT_FLOAT_EQ(1.0f, profile.opacity);
        EXPECT_EQ(glm::vec3(1.0f), profile.colorTint);
        EXPECT_FLOAT_EQ(1.0f, profile.colorIntensity);
    }
}

TEST(VisualEffectParticleProfile, should_keep_ion_plasma_and_thermal_trails_compact) {
    constexpr GrenadeVisual criticalVisuals[] {
        {"VFX_FNF_GRENADE_THERMAL_DETONATOR", "v_grndeto_fnf"},
        {"VFX_FNF_GRENADE_PLASMA", "v_grnplas_fnf"},
        {"VFX_FNF_GRENADE_ION", "v_grnion_fnf"},
    };

    for (const auto &entry : criticalVisuals) {
        SCOPED_TRACE(entry.label);
        auto profile = particleRenderProfileForVisualEffect(
            GameID::KotOR,
            visualEffectDesc(entry.label, entry.impactModel));

        EXPECT_LE(profile.motionLengthScale, 0.5f);
        EXPECT_LE(profile.motionMaxWidth, 0.12f);
        EXPECT_LE(profile.motionMaxLength, 0.8f);
    }
}

TEST(VisualEffectParticleProfile, should_return_neutral_for_unknown_labels) {
    expectNeutral(particleRenderProfileForVisualEffect(
        GameID::KotOR,
        visualEffectDesc("VFX_FNF_NOT_A_GRENADE", "v_grnfrag_fnf")));
}

TEST(VisualEffectParticleProfile, should_return_neutral_for_tsl_grenade_labels) {
    for (const auto &entry : kKotorGrenadeVisuals) {
        SCOPED_TRACE(entry.label);
        expectNeutral(particleRenderProfileForVisualEffect(
            GameID::TSL,
            visualEffectDesc(entry.label, entry.impactModel)));
    }
}

TEST(VisualEffectParticleProfile, should_return_neutral_for_mismatched_kotor_models) {
    expectNeutral(particleRenderProfileForVisualEffect(
        GameID::KotOR,
        visualEffectDesc("VFX_FNF_GRENADE_ION", "v_grnplas_fnf")));
}
