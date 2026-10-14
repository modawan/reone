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

#include <algorithm>

#include <boost/algorithm/string/predicate.hpp>

#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/system/logutil.h"

using namespace reone::resource;

namespace reone {

namespace game {

static void parseEffects(const resource::TwoDA &twoDa,
                         resource::Models &models,
                         resource::AudioClips &audioClips,
                         std::map<uint32_t, VisualEffectDesc> &effects) {
    for (int row = 0; row < twoDa.getRowCount(); ++row) {
        VisualEffectDesc desc;
        desc.label = twoDa.getString(row, "label");
        desc.rootHugeModel = models.get(twoDa.getString(row, "imp_root_h_node"));
        desc.rootLargeModel = models.get(twoDa.getString(row, "imp_root_l_node"));
        desc.rootMediumModel = models.get(twoDa.getString(row, "imp_root_m_node"));
        desc.impactModel = models.get(twoDa.getString(row, "imp_impact_node"));
        desc.headModel = models.get(twoDa.getString(row, "imp_headcon_node"));
        desc.orientationOff = twoDa.getInt(row, "orientationoff", 0) != 0;
        for (const char *column : {"imp_headcon_node", "imp_impact_node", "imp_root_m_node",
                                   "imp_root_s_node", "imp_root_l_node", "imp_root_h_node"}) {
            auto model = twoDa.getString(row, column);
            if (!model.empty()) {
                desc.locationModel = models.get(model);
                break;
            }
        }
        desc.soundImpact = audioClips.get(twoDa.getString(row, "soundimpact"));
        desc.progFXImpact = twoDa.getInt(row, "progfx_impact", -1);
        desc.progFXDuration = twoDa.getInt(row, "progfx_duration", -1);
        desc.progFXCessation = twoDa.getInt(row, "progfx_cessation", -1);
        desc.fireAndForget = boost::iequals(twoDa.getString(row, "type_fd"), "F");
        desc.soundDuration = audioClips.get(twoDa.getString(row, "soundduration"));
        // Visual effects play no cessation sound, so soundcessastion is not read.
        // Rows are keyed by their label, the visual effect's number.
        effects.emplace(static_cast<uint32_t>(std::stoul(twoDa.rows()[row].label)), std::move(desc));
    }
}

void VisualEffects::init() {
    // A blank cell, like a slot past the end of the table, names no visual
    // effect.
    auto hitVisuals = getRequiredTwoDA(_twoDas, "damagehitvisual");
    int slotCount = std::min(kDamageSlotCount, hitVisuals->getRowCount());
    for (int slot = 0; slot < slotCount; ++slot) {
        _hitVisuals[slot].melee = hitVisuals->getInt(slot, "visualeffect", 0);
        _hitVisuals[slot].ranged = hitVisuals->getInt(slot, "rangedeffect", 0);
    }

    auto effectsDa = getRequiredTwoDA(_twoDas, "visualeffects");
    _effects.clear();
    parseEffects(*effectsDa, _models, _audioClips, _effects);
}

void VisualEffects::clear() {
    _effects.clear();
}

std::optional<std::reference_wrapper<const VisualEffectDesc>> VisualEffects::get(uint32_t id) const {
    auto it = _effects.find(id);
    if (it == _effects.end()) {
        return std::nullopt;
    }
    return std::cref(it->second);
}

int VisualEffects::hitVisual(int damageSlot, bool ranged) const {
    const auto &visuals = _hitVisuals[damageSlot];
    return ranged ? visuals.ranged : visuals.melee;
}

} // namespace game

} // namespace reone
