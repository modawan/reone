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

namespace reone {

namespace gui {

bool TextInput::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    default:
        return false;
    }
}

static inline bool isDigitKey(const input::KeyEvent &event) {
    return event.code >= input::KeyCode::Key0 && event.code <= input::KeyCode::Key9;
}

static inline bool isLetterKey(const input::KeyEvent &event) {
    return event.code >= input::KeyCode::A && event.code <= input::KeyCode::Z;
}

static inline bool isSymbolKey(const input::KeyEvent &event) {
    return event.code == input::KeyCode::Minus ||
           event.code == input::KeyCode::Equals ||
           event.code == input::KeyCode::LeftBracket ||
           event.code == input::KeyCode::RightBracket ||
           event.code == input::KeyCode::Semicolon ||
           event.code == input::KeyCode::Quote ||
           event.code == input::KeyCode::Comma ||
           event.code == input::KeyCode::Period ||
           event.code == input::KeyCode::Slash ||
           event.code == input::KeyCode::Backslash;
}

static inline bool isShiftPressed(const input::KeyEvent &event) {
    return event.mod & input::KeyModifiers::shift;
}

bool TextInput::handleKeyDown(const input::KeyEvent &event) {
    if (!isKeyAllowed(event)) {
        return false;
    }

    bool digit = isDigitKey(event);
    bool letter = isLetterKey(event);
    bool symbol = isSymbolKey(event);
    bool shift = isShiftPressed(event);

    if (event.code == input::KeyCode::Backspace) {
        backspace();
    } else if (event.code == input::KeyCode::Space) {
        insert(static_cast<char>(event.code));
    } else if (digit) {
        if (shift) {
            if (event.code == input::KeyCode::Key1) {
                insert('!');
            } else if (event.code == input::KeyCode::Key2) {
                insert('@');
            } else if (event.code == input::KeyCode::Key3) {
                insert('#');
            } else if (event.code == input::KeyCode::Key4) {
                insert('$');
            } else if (event.code == input::KeyCode::Key5) {
                insert('%');
            } else if (event.code == input::KeyCode::Key6) {
                insert('^');
            } else if (event.code == input::KeyCode::Key7) {
                insert('&');
            } else if (event.code == input::KeyCode::Key8) {
                insert('*');
            } else if (event.code == input::KeyCode::Key9) {
                insert('(');
            } else if (event.code == input::KeyCode::Key0) {
                insert(')');
            }
        } else {
            insert(static_cast<char>(event.code));
        }
    } else if (letter) {
        insert(shift ? toupper(static_cast<char>(event.code)) : static_cast<char>(event.code));
    } else if (symbol) {
        if (shift) {
            if (event.code == input::KeyCode::Minus) {
                insert('_');
            } else if (event.code == input::KeyCode::Equals) {
                insert('+');
            } else if (event.code == input::KeyCode::LeftBracket) {
                insert('{');
            } else if (event.code == input::KeyCode::RightBracket) {
                insert('}');
            } else if (event.code == input::KeyCode::Semicolon) {
                insert(':');
            } else if (event.code == input::KeyCode::Quote) {
                insert('\"');
            } else if (event.code == input::KeyCode::Comma) {
                insert('<');
            } else if (event.code == input::KeyCode::Period) {
                insert('>');
            } else if (event.code == input::KeyCode::Slash) {
                insert('?');
            } else if (event.code == input::KeyCode::Backslash) {
                insert('|');
            }
        } else {
            insert(static_cast<char>(event.code));
        }
    }

    return true;
}

bool TextInput::isKeyAllowed(const input::KeyEvent &event) const {
    if (event.code == input::KeyCode::Backspace) {
        return true;
    }
    if (event.code == input::KeyCode::Space) {
        return (_mask & TextInputFlags::whitespace) != 0;
    }
    if (isDigitKey(event)) {
        return (_mask & TextInputFlags::digits) != 0;
    }
    if (isLetterKey(event)) {
        return (_mask & TextInputFlags::letters) != 0;
    }
    if (isSymbolKey(event)) {
        return (_mask & TextInputFlags::symbols) != 0;
    }
    return false;
}

void TextInput::insert(char c) {
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
