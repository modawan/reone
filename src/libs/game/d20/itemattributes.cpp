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

#include "reone/game/d20/itemattributes.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/object/item.h"

namespace reone {
namespace game {

void ItemAttributes::addItem(std::shared_ptr<Item> item, GameServices &game) {
    std::optional<SpellType> spellType = item->activateSpell();
    if (!spellType) {
        return;
    }

    std::shared_ptr<Spell> spell = game.spells.get(spellType.value());
    if (!spell) {
        return;
    }

    if (spell->hostile) {
        _attackingSpells.emplace(std::move(item), std::move(spell));
        return;
    }

    if (spell->itemTargeting) {
        _defensiveSpells.emplace(std::move(item), std::move(spell));
        return;
    }

    _healingSpells.emplace(std::move(item), std::move(spell));
}

void ItemAttributes::removeItem(std::shared_ptr<Item> item) {
    _healingSpells.erase(item);
    _defensiveSpells.erase(item);
    _attackingSpells.erase(item);
}

} // namespace game
} // namespace reone
