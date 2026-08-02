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

} // namespace game

} // namespace reone
