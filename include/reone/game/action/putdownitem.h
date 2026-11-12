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
 * A creature puts down an item it wears, carries or holds in a container it
 * carries, at a point: where it stood when told to. Farther than 1.1 metres
 * from the point it first walks to a metre short of it, running from two
 * metres, and turns to it; there, it turns to the point and plays the
 * get-from-low animation, or get-from-middle for a point more than a metre
 * above its feet, then lays the item on the ground at the point, taking it
 * off first when worn, as a stack of its own. The dead, a party member at no
 * vitality, an item gone or held by another, and a point still out of reach
 * after the walk end it.
 */
class PutDownItemAction : public Action {
public:
    PutDownItemAction(Game &game,
                      ServicesView &services,
                      std::shared_ptr<Item> item,
                      glm::vec3 point) :
        Action(game, services, ActionType::PutDownItem),
        _item(std::move(item)),
        _point(point) {
        requireRuntimeObject(_item);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::PutDownItem;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved PutDownItem command; none once the item is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Item> _item;
    /** Where the item goes down. */
    glm::vec3 _point;
    /** The walk to the point has been queued ahead of the put-down. */
    bool _approached {false};
    /** The animation of laying the item down has been queued ahead of it. */
    bool _animationQueued {false};
};

} // namespace game

} // namespace reone
