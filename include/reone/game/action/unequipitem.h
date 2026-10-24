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

class Item;

class UnequipItemAction : public Action {
public:
    UnequipItemAction(Game &game, ServicesView &services, std::shared_ptr<Item> item, int32_t instant) :
        Action(game, services, ActionType::UnequipItem),
        _item(std::move(item)),
        _instant(instant) {
        requireRuntimeObject(_item);
    }

    UnequipItemAction(Game &game, ServicesView &services, std::shared_ptr<Item> item,
                      int32_t flags, std::shared_ptr<Item> container) :
        UnequipItemAction(game, services, std::move(item), flags) {
        if (container) {
            _containerId = container->id();
            requireRuntimeObject(container);
        }
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::UnequipItem;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
    /** A saved UnequipItem command; none once the item is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

    const std::shared_ptr<Item> &item() const { return _item; }
    /**
     * A later unequip of the same item writes its instant flag over the
     * container. The container then names no item, and the unequip takes
     * nothing off when it runs. The written value is kept as it is, so a
     * save carries it unchanged.
     */
    void overwriteContainer(int32_t instant);

private:
    std::shared_ptr<Item> _item;
    /**
     * The container the item goes into; invalid for the owner's inventory.
     * Once written over, the value written, which names no item.
     */
    uint32_t _containerId {kSavedRuntimeInvalidObjectId};
    bool _containerOverwritten {false};
    int32_t _instant;
};

} // namespace game

} // namespace reone
