/*
 * Copyright (c) 2025 The reone project contributors
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

#include <string>

namespace reone {

/**
 * Remove leading \p trimChars from a string.
 */
std::string_view string_lstrip(std::string_view s, std::string_view trimChars = "\r\n\t ");

/**
 * Remove trailing \p trimChars from a string.
 */
std::string_view string_rstrip(std::string_view s, std::string_view trimChars = "\r\n\t ");

/**
 * Remove leading and trailing \p trimChars from a string.
 */
std::string_view string_strip(std::string_view s, std::string_view trimChars = "\r\n\t ");

} // namespace reone
