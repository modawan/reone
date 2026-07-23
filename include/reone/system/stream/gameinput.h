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

#include "fileinput.h"

namespace reone {

/**
 * Open a game data file for reading. A dedicated seam, so that alternative
 * backends can be plugged in without touching call sites.
 */
inline std::unique_ptr<IInputStream> openGameInputStream(const std::filesystem::path &path) {
    return std::make_unique<FileInputStream>(path);
}

} // namespace reone
