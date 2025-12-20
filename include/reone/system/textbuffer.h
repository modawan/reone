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

#include <vector>

namespace reone {

class TextBuffer;

/**
 * TextBuffer is a sequential container with a file-like interface.
 */
class TextBuffer {
public:
    TextBuffer() {}
    TextBuffer(const TextBuffer &) = delete;

    /**
     * Print contents of the buffer to stderr, as well as the position of the
     * cursor.
     */
    void dump();

    bool empty() const { return _data.empty(); }

    /**
     * Returns the position of the cursor.
     */
    size_t tell() { return _cur; }

    /**
     * Move the cursor by \p offset relative to its current position. Offset
     * can be positive to move forward until the end of the buffer, or negative
     * to move backward until the beginning of the buffer
     */
    void seekCur(long offset);

    /**
     * Set the cursor to an absolute position. Negative \p offset values are
     * clamped to zero.
     */
    void seekSet(long offset) {
        _cur = 0;
        seekCur(offset);
    }

    /**
     * Move the cursor by \p offset relative to the end of the buffer. Note that
     * the end of the buffer points to the next character after the last one.
     */
    void seekEnd(long offset) {
        _cur = _data.size();
        seekCur(offset);
    }

    /**
     * Insert a character into the buffer *before* the cursor.
     */
    void write(char c);

    /**
     * Insert a string into the buffer *before* the cursor.
     */
    void write(std::string_view s);

    /**
     * Read a character at the cursor, then increment the cursor.
     */
    char read();

    /**
     * Read a character at the cursor.
     */
    char peek();

    /**
     * Delete a character *before* the cursor.
     */
    void erase();

    /**
     * Clear the entire buffer, reset the cursor to zero.
     */
    void clear() {
        _cur = 0;
        _data.clear();
    }

    /**
     * Get the whole buffer as a string.
     */
    std::string_view str() { return std::string_view(&_data[0], _data.size()); }

    /**
     * Search for a pattern forward starting from the cursor. If \p sub is
     * found, set the cursor at the beginning of the substring and return
     * true. Otherwise set the cursor to the end of the buffer and return false.
     */
    bool search(std::string_view sub);

    /**
     * Search for a pattern backward starting from the character before the
     * cursor. If \p sub is found, set the cursor at the beginning of the
     * substring and return true. Otherwise set the cursor to the beginning of
     * the buffer and return false.
     */
    bool rsearch(std::string_view sub);

    /**
     * Start from the cursor, search for a line break (\n or the end of the
     * buffer), and return a substring between these two positions. The returned
     * string includes a trailing \n if it was found. This function leaves the
     * cursor one character forward from the line break, or at the end of the
     * buffer.
     */
    std::string_view readline();

    /**
     * Start from the cursor, search backward for a line break (\n or the
     * beginning of the buffer), and return a substring between these two
     * positions. The returned string includes a trailing \n if it was
     * found. This function sets the cursor one character forward from the line
     * break at the beginning of the substring, or at the beginning of the
     * buffer.
     */
    std::string_view readlineReverse();

private:
    std::vector<char> _data;
    size_t _cur {0};
};

} // namespace reone
