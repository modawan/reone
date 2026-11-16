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

namespace reone {

/**
 * The version this build reports: REONE_VERSION when CMake was given one,
 * otherwise `git describe --tags --always --dirty` of the source tree when
 * CMake was configured, otherwise "unknown".
 */
const char *versionString();

/**
 * The installed data directory relative to the installed engine, launcher and
 * toolkit (e.g. "../../share/reone"), from CMake's install directories.
 * Empty when the build has no install layout (Windows, macOS).
 */
const char *relativeDataDirectory();

} // namespace reone
