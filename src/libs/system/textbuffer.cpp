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

#include "reone/system/textbuffer.h"
#include <boost/format.hpp>
#include <iostream>

namespace reone {

void TextBuffer::dump() {
    std::cerr << boost::format("TextBuffer @ 0x%p, cursor: %zu\n") % this % _cur
              << str() << '\n';
}

void TextBuffer::seekCur(long offset) {
    if (offset < 0) {
        size_t offsetAbs = -offset;
        _cur = (offsetAbs < _cur) ? (_cur - offsetAbs) : 0;
        return;
    }

    size_t nextCur = _cur + offset;
    _cur = nextCur <= _data.size() ? nextCur : _data.size();
}

void TextBuffer::write(char c) {
    _data.insert(_data.begin() + _cur, c);
    _cur += 1;
}

void TextBuffer::write(std::string_view s) {
    size_t origSize = _data.size();
    size_t inc = s.size();
    _data.resize(_data.size() + inc);

    // Move all characters right to make space for the substring.
    for (size_t i = origSize; i > _cur; --i) {
        size_t index = i - 1;
        _data[index + inc] = _data[index];
    }

    // Insert the substring before the cursor.
    for (size_t i = 0; i < inc; ++i) {
        _data[_cur++] = s[i];
    }
}

char TextBuffer::read() {
    assert(_cur < _data.size() && "out-of-bounds access");
    char c = _data[_cur];
    _cur += 1;
    return c;
}

char TextBuffer::peek() {
    assert(_cur < _data.size() && "out-of-bounds access");
    return _data[_cur];
}

std::string_view TextBuffer::readline() {
    size_t start = tell();
    if (search("\n")) {
        seekCur(1);
    }
    size_t end = tell();
    return std::string_view(&_data[start], end - start);
}

std::string_view TextBuffer::readlineReverse() {
    size_t end = tell();

    if (_data.empty() || end == 0) {
        return std::string_view();
    }

    seekCur(-1);
    if (rsearch("\n")) {
        seekCur(1);
    }
    size_t start = tell();
    return std::string_view(&_data[start], end - start);
}

void TextBuffer::erase() {
    if (_cur == 0) {
        return;
    }
    _cur -= 1;
    auto it = _data.begin() + _cur;
    _data.erase(it);
}

bool TextBuffer::search(std::string_view sub) {
    size_t index = std::string_view(&_data[_cur], _data.size() - _cur).find(sub);
    if (index == std::string_view::npos) {
        _cur = _data.size();
        return false;
    }
    _cur += index;
    return true;
}

bool TextBuffer::rsearch(std::string_view sub) {
    size_t index = std::string_view(&_data[0], _cur).rfind(sub);
    if (index == std::string_view::npos) {
        _cur = 0;
        return false;
    }
    _cur = index;
    return true;
}

} // namespace reone
