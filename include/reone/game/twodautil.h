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

#include <memory>
#include <string>

#include "types.h"

namespace reone {

namespace resource {
class ITwoDAs;
class TwoDA;
} // namespace resource

namespace game {

std::shared_ptr<resource::TwoDA> getRequiredTwoDA(
    resource::ITwoDAs &twoDas,
    const std::string &resRef);

void validateTwoDARow(
    const resource::TwoDA &table,
    const std::string &resRef,
    int row);

/**
 * Throw unless \p row is a row of a table of \p rowCount rows.
 *
 * @param resRef the table's name, for the message
 */
void validateTwoDARowIndex(
    const std::string &resRef,
    int row,
    int rowCount);

/** A race's adjustment to an ability (racialtypes.2da); a blank one is zero. */
int readRacialAbilityAdjustment(
    const resource::TwoDA &table, int row, Ability ability);

} // namespace game

} // namespace reone
