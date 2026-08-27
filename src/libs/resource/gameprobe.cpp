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

#include "reone/resource/gameprobe.h"
#include "reone/system/fileutil.h"
#include "reone/system/logutil.h"

namespace reone {

namespace resource {

GameID GameProbe::probe() {
    auto chitin = findFileIgnoreCase(_gamePath, "chitin.key");
    if (!chitin) {
        error("chitin.key is missing");
        throw std::runtime_error("Unable to determine game ID: " + _gamePath.string());
    }

    auto k2 = findFileIgnoreCase(_gamePath, "swkotor2.ini");
    if (k2) {
        return GameID::TSL;
    }

    return GameID::KotOR;
}

} // namespace resource

} // namespace reone
