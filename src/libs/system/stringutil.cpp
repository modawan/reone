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

#include "reone/system/stringutil.h"

namespace reone {

std::string_view string_lstrip(std::string_view s, std::string_view trimChars) {
    size_t stripTo = s.find_first_not_of(trimChars);
    if (stripTo == s.npos) {
        return std::string_view();
    }

    s.remove_prefix(stripTo);
    return s;
}

std::string_view string_rstrip(std::string_view s, std::string_view trimChars) {
    size_t stripTo = s.find_last_not_of(trimChars);
    if (stripTo == s.npos) {
        return std::string_view();
    }

    s.remove_suffix(s.size() - (stripTo + 1));
    return s;
}

std::string_view string_strip(std::string_view s, std::string_view trimChars) {
    return string_rstrip(string_lstrip(s, trimChars), trimChars);
}

} // namespace reone
