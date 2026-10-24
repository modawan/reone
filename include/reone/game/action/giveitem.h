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

#include <optional>

#include "../action.h"
#include "../object.h"
#include "../object/item.h"

namespace reone {

namespace game {

/**
 * A creature or placeable gives an item to a creature or placeable. A count
 * short of the stack gives that many, split off the stack when the give
 * runs; otherwise the whole stack moves.
 */
class GiveItemAction : public Action {
public:
    /** The count that gives the whole stack. */
    static constexpr int kWholeStack = -1;

    /**
     * @param count how many of the stack to give; kWholeStack for all of it
     * @param givenByScript whether a script asked for the give; a party
     *                      member that receives such an item reports it
     */
    GiveItemAction(Game &game, ServicesView &services, std::shared_ptr<Item> item, std::shared_ptr<Object> giveTo, int count, bool givenByScript) :
        Action(game, services, ActionType::GiveItem),
        _item(std::move(item)),
        _giveTo(std::move(giveTo)),
        _count(count),
        _givenByScript(givenByScript) {
        requireRuntimeObject(_item);
        requireRuntimeObject(_giveTo);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::GiveItem;
    }

    /**
     * Queues a give on \p giver. The giver must hold the item, or it and the
     * creature that holds it must both be party members, and it must take
     * commands. A count above the stack gives the whole stack.
     *
     * @param count how many of the stack to give; kWholeStack for all of it
     * @param givenByScript whether a script asked for the give
     * @return whether the give was queued
     */
    static bool queue(Game &game, Object &giver, std::shared_ptr<Item> item, std::shared_ptr<Object> giveTo, int count, bool givenByScript);

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved GiveItem command; none once the item or the receiver is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Item> _item;
    std::shared_ptr<Object> _giveTo;
    /** How many of the stack to give; kWholeStack for all of it. */
    int _count;
    /** Whether a script asked for the give. */
    bool _givenByScript;
    /** Whether the giver runs to the receiver, fixed when it sets off. */
    std::optional<bool> _run;
};

} // namespace game

} // namespace reone
