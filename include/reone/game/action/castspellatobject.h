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

#include "castspell.h"

namespace reone {

namespace game {

/** A spell cast or an item use aimed at an object. */
class CastSpellAtObjectAction : public CastSpellAction {
public:
    CastSpellAtObjectAction(
        Game &game,
        ServicesView &services,
        std::shared_ptr<Spell> spell,
        std::shared_ptr<Object> target,
        std::optional<std::shared_ptr<Item>> item,
        bool cheat = false,
        ProjectilePathType projectilePathType = ProjectilePathType::Default,
        bool instantSpell = false,
        std::optional<size_t> itemProperty = std::nullopt,
        std::optional<int> itemCasterLevel = std::nullopt,
        std::optional<SpellSelection> selection = std::nullopt,
        std::optional<FeatType> associatedFeat = std::nullopt,
        bool fake = false);

    static bool classof(Action *from) {
        return from->type() == ActionType::CastSpellAtObject;
    }

    /** The same item use, made at the command's location with no target. */
    std::shared_ptr<Action> toLocationUse() const;

    /**
     * The location the command carries, which the spell's target location
     * reports until the impact: none (the origin) for a script or menu cast,
     * the target's position when the command was given for a talent or an
     * item used at another creature.
     */
    void setCommandLocation(const glm::vec3 &location) { _location = location; }
};

} // namespace game

} // namespace reone
