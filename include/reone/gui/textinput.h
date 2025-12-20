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

#include "reone/input/event.h"
#include "reone/system/textbuffer.h"

namespace reone {

namespace gui {

struct TextInputFlags {
    static constexpr int digits = 1;
    static constexpr int letters = 2;
    static constexpr int whitespace = 4;
    static constexpr int symbols = 8;

    static constexpr int lettersWhitespace = letters | whitespace;
    static constexpr int console = digits | letters | whitespace | symbols;
};

class TextInput {
public:
    TextInput(TextBuffer &buffer, int mask) :
        _buffer(buffer), _mask(mask) {}

    bool handle(const input::Event &event);

    void setMinOffset(size_t minOffset) { _minOffset = minOffset; }
    void clear();
    void setText(std::string_view text);

private:
    TextBuffer &_buffer;
    int _minOffset {0};
    int _mask {0};

    bool handleKeyDown(const input::KeyEvent &event);
    bool isKeyAllowed(const input::KeyEvent &event) const;

    void insert(char c);
    void backspace();
};

} // namespace gui

} // namespace reone
