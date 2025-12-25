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

#include "reone/system/arrayref.h"

#include <string.h>

namespace reone {

class StringRef : public ArrayRef<char> {
public:
    using Base = ArrayRef<char>;

    StringRef() :
        Base(nullptr, (size_t)0) {}

    StringRef(const std::string &str) :
        Base(&str[0], str.size()) {}

    StringRef(const char *str, size_t len) :
        Base(str, len) {}

    StringRef(const char *str) :
        Base(str, strlen(str)) {}

    static constexpr size_t npos = -1;

    bool operator==(const StringRef &other) const;

    /**
     * Convert StringRef to std::string.
     */
    std::string str() const { return std::string(_begin, _size); }

    /**
     * Take a substring in range [begin, end).
     * Here \p begin is a start index for the substring, and \p end is 1 past
     * the end of the substring.
     */
    StringRef substr(size_t begin, size_t end) const;

    /**
     * Remove all characters from \p trimChars from the beginning of the string.
     */
    StringRef lstrip(StringRef trimChars = "\r\n\t ") const;

    /**
     * Remove all characters from \p trimChars from the end of the string.
     */
    StringRef rstrip(StringRef trimChars = "\r\n\t ") const;

    StringRef strip(StringRef trimChars = "\r\n\t ") const;

    /**
     * Find the first occurrence of \p sub and return its index. Return
     * StringRef::npos if not found.
     */
    size_t find(StringRef sub) const;

    /**
     * Find the last occurrence of \p sub and return its index. Return
     * StringRef::npos if not found.
     */
    size_t rfind(StringRef sub) const;
};

} // namespace reone
