/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "../location.h"

#include "castspell.h"

namespace reone {

namespace game {

/** A spell cast or an item use aimed at a location. */
class CastSpellAtLocationAction : public CastSpellAction {
public:
    CastSpellAtLocationAction(
        Game &game,
        ServicesView &services,
        std::shared_ptr<Spell> spell,
        const std::shared_ptr<Location> &targetLocation,
        bool cheat,
        ProjectilePathType projectilePathType,
        bool instantSpell,
        std::optional<std::shared_ptr<Item>> item = std::nullopt,
        std::optional<size_t> itemProperty = std::nullopt,
        std::optional<int> itemCasterLevel = std::nullopt,
        std::optional<SpellSelection> selection = std::nullopt,
        std::optional<FeatType> associatedFeat = std::nullopt,
        bool fake = false);

    static bool classof(Action *from) {
        return from->type() == ActionType::CastSpellAtLocation;
    }
};

} // namespace game

} // namespace reone
