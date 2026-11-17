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

#include "reone/gui/textinput.h"
#include "reone/system/clipboard.h"
#include "reone/system/unicodeutil.h"

namespace reone {

namespace gui {

bool TextInput::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    case input::EventType::TextInput:
        return handleTextInput(event.text);
    default:
        return false;
    }
}

// Pasting takes Control, or Command where the system pastes with it.
static inline bool isPasteModifierPressed(const input::KeyEvent &event) {
    return (event.mod & (input::KeyModifiers::control | input::KeyModifiers::gui)) != 0;
}

bool TextInput::handleKeyDown(const input::KeyEvent &event) {
    if (event.code == input::KeyCode::Backspace) {
        backspace();
        return true;
    }
    if (_pasteAllowed && isPasteModifierPressed(event) && event.code == input::KeyCode::V) {
        if (!event.repeat) {
            // The clipboard holds UTF-8; each of its characters is entered as
            // one typed.
            for (uint32_t codePoint : codePointsFromUTF8(std::string(getClipboard()->str()))) {
                handleTextInput(input::TextInputEvent {codePoint});
            }
        }
        return true;
    }
    // A key that types is taken whatever the mask, so that nothing else acts
    // on it while text is typed; its character arrives as text, and the mask
    // decides whether it is entered.
    return event.character != 0;
}

bool TextInput::handleTextInput(const input::TextInputEvent &event) {
    auto byte = codePageByteFromCodePoint(event.codePoint, _codePage);
    if (!byte) {
        return true;
    }
    char c = static_cast<char>(*byte);
    int kind = TextInputFlags::symbols;
    if (c == ' ') {
        kind = TextInputFlags::whitespace;
    } else if (c >= '0' && c <= '9') {
        kind = TextInputFlags::digits;
    } else if (isCodePointAlpha(event.codePoint)) {
        kind = TextInputFlags::letters;
    }
    if ((_mask & kind) != 0) {
        insert(c);
    }
    return true;
}

void TextInput::insert(char c) {
    // Control characters and Delete are never entered.
    const auto code = static_cast<unsigned char>(c);
    if (code < 0x20 || code == 0x7f) {
        return;
    }
    if (_excluded.find(c) != std::string::npos) {
        return;
    }
    if (_maxLength > 0) {
        const size_t size = _buffer.empty() ? 0 : _buffer.str().size();
        if (size >= _minOffset && size - _minOffset >= _maxLength) {
            return;
        }
    }
    _buffer.write(c);
}

void TextInput::backspace() {
    if (_buffer.tell() == _minOffset) {
        return;
    }
    _buffer.erase();
}

void TextInput::clear() {
    _buffer.seekEnd(0);
    while (_buffer.tell() != _minOffset) {
        _buffer.erase();
    }
}

void TextInput::setText(std::string_view text) {
    clear();
    _buffer.write(text);
}

} // namespace gui

} // namespace reone
