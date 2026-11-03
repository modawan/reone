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

#include <gtest/gtest.h>

#include "reone/input/keyname.h"

using namespace reone::input;

TEST(KeyCodeFromName, should_take_single_characters) {
    EXPECT_EQ(keyCodeFromName("a"), KeyCode::A);
    EXPECT_EQ(keyCodeFromName("A"), KeyCode::A);
    EXPECT_EQ(keyCodeFromName("1"), KeyCode::Key1);
    EXPECT_EQ(keyCodeFromName("`"), KeyCode::Backquote);
}

TEST(KeyCodeFromName, should_take_key_names_in_any_case) {
    EXPECT_EQ(keyCodeFromName("escape"), KeyCode::Escape);
    EXPECT_EQ(keyCodeFromName("Esc"), KeyCode::Escape);
    EXPECT_EQ(keyCodeFromName("RETURN"), KeyCode::Return);
    EXPECT_EQ(keyCodeFromName("enter"), KeyCode::Return);
    EXPECT_EQ(keyCodeFromName("space"), KeyCode::Space);
    EXPECT_EQ(keyCodeFromName("up"), KeyCode::Up);
    EXPECT_EQ(keyCodeFromName("f1"), KeyCode::F1);
    EXPECT_EQ(keyCodeFromName("F12"), KeyCode::F12);
    EXPECT_EQ(keyCodeFromName("rctrl"), KeyCode::RightControl);
    EXPECT_EQ(keyCodeFromName("ralt"), KeyCode::RightAlt);
}

TEST(KeyCodeFromName, should_reject_anything_else) {
    EXPECT_FALSE(keyCodeFromName("").has_value());
    EXPECT_FALSE(keyCodeFromName(" ").has_value());
    EXPECT_FALSE(keyCodeFromName("f13").has_value());
    EXPECT_FALSE(keyCodeFromName("escapes").has_value());
}
