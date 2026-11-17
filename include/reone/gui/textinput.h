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
#include "reone/system/unicodeutil.h"

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

/**
 * Edits a text buffer from the keyboard. The text typed is what the system
 * translates the keys into, one character at a time, kept in the code page
 * set (Windows-1252 unless set); Backspace removes the last character.
 * Control characters and characters the code page lacks are never entered.
 */
class TextInput {
public:
    TextInput(TextBuffer &buffer, int mask) :
        _buffer(buffer), _mask(mask) {}

    /**
     * Enters a typed character, or acts on a key: Backspace, and Control+V
     * or Command+V where pasting is allowed. Every other key that types a
     * character with its modifiers (input::KeyEvent::character) is taken
     * too, though its character arrives as text.
     *
     * @return whether the event was taken
     */
    bool handle(const input::Event &event);

    void setMinOffset(size_t minOffset) { _minOffset = minOffset; }
    /** Characters never entered, whether typed or pasted. */
    void setExcludedCharacters(std::string characters) { _excluded = std::move(characters); }
    /** Whether Control+V or Command+V enters the clipboard's text, as if typed. Off unless set. */
    void setPasteAllowed(bool allowed) { _pasteAllowed = allowed; }
    /** Sets the code page the text is kept in, which typed and pasted characters are entered in. */
    void setCodePage(CodePage codePage) { _codePage = codePage; }
    /** Characters are no longer entered once the text holds this many. Zero sets no limit. */
    void setMaxLength(size_t length) { _maxLength = length; }
    void clear();
    void setText(std::string_view text);

private:
    TextBuffer &_buffer;
    size_t _minOffset {0};
    int _mask {0};
    /** Characters never entered (setExcludedCharacters). */
    std::string _excluded;
    /** The most characters the text holds; zero sets no limit (setMaxLength). */
    size_t _maxLength {0};
    /** The code page the text is kept in (setCodePage). */
    CodePage _codePage {CodePage::Windows1252};
    /** Whether Control+V pastes (setPasteAllowed). */
    bool _pasteAllowed {false};

    bool handleKeyDown(const input::KeyEvent &event);
    /** Enters the typed character when the mask allows its kind. */
    bool handleTextInput(const input::TextInputEvent &event);

    /** Enters the character, unless it is excluded, a control character or past the length limit. */
    void insert(char c);
    void backspace();
};

} // namespace gui

} // namespace reone
