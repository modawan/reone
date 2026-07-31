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

#include <memory>
#include <optional>
#include <string>

namespace reone {

namespace resource {
class ITwoDAs;
class TwoDA;
}

namespace game {

std::shared_ptr<resource::TwoDA> getRequiredTwoDA(
    resource::ITwoDAs &twoDas,
    const std::string &resRef);

void validateTwoDARow(
    const resource::TwoDA &table,
    const std::string &resRef,
    int row);

std::optional<int> getTwoDAIntOpt(
    const resource::TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column);

std::string getRequiredTwoDAString(
    const resource::TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column);

int getRequiredTwoDAInt(
    const resource::TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column);

std::string getTwoDAStringOrBlank(
    const resource::TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column,
    std::string blankValue);

int getTwoDAIntOrBlank(
    const resource::TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column,
    int blankValue);

float getTwoDAFloatOrBlank(
    const resource::TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column,
    float blankValue);

} // namespace game

} // namespace reone
