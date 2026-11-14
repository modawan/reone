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

#include <functional>
#include <string>

#include "object/item.h"

namespace reone {

namespace game {

class Game;
struct ServicesView;

/**
 * The text the inventory and equipment screens show for an item: its
 * property lines, built from interface strings and the item property tables,
 * followed by its description, or a placeholder when it has none.
 */
std::string buildItemDescription(const Item &item, const Game &game, ServicesView &services);

/**
 * A line for each property of the item that include chooses, active or not,
 * up to five lines: its name, cost and subtype in TSL's short property names,
 * as a workbench lists a held upgrade's properties.
 */
std::string buildItemPropertyLines(
    const Item &item,
    const Game &game,
    ServicesView &services,
    const std::function<bool(const Item::PropertyEntry &)> &include);

/**
 * The item's properties keyed to an upgrade row, as a workbench names a slot's
 * row: a header and the properties' lines in ascending order, each ending in a
 * line break; empty when no property is keyed to the row.
 *
 * @param row the upgrade row the properties are keyed to
 */
std::string buildKeyedPropertyText(const Item &item, int row, const Game &game, ServicesView &services);

} // namespace game

} // namespace reone
