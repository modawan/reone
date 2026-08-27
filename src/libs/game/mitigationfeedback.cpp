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

#include "reone/game/mitigationfeedback.h"

#include "reone/resource/strings.h"

#include <cassert>
#include <utility>

namespace reone {

namespace game {

static constexpr int kStrRefDamageResistance = 1454;
static constexpr int kStrRefDamageReduction = 1455;
static constexpr int kStrRefFiniteDamageResistance = 1456;
static constexpr int kStrRefFiniteDamageReduction = 1457;
static constexpr int kStrRefDamageImmunity = 1458;
static constexpr int kStrRefObjectNameFallback = 0;
static constexpr int kStrRefUniversalDamage = 1422;
static constexpr int kStrRefPhysicalDamage = 1423;
static constexpr int kStrRefAcidDamage = 1440;
static constexpr int kStrRefColdDamage = 1441;
static constexpr int kStrRefLightSideDamage = 1442;
static constexpr int kStrRefElectricalDamage = 1443;
static constexpr int kStrRefFireDamage = 1444;
static constexpr int kStrRefDarkSideDamage = 1445;
static constexpr int kStrRefSonicDamage = 1446;
static constexpr int kStrRefIonDamage = 1447;
static constexpr int kStrRefEnergyDamage = 1448;
static constexpr int kStrRefPoisonDamage = 41902;

static std::string resolveMitigationTargetName(
    resource::IStrings &strings,
    std::optional<std::string> targetName) {

    if (targetName) {
        return std::move(*targetName);
    }
    return strings.getText(kStrRefObjectNameFallback);
}

static std::string getMitigationDamageTypeName(
    resource::IStrings &strings,
    int damageFlags) {

    int strRef = -1;
    if ((damageFlags & static_cast<int>(DamageType::Physical)) != 0) {
        strRef = kStrRefPhysicalDamage;
    } else {
        static constexpr std::pair<int, int> kDamageTypes[] {
            {0x0008, kStrRefUniversalDamage},
            {0x0010, kStrRefAcidDamage},
            {0x0020, kStrRefColdDamage},
            {0x0040, kStrRefLightSideDamage},
            {0x0080, kStrRefElectricalDamage},
            {0x0100, kStrRefFireDamage},
            {0x0200, kStrRefDarkSideDamage},
            {0x0400, kStrRefSonicDamage},
            {0x0800, kStrRefIonDamage},
            {0x1000, kStrRefEnergyDamage},
            {0x2000, kStrRefPoisonDamage},
        };
        for (const auto &[flag, candidateStrRef] : kDamageTypes) {
            if ((damageFlags & flag) != 0) {
                strRef = candidateStrRef;
                break;
            }
        }
    }
    if (strRef < 0) {
        return {};
    }

    // Damage-family strings contain a CUSTOM0 placeholder used by other
    // feedback contexts. This message supplies that token as an empty string.
    std::string name = strings.getText(strRef);
    static constexpr char kUnusedCustomToken[] = "<CUSTOM0>";
    std::string::size_type pos = 0;
    while ((pos = name.find(kUnusedCustomToken, pos)) != std::string::npos) {
        name.erase(pos, sizeof(kUnusedCustomToken) - 1);
    }
    return name;
}

MitigationFeedbackMessage buildMitigationFeedbackMessage(
    resource::IStrings &strings,
    std::optional<std::string> targetName,
    const MitigationFeedback &feedback) {

    std::string resolvedTargetName = resolveMitigationTargetName(
        strings,
        std::move(targetName));

    switch (feedback.type) {
    case MitigationFeedbackType::DamageImmunity:
        return {
            kStrRefDamageImmunity,
            {
                std::move(resolvedTargetName),
                getMitigationDamageTypeName(strings, feedback.damageFlags),
                {},
            },
            2,
        };
    case MitigationFeedbackType::DamageResistance:
        return {
            kStrRefDamageResistance,
            {
                std::move(resolvedTargetName),
                std::to_string(feedback.amount),
                {},
            },
            2,
        };
    case MitigationFeedbackType::DamageReduction:
        return {
            kStrRefDamageReduction,
            {
                std::move(resolvedTargetName),
                std::to_string(feedback.amount),
                {},
            },
            2,
        };
    case MitigationFeedbackType::FiniteDamageResistance:
        assert(feedback.remaining &&
               "finite damage-resistance feedback has no remainder");
        return {
            kStrRefFiniteDamageResistance,
            {
                std::move(resolvedTargetName),
                std::to_string(feedback.amount),
                std::to_string(feedback.remaining.value_or(0)),
            },
            3,
        };
    case MitigationFeedbackType::FiniteDamageReduction:
        assert(feedback.remaining &&
               "finite damage-reduction feedback has no remainder");
        return {
            kStrRefFiniteDamageReduction,
            {
                std::move(resolvedTargetName),
                std::to_string(feedback.amount),
                std::to_string(feedback.remaining.value_or(0)),
            },
            3,
        };
    }
    assert(false && "invalid mitigation feedback type");
    return {};
}

} // namespace game

} // namespace reone
