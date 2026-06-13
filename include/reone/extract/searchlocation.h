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

#include <vector>

namespace reone {

namespace extract {

/// Mirrors PyKotor SearchLocation (installation.py) plus reone-only Executable.
enum class SearchLocation {
    Override = 0,
    Modules = 1,
    Chitin = 2,
    TexturesTpa = 3,
    TexturesTpb = 4,
    TexturesTpc = 5,
    TexturesGui = 6,
    Music = 7,
    Sound = 8,
    Voice = 9,
    Lips = 10,
    Rims = 11,
    CustomModules = 12,
    CustomFolders = 13,
    Executable = 14,
    Root = 15,
    Movies = 16,
};

using SearchScope = std::vector<SearchLocation>;

} // namespace extract

} // namespace reone
