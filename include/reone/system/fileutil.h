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

std::filesystem::path getFileIgnoreCase(const std::filesystem::path &dir, std::string_view relPath);
std::optional<std::filesystem::path> findFileIgnoreCase(const std::filesystem::path &dir, std::string_view relPath);

/** KotOR ResRef rules: 1-16 alphanumeric or underscore characters. */
bool isValidResRef(std::string_view name, size_t maxLen = 16);

/**
 * Whether name is safe to use as a single path component under a trusted
 * parent directory. Permits characters found in the wild, e.g. in savegame
 * directory names ("000043 - Game42"), while rejecting anything that could
 * escape the parent: ".", "..", separators, drive qualifiers and absolute
 * paths. Not a ResRef check - use isValidResRef for ResRefs.
 */
bool isSafePathComponent(std::string_view name);

} // namespace reone
