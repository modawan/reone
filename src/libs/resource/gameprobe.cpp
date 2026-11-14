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

#include <stdexcept>

#include "reone/system/fileutil.h"

namespace reone {

namespace resource {

// The voice folders tell the games apart on every platform: streamwaves for
// KotOR, streamvoice for TSL. A directory with neither, or both, is neither.
GameID GameProbe::probe() {
    if (!findFileIgnoreCase(_gamePath, "chitin.key")) {
        throw std::runtime_error("chitin.key not found in " + _gamePath.string());
    }
    const bool kotor = findFileIgnoreCase(_gamePath, "streamwaves").has_value();
    const bool tsl = findFileIgnoreCase(_gamePath, "streamvoice").has_value();
    if (kotor && !tsl) {
        return GameID::KotOR;
    }
    if (tsl && !kotor) {
        return GameID::TSL;
    }
    throw std::runtime_error("Unable to tell KotOR from TSL in " + _gamePath.string());
}

} // namespace resource

} // namespace reone
