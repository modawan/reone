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

#pragma once

#include <algorithm>
#include <functional>
#include <optional>

#include "../effect.h"

namespace reone {

namespace game {

/**
 * The balance of haste over slow on a creature, each held to -1, 0 or 1:
 * before and after a haste or slow is applied or removed.
 */
struct HasteSlowTransition {
    int before;
    int after;
};

/**
 * How the balance of haste over slow in \p records changes when a haste or
 * slow of saved type \p type is applied, or removed when \p removing.
 */
template <class Records>
HasteSlowTransition getHasteSlowTransition(const Records &records, uint16_t type, bool removing) {
    int balance = 0;
    for (const auto &record : records) {
        if (record.serializedType > SavedEffectTypes::slow) {
            break;
        }
        if (record.serializedType == SavedEffectTypes::haste) {
            ++balance;
        } else if (record.serializedType == SavedEffectTypes::slow) {
            --balance;
        }
    }
    const int contribution = type == SavedEffectTypes::haste ? 1 : -1;
    return {std::clamp(balance, -1, 1),
            std::clamp(balance + (removing ? -contribution : contribution), -1, 1)};
}

/**
 * The record that hastes the creature when \p selection is positive, or slows
 * it, for good. It takes the spell of the haste or slow being applied, if any.
 */
inline EffectInstance makeHasteSlowInternal(int selection,
                                            std::optional<std::reference_wrapper<const EffectInstance>> applyingRoot) {
    EffectInstance result;
    result.serializedType = selection > 0 ? SavedEffectTypes::hasteInternal : SavedEffectTypes::slowInternal;
    result.setCategory(EffectCategory::Magical);
    result.setDuration(DurationType::Innate, 0.0f);
    result.creatorId = kSavedEffectInvalidObjectId;
    result.markGeneratedForLoad();
    if (applyingRoot) {
        result.spellId = applyingRoot->get().spellId;
        result.restoring = applyingRoot->get().restoring;
    }
    return result;
}

/**
 * A haste or a slow applied to a creature. Hastes and slows cancel each
 * other; while either side prevails, one record of it carries the bonuses or
 * penalties.
 */
class HasteSlowEffect : public CopyableEffect<HasteSlowEffect> {
public:
    explicit HasteSlowEffect(bool haste) :
        CopyableEffect(haste ? EffectType::Haste : EffectType::Slow) {
    }

    EffectApplicationResult onApply(Object &, EffectInstance &) override;
    EffectRemovalResult onRemove(Object &, const EffectInstance &) override;
};

/** The record that carries the bonuses of haste or the penalties of slow. */
class HasteSlowInternalEffect : public CopyableEffect<HasteSlowInternalEffect> {
public:
    explicit HasteSlowInternalEffect(bool haste) :
        CopyableEffect(EffectType::Invalid),
        _haste(haste) {
    }

    EffectInstance saveFacingInstance() const override;
    EffectApplicationResult onApply(Object &, EffectInstance &) override;
    EffectRemovalResult onRemove(Object &, const EffectInstance &) override;

private:
    bool _haste;
};

} // namespace game

} // namespace reone
