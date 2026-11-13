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
#include "../object/item.h"

namespace reone {

namespace game {

/**
 * A creature picks up an item lying in its area. Farther than 1.1 metres from
 * the item it first walks to a metre short of it, running from two metres,
 * and turns to it; beside it, it turns to it and plays the get-from-low
 * animation, then takes the item into its inventory. The dead, a party member
 * at no vitality, an item gone or held by anyone, and an item still out of
 * reach after the walk end it.
 */
class PickUpItemAction : public Action {
public:
    PickUpItemAction(Game &game,
                     ServicesView &services,
                     std::shared_ptr<Item> item) :
        Action(game, services, ActionType::PickUpItem),
        _item(std::move(item)) {
        requireRuntimeObject(_item);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::PickUpItem;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved PickUpItem command; none once the item is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Item> _item;
    /** The walk to the item has been queued ahead of the pick-up. */
    bool _approached {false};
    /** The animation of getting the item has been queued ahead of the pick-up. */
    bool _animationQueued {false};
};

} // namespace game

} // namespace reone
