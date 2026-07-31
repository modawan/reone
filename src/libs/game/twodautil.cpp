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

#include "reone/game/twodautil.h"

#include "reone/resource/2da.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/exception/validation.h"

using namespace reone::resource;

namespace reone {

namespace game {

static bool hasColumn(const TwoDA &table, const std::string &column) {
    return std::find(
               table.columns().begin(),
               table.columns().end(),
               column) != table.columns().end();
}

static int parseInt(const std::string &value) {
    bool hexadecimal = value.size() > 2 &&
                       value[0] == '0' &&
                       (value[1] == 'x' || value[1] == 'X');
    return std::stoi(value, nullptr, hexadecimal ? 16 : 10);
}

std::shared_ptr<TwoDA> getRequiredTwoDA(
    ITwoDAs &twoDas,
    const std::string &resRef) {

    auto table = twoDas.get(resRef);
    if (!table) {
        throw ResourceNotFoundException("2DA not found: " + resRef);
    }
    return table;
}

void validateTwoDARow(
    const TwoDA &table,
    const std::string &resRef,
    int row) {

    if (row < 0 || row >= table.getRowCount()) {
        throw ValidationException(str(boost::format(
            "%s.2da row out of range: %d/%d") %
            resRef % row % table.getRowCount()));
    }
}

static void validateTwoDALookup(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column) {

    validateTwoDARow(table, resRef, row);
    if (!hasColumn(table, column)) {
        throw ValidationException(
            resRef + ".2da column not found: " + column);
    }
}

static std::optional<std::string> getTwoDAStringOpt(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column) {

    validateTwoDALookup(table, resRef, row, column);
    return table.getStringOpt(row, column);
}

std::optional<int> getTwoDAIntOpt(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column) {

    auto value = getTwoDAStringOpt(table, resRef, row, column);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return parseInt(*value);
}

static std::optional<float> getTwoDAFloatOpt(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column) {

    auto value = getTwoDAStringOpt(table, resRef, row, column);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return std::stof(*value);
}

std::string getRequiredTwoDAString(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column) {

    auto value = getTwoDAStringOpt(table, resRef, row, column);
    if (!value || value->empty()) {
        throw ValidationException(str(boost::format(
            "%s.2da value missing: row %d, column %s") %
            resRef % row % column));
    }
    return *value;
}

int getRequiredTwoDAInt(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column) {

    auto value = getTwoDAIntOpt(table, resRef, row, column);
    if (!value) {
        throw ValidationException(str(boost::format(
            "%s.2da value missing: row %d, column %s") %
            resRef % row % column));
    }
    return *value;
}

std::string getTwoDAStringOrBlank(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column,
    std::string blankValue) {

    auto value = getTwoDAStringOpt(table, resRef, row, column);
    return !value || value->empty() ? std::move(blankValue) : *value;
}

int getTwoDAIntOrBlank(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column,
    int blankValue) {

    return getTwoDAIntOpt(table, resRef, row, column).value_or(blankValue);
}

float getTwoDAFloatOrBlank(
    const TwoDA &table,
    const std::string &resRef,
    int row,
    const std::string &column,
    float blankValue) {

    return getTwoDAFloatOpt(table, resRef, row, column).value_or(blankValue);
}

} // namespace game

} // namespace reone
