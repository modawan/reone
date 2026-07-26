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

static scene::ParticleRenderProfile grenadeProfile(
    float largeParticleScale,
    float worldZScale,
    float motionLengthScale,
    float motionMaxWidth,
    float motionMaxLength,
    float reconstructionStrength,
    float alphaExponent,
    float trailCoreIntensity) {

    scene::ParticleRenderProfile profile;
    profile.largeParticleScale = largeParticleScale;
    profile.worldZScale = worldZScale;
    profile.motionLengthScale = motionLengthScale;
    profile.motionMaxWidth = motionMaxWidth;
    profile.motionMaxLength = motionMaxLength;
    profile.policy.reconstruction = scene::ParticleReconstruction::Cubic;
    profile.policy.alpha = scene::ParticleAlphaMode::AlphaAndLuminance;
    profile.policy.trail = scene::ParticleTrailMode::AnalyticCore;
    profile.policy.reconstructionStrength = reconstructionStrength;
    profile.policy.alphaExponent = alphaExponent;
    profile.policy.trailCoreIntensity = trailCoreIntensity;
    return profile;
}

static scene::ParticleRenderProfile fragmentationGrenadeProfile() {
    return grenadeProfile(0.85f, 0.80f, 0.75f, 0.18f, 1.20f, 0.68f, 1.08f, 0.04f);
}

static scene::ParticleRenderProfile stunGrenadeProfile() {
    return grenadeProfile(0.85f, 0.82f, 0.70f, 0.16f, 1.10f, 0.72f, 1.08f, 0.05f);
}

static scene::ParticleRenderProfile thermalDetonatorProfile() {
    return grenadeProfile(0.72f, 0.68f, 0.45f, 0.10f, 0.75f, 0.80f, 1.15f, 0.07f);
}

static scene::ParticleRenderProfile poisonGrenadeProfile() {
    return grenadeProfile(0.90f, 0.85f, 0.80f, 0.20f, 1.35f, 0.65f, 1.05f, 0.03f);
}

static scene::ParticleRenderProfile sonicGrenadeProfile() {
    return grenadeProfile(0.88f, 0.82f, 0.70f, 0.16f, 1.10f, 0.68f, 1.08f, 0.04f);
}

static scene::ParticleRenderProfile adhesiveGrenadeProfile() {
    return grenadeProfile(0.90f, 0.85f, 0.75f, 0.18f, 1.20f, 0.65f, 1.05f, 0.03f);
}

static scene::ParticleRenderProfile cryobanGrenadeProfile() {
    return grenadeProfile(0.85f, 0.80f, 0.65f, 0.15f, 1.00f, 0.70f, 1.08f, 0.04f);
}

static scene::ParticleRenderProfile plasmaGrenadeProfile() {
    return grenadeProfile(0.75f, 0.70f, 0.45f, 0.10f, 0.70f, 0.82f, 1.15f, 0.07f);
}

static scene::ParticleRenderProfile ionGrenadeProfile() {
    return grenadeProfile(0.74f, 0.72f, 0.42f, 0.09f, 0.65f, 0.85f, 1.12f, 0.08f);
}

scene::ParticleRenderProfile particleRenderProfileForVisualEffect(
    resource::GameID gameId,
    const VisualEffectDesc &desc) {

    if (gameId != resource::GameID::KotOR || !desc.impRootMNode) {
        return {};
    }
    const auto &impactModel = desc.impRootMNode->name();
    if (desc.label == "VFX_FNF_GRENADE_FRAGMENTATION" && impactModel == "v_grnfrag_fnf") {
        return fragmentationGrenadeProfile();
    }
    if (desc.label == "VFX_FNF_GRENADE_STUN" && impactModel == "v_grnstun_fnf") {
        return stunGrenadeProfile();
    }
    if (desc.label == "VFX_FNF_GRENADE_THERMAL_DETONATOR" && impactModel == "v_grndeto_fnf") {
        return thermalDetonatorProfile();
    }
    if (desc.label == "VFX_FNF_GRENADE_POISON" && impactModel == "v_grnpois_fnf") {
        return poisonGrenadeProfile();
    }
    if (desc.label == "VFX_FNF_GRENADE_SONIC" && impactModel == "v_grnsonc_fnf") {
        return sonicGrenadeProfile();
    }
    if (desc.label == "VFX_FNF_GRENADE_ADHESIVE" && impactModel == "v_grnadhs_fnf") {
        return adhesiveGrenadeProfile();
    }
    if (desc.label == "VFX_FNF_GRENADE_CRYOBAN" && impactModel == "v_grncryo_fnf") {
        return cryobanGrenadeProfile();
    }
    if (desc.label == "VFX_FNF_GRENADE_PLASMA" && impactModel == "v_grnplas_fnf") {
        return plasmaGrenadeProfile();
    }
    if (desc.label == "VFX_FNF_GRENADE_ION" && impactModel == "v_grnion_fnf") {
        return ionGrenadeProfile();
    }
    return {};
}

} // namespace game

} // namespace reone
