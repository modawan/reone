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

#include "reone/game/footstepsounds.h"

#include <boost/algorithm/string.hpp>

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

std::shared_ptr<FootstepTypeSounds> FootstepSounds::doGet(uint32_t type) {
    std::shared_ptr<FootstepTypeSounds> result;

    std::shared_ptr<TwoDA> twoDa(_twoDas.get("footstepsounds"));
    if (twoDa) {
        result = std::make_shared<FootstepTypeSounds>();
        for (const auto &column : twoDa->columns()) {
            result->cells.emplace(boost::to_lower_copy(column), twoDa->getString(static_cast<int>(type), column));
        }
        result->pitchOffset = twoDa->getFloat(static_cast<int>(type), "pitchoffset");
    }

    return result;
}

} // namespace game

} // namespace reone
