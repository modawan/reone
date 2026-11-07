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

#include <set>

#include "reone/game/menukeys.h"

using namespace reone;
using namespace reone::game;

// KotOR's keymap.2da rows action209 to action216 are Equip (U), Inventory
// (I), Character (P), SkillsAndFeats (K), Messages (J), Quests (L), Map (M)
// and Options (O), in the order of the menu's top bar.
TEST(MenuKeys, page_events_open_the_pages_of_the_top_bar_in_order) {
    const InGameMenuTab expected[] {
        InGameMenuTab::Equipment, InGameMenuTab::Inventory, InGameMenuTab::Character, InGameMenuTab::Abilities,
        InGameMenuTab::Messages, InGameMenuTab::Journal, InGameMenuTab::Map, InGameMenuTab::Options};
    for (int event = 209; event <= 216; ++event) {
        auto key = menuKeyForInputEvent(event);
        ASSERT_TRUE(key.has_value()) << event;
        EXPECT_EQ(MenuKeyCommand::Open, key->command) << event;
        EXPECT_EQ(expected[event - 209], key->tab) << event;
    }
}

// action223 is "GUI" on Escape; action243 and action244 are PrevMenu (Q) and
// NextMenu (E), bound in the GUI only.
TEST(MenuKeys, escape_and_the_previous_and_next_page_events_act_inside_the_menu) {
    EXPECT_EQ(MenuKeyCommand::Close, menuKeyForInputEvent(223)->command);
    EXPECT_EQ(MenuKeyCommand::Previous, menuKeyForInputEvent(243)->command);
    EXPECT_EQ(MenuKeyCommand::Next, menuKeyForInputEvent(244)->command);
}

// Quicksave (218), Pause (224, 241), the Q and E of selection (204, 205)
// and the dialogue numbers (254...) are not the menu's.
TEST(MenuKeys, other_events_are_not_menu_keys) {
    for (int event : {0, 200, 204, 205, 206, 208, 217, 218, 222, 224, 241, 242, 245, 254, 263, 280}) {
        EXPECT_FALSE(menuKeyForInputEvent(event).has_value()) << event;
    }
}

TEST(MenuKeys, every_menu_event_is_listed_once) {
    std::set<int> events(kMenuInputEvents.begin(), kMenuInputEvents.end());
    EXPECT_EQ(kMenuInputEvents.size(), events.size());
    for (int event : kMenuInputEvents) {
        EXPECT_TRUE(menuKeyForInputEvent(event).has_value()) << event;
    }
}

// The names are bindablekeys.2da's keyname column.
TEST(MenuKeys, bindable_key_names_resolve_to_keys) {
    EXPECT_EQ(input::KeyCode::U, keyCodeFromBindableKeyName("KEYBOARD_U"));
    EXPECT_EQ(input::KeyCode::I, keyCodeFromBindableKeyName("KEYBOARD_I"));
    EXPECT_EQ(input::KeyCode::Q, keyCodeFromBindableKeyName("KEYBOARD_Q"));
    EXPECT_EQ(input::KeyCode::Key1, keyCodeFromBindableKeyName("KEYBOARD_1"));
    EXPECT_EQ(input::KeyCode::Escape, keyCodeFromBindableKeyName("KEYBOARD_ESC"));
    EXPECT_EQ(input::KeyCode::F4, keyCodeFromBindableKeyName("KEYBOARD_F4"));
    EXPECT_EQ(input::KeyCode::F12, keyCodeFromBindableKeyName("KEYBOARD_F12"));
}

TEST(MenuKeys, other_names_resolve_to_nothing) {
    for (const char *name : {"", "KEYBOARD_", "KEYBOARD_F0", "KEYBOARD_F13", "KEYBOARD_FX", "KEYBOARD_NUMPAD1",
                             "KEYBOARD_LEFT_ARROW", "KEYBOARD_.", "MOUSE_LEFT", "U", "keyboard_u"}) {
        EXPECT_FALSE(keyCodeFromBindableKeyName(name).has_value()) << name;
    }
}

TEST(MenuKeys, next_and_previous_walk_the_top_bar_and_go_round) {
    const auto all = [](InGameMenuTab) { return true; };
    EXPECT_EQ(InGameMenuTab::Inventory, adjacentMenuPage(InGameMenuTab::Equipment, 1, all));
    EXPECT_EQ(InGameMenuTab::Options, adjacentMenuPage(InGameMenuTab::Map, 1, all));
    EXPECT_EQ(InGameMenuTab::Equipment, adjacentMenuPage(InGameMenuTab::Options, 1, all));
    EXPECT_EQ(InGameMenuTab::Options, adjacentMenuPage(InGameMenuTab::Equipment, -1, all));
    EXPECT_EQ(InGameMenuTab::Messages, adjacentMenuPage(InGameMenuTab::Journal, -1, all));
}

TEST(MenuKeys, next_and_previous_skip_pages_that_cannot_be_shown) {
    const auto noAbilitiesOrMessages = [](InGameMenuTab tab) {
        return tab != InGameMenuTab::Abilities && tab != InGameMenuTab::Messages;
    };
    EXPECT_EQ(InGameMenuTab::Journal, adjacentMenuPage(InGameMenuTab::Character, 1, noAbilitiesOrMessages));
    EXPECT_EQ(InGameMenuTab::Character, adjacentMenuPage(InGameMenuTab::Journal, -1, noAbilitiesOrMessages));
}

TEST(MenuKeys, from_no_page_next_is_the_first_and_previous_the_last) {
    const auto all = [](InGameMenuTab) { return true; };
    EXPECT_EQ(InGameMenuTab::Equipment, adjacentMenuPage(InGameMenuTab::None, 1, all));
    EXPECT_EQ(InGameMenuTab::Options, adjacentMenuPage(InGameMenuTab::None, -1, all));
}

TEST(MenuKeys, with_no_page_to_show_the_page_stays) {
    const auto none = [](InGameMenuTab) { return false; };
    EXPECT_EQ(InGameMenuTab::Map, adjacentMenuPage(InGameMenuTab::Map, 1, none));
    EXPECT_EQ(InGameMenuTab::None, adjacentMenuPage(InGameMenuTab::None, -1, none));
}
