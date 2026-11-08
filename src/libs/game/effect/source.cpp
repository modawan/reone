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

#include "reone/game/effect/source.h"

#include <algorithm>
#include <limits>

#include "reone/game/effect.h"
#include "reone/game/object/item.h"

namespace reone {

namespace game {

EffectSourceKey getEffectSourceKey(const EffectInstance &effect) {
    if (auto creator = effect.boundCreator()) {
        if (dyn_cast<Item>(creator.get())) {
            return {
                EffectSourceKind::Item,
                creator->runtimeIncarnation(),
            };
        }
    }
    return getEffectSpellKey(effect);
}

EffectSourceKey getEffectSpellKey(const EffectInstance &effect) {
    if (effect.spellId != std::numeric_limits<uint32_t>::max()) {
        return {
            EffectSourceKind::Spell,
            effect.spellId,
        };
    }
    return {EffectSourceKind::Independent, effect.id};
}

void EffectSourceSlots::add(EffectSourceKey source, int amount, bool addsUp) {
    if (source.kind != EffectSourceKind::Independent) {
        auto it = _bySource.find(source);
        if (it != _bySource.end()) {
            it->second = addsUp ? it->second + amount : std::max(it->second, amount);
            return;
        }
    }
    if (_sources == _capacity) {
        return;
    }
    ++_sources;
    if (source.kind == EffectSourceKind::Independent) {
        _independent += amount;
    } else {
        _bySource.emplace(source, amount);
    }
}

int EffectSourceSlots::total() const {
    int sum = _independent;
    for (const auto &[source, amount] : _bySource) {
        sum += amount;
    }
    return sum;
}

EffectBonusReducer EffectBonusReducer::forAbility(bool tsl) {
    return EffectBonusReducer {getAbilityEffectIncreaseCap(tsl), getAbilityEffectDecreaseCap(tsl), true, getEffectSourceCapacity(tsl)};
}

EffectBonusReducer EffectBonusReducer::forSkill(bool tsl) {
    return EffectBonusReducer {getSkillEffectCap(tsl), getSkillEffectCap(tsl), false, getEffectSourceCapacity(tsl)};
}

void EffectBonusReducer::addIncrease(EffectSourceKey source, int amount) {
    _increases.add(source, amount);
}

void EffectBonusReducer::addDecrease(EffectSourceKey source, int amount) {
    _decreases.add(source, amount, _decreasesAddUp);
}

int EffectBonusReducer::total() const {
    return std::min(_increases.total(), _increaseCap) - std::min(_decreases.total(), _decreaseCap);
}

} // namespace game

} // namespace reone
