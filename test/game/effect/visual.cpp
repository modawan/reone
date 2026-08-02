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
    uint32_t id;
    const char *label;
    const char *impactModel;
};

constexpr GrenadeVisual kKotorGrenadeVisuals[] {
    {VisualEffectIds::grenadeFragmentation, "VFX_FNF_GRENADE_FRAGMENTATION", "v_grnfrag_fnf"},
    {VisualEffectIds::grenadeStun, "VFX_FNF_GRENADE_STUN", "v_grnstun_fnf"},
    {VisualEffectIds::thermalDetonator, "VFX_FNF_GRENADE_THERMAL_DETONATOR", "v_grndeto_fnf"},
    {VisualEffectIds::grenadePoison, "VFX_FNF_GRENADE_POISON", "v_grnpois_fnf"},
    {VisualEffectIds::grenadeSonic, "VFX_FNF_GRENADE_SONIC", "v_grnsonc_fnf"},
    {VisualEffectIds::grenadeAdhesive, "VFX_FNF_GRENADE_ADHESIVE", "v_grnadhs_fnf"},
    {VisualEffectIds::grenadeCryoban, "VFX_FNF_GRENADE_CRYOBAN", "v_grncryo_fnf"},
    {VisualEffectIds::grenadePlasma, "VFX_FNF_GRENADE_PLASMA", "v_grnplas_fnf"},
    {VisualEffectIds::grenadeIon, "VFX_FNF_GRENADE_ION", "v_grnion_fnf"},
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

ParticleRenderProfile selectedProfile(
    GameID gameId,
    const GrenadeVisual &visual) {

    ParticleRenderProfile profile;
    EXPECT_TRUE(particleRenderProfileForVisualEffect(
        gameId,
        visual.id,
        visualEffectDesc(visual.label, visual.impactModel),
        profile));
    return profile;
}

void expectUnselected(
    GameID gameId,
    uint32_t visualEffectId,
    VisualEffectDesc desc) {

    ParticleRenderProfile profile;
    profile.opacity = 0.123f;
    EXPECT_FALSE(particleRenderProfileForVisualEffect(
        gameId,
        visualEffectId,
        desc,
        profile));
    EXPECT_FLOAT_EQ(0.123f, profile.opacity);
}

} // namespace

TEST(VisualEffectParticleProfile, should_select_all_nine_kotor_grenade_profiles) {
    for (const auto &entry : kKotorGrenadeVisuals) {
        SCOPED_TRACE(entry.label);
        auto profile = selectedProfile(GameID::KotOR, entry);

        EXPECT_EQ(ParticleReconstruction::Cubic, profile.policy.reconstruction);
        EXPECT_EQ(ParticleAlphaMode::AlphaAndLuminance, profile.policy.alpha);
        EXPECT_EQ(ParticleTrailMode::AnalyticCore, profile.policy.trail);
        EXPECT_EQ(ParticleDiagnosticMode::Composite, profile.policy.diagnostic);
        EXPECT_GT(profile.policy.reconstructionStrength, 0.0f);
        EXPECT_LE(profile.policy.reconstructionStrength, 1.0f);
        EXPECT_GE(profile.policy.alphaExponent, 1.15f);
        EXPECT_LE(profile.policy.alphaExponent, 2.0f);
        EXPECT_GT(profile.policy.trailCoreIntensity, 0.0f);
        EXPECT_LE(profile.policy.trailCoreIntensity, 0.4f);
        EXPECT_GE(profile.policy.coverageContrast, 0.25f);
        EXPECT_LE(profile.policy.coverageContrast, 0.9f);
        EXPECT_GT(profile.motionMaxWidth, 0.0f);
        EXPECT_LE(profile.motionMaxWidth, 0.22f);
        EXPECT_GT(profile.motionMaxLength, 0.0f);
        EXPECT_LE(profile.motionMaxLength, 1.5f);

        EXPECT_GE(profile.opacity, 0.3f);
        EXPECT_LE(profile.opacity, 0.85f);
        EXPECT_GE(profile.worldZOpacity, profile.opacity);
        EXPECT_LE(profile.worldZOpacity, 0.9f);
        EXPECT_GE(profile.motionOpacity, 0.2f);
        EXPECT_LE(profile.motionOpacity, 0.6f);
        EXPECT_GT(profile.colorTint.r, 0.0f);
        EXPECT_GT(profile.colorTint.g, 0.0f);
        EXPECT_GT(profile.colorTint.b, 0.0f);
        EXPECT_LE(profile.colorTint.r, 1.0f);
        EXPECT_LE(profile.colorTint.g, 1.0f);
        EXPECT_LE(profile.colorTint.b, 1.0f);
        EXPECT_GE(profile.colorIntensity, 1.0f);
        EXPECT_LE(profile.colorIntensity, 1.05f);
    }
}

TEST(VisualEffectParticleProfile, should_keep_broad_area_grenades_localized) {
    constexpr size_t broadAreaIndices[] = {0, 4, 5, 6};

    for (size_t index : broadAreaIndices) {
        const auto &entry = kKotorGrenadeVisuals[index];
        SCOPED_TRACE(entry.label);
        auto profile = selectedProfile(GameID::KotOR, entry);

        EXPECT_LE(profile.largeParticleScale, 0.60f);
        EXPECT_LE(profile.worldZScale, 0.60f);
        EXPECT_LE(profile.opacity, 0.55f);
        EXPECT_LE(profile.worldZOpacity, 0.60f);
        EXPECT_GE(profile.policy.alphaExponent, 1.50f);
        EXPECT_LE(profile.policy.coverageContrast, 0.50f);
    }
}

TEST(VisualEffectParticleProfile, should_keep_ion_plasma_and_thermal_trails_compact) {
    constexpr GrenadeVisual criticalVisuals[] {
        {VisualEffectIds::thermalDetonator, "VFX_FNF_GRENADE_THERMAL_DETONATOR", "v_grndeto_fnf"},
        {VisualEffectIds::grenadePlasma, "VFX_FNF_GRENADE_PLASMA", "v_grnplas_fnf"},
        {VisualEffectIds::grenadeIon, "VFX_FNF_GRENADE_ION", "v_grnion_fnf"},
    };

    for (const auto &entry : criticalVisuals) {
        SCOPED_TRACE(entry.label);
        auto profile = selectedProfile(GameID::KotOR, entry);

        EXPECT_LE(profile.motionLengthScale, 0.5f);
        EXPECT_LE(profile.motionMaxWidth, 0.12f);
        EXPECT_LE(profile.motionMaxLength, 0.8f);
    }
}

TEST(VisualEffectParticleProfile, should_give_critical_grenades_distinct_energy_tints) {
    auto thermal = selectedProfile(GameID::KotOR, kKotorGrenadeVisuals[2]);
    auto plasma = selectedProfile(GameID::KotOR, kKotorGrenadeVisuals[7]);
    auto ion = selectedProfile(GameID::KotOR, kKotorGrenadeVisuals[8]);

    EXPECT_GT(thermal.colorTint.r, thermal.colorTint.b);
    EXPECT_GT(plasma.colorTint.r, plasma.colorTint.g);
    EXPECT_GT(ion.colorTint.b, ion.colorTint.r);
    EXPECT_GT(ion.policy.trailCoreIntensity, thermal.policy.trailCoreIntensity);
}

TEST(VisualEffectParticleProfile, should_not_select_unknown_labels) {
    expectUnselected(
        GameID::KotOR,
        VisualEffectIds::grenadeFragmentation,
        visualEffectDesc("VFX_FNF_NOT_A_GRENADE", "v_grnfrag_fnf"));
}

TEST(VisualEffectParticleProfile, should_not_select_unknown_ids) {
    expectUnselected(
        GameID::KotOR,
        42,
        visualEffectDesc("VFX_FNF_GRENADE_FRAGMENTATION", "v_grnfrag_fnf"));
}

TEST(VisualEffectParticleProfile, should_not_select_tsl_grenade_labels) {
    for (const auto &entry : kKotorGrenadeVisuals) {
        SCOPED_TRACE(entry.label);
        expectUnselected(
            GameID::TSL,
            entry.id,
            visualEffectDesc(entry.label, entry.impactModel));
    }
}

TEST(VisualEffectParticleProfile, should_not_select_mismatched_kotor_models) {
    expectUnselected(
        GameID::KotOR,
        VisualEffectIds::grenadeIon,
        visualEffectDesc("VFX_FNF_GRENADE_ION", "v_grnplas_fnf"));
}
