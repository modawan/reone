/*
 * Copyright (c) 2026 The reone project contributors
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

#include <gtest/gtest.h>

#include "reone/gui/textinput.h"

using namespace reone;
using namespace reone::gui;
using namespace reone::input;

TEST(TextInput, should_enter_only_the_typed_characters_its_mask_and_code_page_allow) {
    // given
    TextBuffer buffer;
    TextInput input(buffer, TextInputFlags::letters);
    input.setCodePage(CodePage::Windows1250);

    // when
    for (uint32_t codePoint : std::vector<uint32_t> {'a', '1', ' ', 0x142, 0x416}) {
        input.handle(Event::newTextInput(TextInputEvent {codePoint}));
    }

    // then
    EXPECT_EQ("a\xb3", buffer.str());
}

TEST(TextInput, should_take_keys_that_type_and_keep_to_its_length) {
    // given
    TextBuffer buffer;
    TextInput input(buffer, TextInputFlags::console);
    input.setMaxLength(2);

    // when
    bool typingKeyTaken = input.handle(Event::newKeyDown(KeyEvent {true, KeyCode::A, 0, false, 'a'}));
    bool shortcutTaken = input.handle(Event::newKeyDown(KeyEvent {true, KeyCode::A, KeyModifiers::leftControl, false}));
    for (uint32_t codePoint : std::vector<uint32_t> {'a', 'b', 'c'}) {
        input.handle(Event::newTextInput(TextInputEvent {codePoint}));
    }
    input.handle(Event::newKeyDown(KeyEvent {true, KeyCode::Backspace, 0, false}));

    // then
    EXPECT_TRUE(typingKeyTaken);
    EXPECT_FALSE(shortcutTaken);
    EXPECT_EQ("a", buffer.str());
}
