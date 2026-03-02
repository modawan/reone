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

#include "reone/game/visualeffects.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/system/logutil.h"

using namespace reone::resource;

namespace reone {

namespace game {

// Effects are identified by string labels in the .2da, but used as integers
// in scripts. It appears that they are only mapped in nwscript.nss, so we
// have to hardcode them.
static std::unordered_map<std::string, uint32_t> workaroundLabels() {
    std::unordered_map<std::string, uint32_t> labels;

    labels["VFX_FNF_GRENADE_FRAGMENTATION"] = 3003;
    labels["VFX_FNF_GRENADE_STUN"] = 3004;
    labels["VFX_FNF_GRENADE_THERMAL_DETONATOR"] = 3005;
    labels["VFX_FNF_GRENADE_POISON"] = 3006;
    labels["VFX_FNF_GRENADE_SONIC"] = 3007;
    labels["VFX_FNF_GRENADE_ADHESIVE"] = 3008;
    labels["VFX_FNF_GRENADE_CRYOBAN"] = 3009;
    labels["VFX_FNF_GRENADE_PLASMA"] = 3010;
    labels["VFX_FNF_GRENADE_ION"] = 3011;

    return labels;
}

static void parseEffects(const resource::TwoDA &twoDa,
                         resource::Models &models,
                         resource::AudioClips &audioClips,
                         std::map<uint32_t, VisualEffectDesc> &effects) {
    std::unordered_map<std::string, uint32_t> labels = workaroundLabels();

    for (int row = 0; row < twoDa.getRowCount(); ++row) {
        VisualEffectDesc desc;
        desc.label = twoDa.getString(row, "label");
        desc.impRootMNode = models.get(twoDa.getString(row, "imp_root_m_node"));
        desc.soundImpact = audioClips.get(twoDa.getString(row, "soundimpact"));
        effects.insert({labels[desc.label], desc});
    }
}

void VisualEffects::init() {
    std::shared_ptr<TwoDA> effectsDa(_twoDas.get("visualeffects"));
    if (!effectsDa) {
        return;
    }

    parseEffects(*effectsDa, _models, _audioClips, _effects);
}

std::optional<const VisualEffectDesc *> VisualEffects::get(uint32_t id) const {
    auto it = _effects.find(id);
    if (it == _effects.end()) {
        return std::nullopt;
    }
    return &it->second;
}

} // namespace game

} // namespace reone
