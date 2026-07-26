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

#include "reone/game/effect/visual.h"

#include "reone/game/visualeffects.h"
#include "reone/graphics/model.h"
#include "reone/scene/node/emitter.h"

namespace reone {

namespace game {

static scene::ParticleRenderProfile baseGrenadeProfile() {
    scene::ParticleRenderProfile profile;
    profile.policy.reconstruction = scene::ParticleReconstruction::Cubic;
    profile.policy.alpha = scene::ParticleAlphaMode::AlphaAndLuminance;
    profile.policy.trail = scene::ParticleTrailMode::AnalyticCore;
    profile.policy.coverageContrast = 0.55f;
    return profile;
}

static scene::ParticleRenderProfile fragmentationGrenadeProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.82f;
    profile.worldZScale = 0.78f;
    profile.opacity = 0.80f;
    profile.worldZOpacity = 0.85f;
    profile.motionOpacity = 0.55f;
    profile.motionLengthScale = 0.70f;
    profile.motionMaxWidth = 0.16f;
    profile.motionMaxLength = 1.10f;
    profile.policy.reconstructionStrength = 0.70f;
    profile.policy.alphaExponent = 1.20f;
    profile.policy.trailCoreIntensity = 0.04f;
    return profile;
}

static scene::ParticleRenderProfile stunGrenadeProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.78f;
    profile.worldZScale = 0.76f;
    profile.opacity = 0.72f;
    profile.worldZOpacity = 0.75f;
    profile.motionOpacity = 0.48f;
    profile.motionLengthScale = 0.60f;
    profile.motionMaxWidth = 0.14f;
    profile.motionMaxLength = 0.95f;
    profile.policy.reconstructionStrength = 0.74f;
    profile.policy.alphaExponent = 1.30f;
    profile.policy.trailCoreIntensity = 0.05f;
    return profile;
}

static scene::ParticleRenderProfile thermalDetonatorProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.70f;
    profile.worldZScale = 0.67f;
    profile.opacity = 0.72f;
    profile.worldZOpacity = 0.75f;
    profile.motionOpacity = 0.45f;
    profile.motionLengthScale = 0.34f;
    profile.motionMaxWidth = 0.075f;
    profile.motionMaxLength = 0.52f;
    profile.colorTint = glm::vec3(1.0f, 0.68f, 0.28f);
    profile.colorIntensity = 1.05f;
    profile.policy.reconstructionStrength = 0.84f;
    profile.policy.alphaExponent = 1.30f;
    profile.policy.trailCoreIntensity = 0.28f;
    profile.policy.coverageContrast = 0.78f;
    return profile;
}

static scene::ParticleRenderProfile poisonGrenadeProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.86f;
    profile.worldZScale = 0.82f;
    profile.opacity = 0.78f;
    profile.worldZOpacity = 0.82f;
    profile.motionOpacity = 0.55f;
    profile.motionLengthScale = 0.72f;
    profile.motionMaxWidth = 0.18f;
    profile.motionMaxLength = 1.20f;
    profile.policy.reconstructionStrength = 0.68f;
    profile.policy.alphaExponent = 1.20f;
    profile.policy.trailCoreIntensity = 0.03f;
    return profile;
}

static scene::ParticleRenderProfile sonicGrenadeProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.80f;
    profile.worldZScale = 0.76f;
    profile.opacity = 0.70f;
    profile.worldZOpacity = 0.74f;
    profile.motionOpacity = 0.45f;
    profile.motionLengthScale = 0.58f;
    profile.motionMaxWidth = 0.14f;
    profile.motionMaxLength = 0.92f;
    profile.policy.reconstructionStrength = 0.72f;
    profile.policy.alphaExponent = 1.35f;
    profile.policy.trailCoreIntensity = 0.04f;
    return profile;
}

static scene::ParticleRenderProfile adhesiveGrenadeProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.86f;
    profile.worldZScale = 0.82f;
    profile.opacity = 0.78f;
    profile.worldZOpacity = 0.82f;
    profile.motionOpacity = 0.52f;
    profile.motionLengthScale = 0.68f;
    profile.motionMaxWidth = 0.16f;
    profile.motionMaxLength = 1.08f;
    profile.policy.reconstructionStrength = 0.68f;
    profile.policy.alphaExponent = 1.18f;
    profile.policy.trailCoreIntensity = 0.03f;
    return profile;
}

static scene::ParticleRenderProfile cryobanGrenadeProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.76f;
    profile.worldZScale = 0.72f;
    profile.opacity = 0.68f;
    profile.worldZOpacity = 0.72f;
    profile.motionOpacity = 0.42f;
    profile.motionLengthScale = 0.55f;
    profile.motionMaxWidth = 0.13f;
    profile.motionMaxLength = 0.85f;
    profile.policy.reconstructionStrength = 0.74f;
    profile.policy.alphaExponent = 1.40f;
    profile.policy.trailCoreIntensity = 0.04f;
    return profile;
}

static scene::ParticleRenderProfile plasmaGrenadeProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.70f;
    profile.worldZScale = 0.68f;
    profile.opacity = 0.70f;
    profile.worldZOpacity = 0.73f;
    profile.motionOpacity = 0.42f;
    profile.motionLengthScale = 0.32f;
    profile.motionMaxWidth = 0.07f;
    profile.motionMaxLength = 0.48f;
    profile.colorTint = glm::vec3(1.0f, 0.46f, 0.22f);
    profile.colorIntensity = 1.05f;
    profile.policy.reconstructionStrength = 0.86f;
    profile.policy.alphaExponent = 1.35f;
    profile.policy.trailCoreIntensity = 0.32f;
    profile.policy.coverageContrast = 0.80f;
    return profile;
}

static scene::ParticleRenderProfile ionGrenadeProfile() {
    auto profile = baseGrenadeProfile();
    profile.largeParticleScale = 0.56f;
    profile.worldZScale = 0.65f;
    profile.opacity = 0.50f;
    profile.worldZOpacity = 0.80f;
    profile.motionOpacity = 0.50f;
    profile.motionLengthScale = 0.30f;
    profile.motionMaxWidth = 0.06f;
    profile.motionMaxLength = 0.42f;
    profile.colorTint = glm::vec3(0.58f, 0.76f, 1.0f);
    profile.colorIntensity = 1.05f;
    profile.policy.reconstructionStrength = 0.88f;
    profile.policy.alphaExponent = 1.70f;
    profile.policy.trailCoreIntensity = 0.34f;
    profile.policy.coverageContrast = 0.82f;
    return profile;
}

static bool matchesEffect(
    const VisualEffectDesc &desc,
    const char *label,
    const char *impactModel) {

    return desc.label == label &&
           desc.impRootMNode &&
           desc.impRootMNode->name() == impactModel;
}

bool particleRenderProfileForVisualEffect(
    resource::GameID gameId,
    uint32_t visualEffectId,
    const VisualEffectDesc &desc,
    scene::ParticleRenderProfile &profile) {

    if (gameId != resource::GameID::KotOR) {
        return false;
    }

    switch (visualEffectId) {
    case VisualEffectIds::grenadeFragmentation:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_FRAGMENTATION", "v_grnfrag_fnf")) {
            profile = fragmentationGrenadeProfile();
            return true;
        }
        break;
    case VisualEffectIds::grenadeStun:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_STUN", "v_grnstun_fnf")) {
            profile = stunGrenadeProfile();
            return true;
        }
        break;
    case VisualEffectIds::thermalDetonator:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_THERMAL_DETONATOR", "v_grndeto_fnf")) {
            profile = thermalDetonatorProfile();
            return true;
        }
        break;
    case VisualEffectIds::grenadePoison:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_POISON", "v_grnpois_fnf")) {
            profile = poisonGrenadeProfile();
            return true;
        }
        break;
    case VisualEffectIds::grenadeSonic:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_SONIC", "v_grnsonc_fnf")) {
            profile = sonicGrenadeProfile();
            return true;
        }
        break;
    case VisualEffectIds::grenadeAdhesive:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_ADHESIVE", "v_grnadhs_fnf")) {
            profile = adhesiveGrenadeProfile();
            return true;
        }
        break;
    case VisualEffectIds::grenadeCryoban:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_CRYOBAN", "v_grncryo_fnf")) {
            profile = cryobanGrenadeProfile();
            return true;
        }
        break;
    case VisualEffectIds::grenadePlasma:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_PLASMA", "v_grnplas_fnf")) {
            profile = plasmaGrenadeProfile();
            return true;
        }
        break;
    case VisualEffectIds::grenadeIon:
        if (matchesEffect(desc, "VFX_FNF_GRENADE_ION", "v_grnion_fnf")) {
            profile = ionGrenadeProfile();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

} // namespace game

} // namespace reone
