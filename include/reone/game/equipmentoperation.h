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

#include "equipmentrules.h"

namespace reone::game {

/** An ordinary gameplay result, independent of command delivery by a screen. */
enum class EquipmentOperationOutcome {
    Applied,
    Unchanged,
    Rejected,
    Failed
};

/**
 * Apply a selection using existing equipment and inventory operations.
 * A null item explicitly requests clearing the slot. All non-null objects must
 * be exact live objects in game; item must belong to sourceInventory.
 *
 * Recoverable equip rejection returns the taken candidate to sourceInventory.
 * A previously cleared paired hand stays in that inventory. Exceptions from
 * core split/transfer/effect operations propagate; this is not an atomic rollback
 * boundary over those operations.
 */
EquipmentOperationOutcome applyEquipmentOperation(
    Game &game,
    Creature &subject,
    Object &sourceInventory,
    const std::shared_ptr<Item> &item,
    int requestedSlot);

} // namespace reone::game
