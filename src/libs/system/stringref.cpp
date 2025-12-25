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

#include "reone/system/stringref.h"
#include <cstring>

namespace reone {

bool StringRef::operator==(const StringRef &other) const {
    if (size() != other.size()) {
        return false;
    }
    return memcmp(begin(), other.begin(), size()) == 0;
}

StringRef StringRef::substr(size_t begin, size_t end) const {
    assert((begin <= _size) && (end <= _size) && "out-of-bounds substr");
    assert(begin <= end && "invalid substr");
    return StringRef(_begin + begin, end - begin);
}

StringRef StringRef::lstrip(StringRef trimChars) const {
    for (size_t i = 0; i < _size; ++i) {
        char c = _begin[i];
        bool shouldStrip = false;
        for (char t : trimChars) {
            if (c == t) {
                shouldStrip = true;
                break;
            }
        }

        if (!shouldStrip) {
            return StringRef(_begin + i, _size - i);
        }
    }

    return StringRef(_begin + _size, 0);
}

StringRef StringRef::rstrip(StringRef trimChars) const {
    for (size_t i = _size; i > 0; --i) {
        char c = _begin[i - 1];
        bool shouldStrip = false;
        for (char t : trimChars) {
            if (c == t) {
                shouldStrip = true;
                break;
            }
        }

        if (!shouldStrip) {
            return StringRef(_begin, i);
        }
    }

    return StringRef(_begin, 0);
}

StringRef StringRef::strip(StringRef trimChars) const {
    return lstrip(trimChars).rstrip(trimChars);
}

size_t StringRef::find(StringRef sub) const {
    if (size() < sub.size()) {
        return StringRef::npos;
    }

    for (size_t i = 0; i <= (size() - sub.size()); ++i) {
        if (substr(i, i + sub.size()) == sub) {
            return i;
        }
    }
    return StringRef::npos;
}

size_t StringRef::rfind(StringRef sub) const {
    if (size() < sub.size()) {
        return StringRef::npos;
    }

    for (size_t i = size(); i >= sub.size(); --i) {
        size_t begin = i - sub.size();
        if (substr(begin, i) == sub) {
            return begin;
        }
    }
    return StringRef::npos;
}

} // namespace reone
