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

#include "modulediscovery.h"
#include "types.h"

#include <array>

namespace reone::resource {

class Strings;

struct OdysseyResourceRoots {
    static constexpr std::size_t kLivePackageCount = 6;

    std::optional<std::filesystem::path> nwmFiles;
    std::array<std::optional<std::filesystem::path>, kLivePackageCount> livePackages;
    std::vector<std::filesystem::path> k2OverrideRoots;
};

OdysseyResourceRoots defaultOdysseyResourceRoots(const std::filesystem::path &gamePath);

/// Load every bound package TLK into its stable observable slot.
void loadLiveTalkTables(Strings &strings, const OdysseyResourceRoots &roots);

std::vector<std::filesystem::path> looseOverrideRoots(
    GameID game,
    const std::filesystem::path &gamePath,
    const OdysseyResourceRoots &roots);

std::vector<std::filesystem::path> lipsRoots(
    GameID game,
    const std::filesystem::path &gamePath,
    const OdysseyResourceRoots &roots);

std::vector<ModuleSearchRoot> primaryModuleSearchRoots(
    GameID game,
    const std::filesystem::path &gamePath,
    const OdysseyResourceRoots &roots);

std::vector<ModuleSearchRoot> moduleSearchRoots(
    GameID game,
    const std::filesystem::path &gamePath,
    const OdysseyResourceRoots &roots);

} // namespace reone::resource
