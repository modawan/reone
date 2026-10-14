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

#include "../action.h"

namespace reone {

namespace game {

class Creature;

/** Moves each weapon set into the other's slots and resumes a direct attack. */
void swapWeaponSets(Game &game, Creature &creature);

class SwitchWeaponsAction : public Action {
public:
    SwitchWeaponsAction(Game &game, ServicesView &services) :
        Action(game, services, ActionType::SwitchWeapons) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::SwitchWeapons;
    }

    uint32_t serializedActionId() const override { return static_cast<uint32_t>(SavedActionType::SwitchWeapons); }
    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    bool cancel(std::shared_ptr<Action> self, Object &actor) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
};

} // namespace game

} // namespace reone
