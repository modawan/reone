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

#include <array>
#include <cctype>
#include <optional>
#include <string_view>

#include "reone/input/event.h"

#include "types.h"

namespace reone {

namespace game {

/**
 * The in-game menu's keys, as KotOR's keymap.2da binds them: the page keys
 * (rows action209 to action216: U, I, P, K, J, L, M, O) open a page from the
 * game and switch to it inside the menu; "GUI" (action223, Escape) and the
 * previous and next page keys (action243 and action244, Q and E) act inside
 * the menu only.
 */
enum class MenuKeyCommand {
    Open,
    Close,
    Previous,
    Next
};

struct MenuKey {
    MenuKeyCommand command {MenuKeyCommand::Open};
    InGameMenuTab tab {InGameMenuTab::None};
};

static constexpr int kMenuInputEventFirstPage = 209;
static constexpr int kMenuInputEventLastPage = 216;
static constexpr int kMenuInputEventClose = 223;
static constexpr int kMenuInputEventPrevious = 243;
static constexpr int kMenuInputEventNext = 244;

/** The keymap.2da input events that the in-game menu answers to. */
static constexpr std::array<int, 11> kMenuInputEvents {
    209, 210, 211, 212, 213, 214, 215, 216,
    kMenuInputEventClose, kMenuInputEventPrevious, kMenuInputEventNext};

/**
 * KotOR's pages in the order of the menu's top bar, which is the order of
 * their keymap.2da rows. Previous and next page walk it.
 */
static constexpr std::array<InGameMenuTab, 8> kMenuPagesK1 {
    InGameMenuTab::Equipment,
    InGameMenuTab::Inventory,
    InGameMenuTab::Character,
    InGameMenuTab::Abilities,
    InGameMenuTab::Messages,
    InGameMenuTab::Journal,
    InGameMenuTab::Map,
    InGameMenuTab::Options};

/** What a keymap.2da input event does in KotOR's in-game menu, if anything. */
inline std::optional<MenuKey> menuKeyForInputEvent(int event) {
    if (event >= kMenuInputEventFirstPage && event <= kMenuInputEventLastPage) {
        return MenuKey {MenuKeyCommand::Open, kMenuPagesK1[event - kMenuInputEventFirstPage]};
    }
    switch (event) {
    case kMenuInputEventClose:
        return MenuKey {MenuKeyCommand::Close};
    case kMenuInputEventPrevious:
        return MenuKey {MenuKeyCommand::Previous};
    case kMenuInputEventNext:
        return MenuKey {MenuKeyCommand::Next};
    default:
        return std::nullopt;
    }
}

/**
 * The key a bindablekeys.2da name stands for, for the names a menu key can
 * have: KEYBOARD_ and a letter or digit, KEYBOARD_ESC, and KEYBOARD_F1 to
 * KEYBOARD_F12. Any other name gives nothing.
 */
inline std::optional<input::KeyCode> keyCodeFromBindableKeyName(std::string_view name) {
    static constexpr std::string_view kPrefix {"KEYBOARD_"};
    if (name.substr(0, kPrefix.size()) != kPrefix) {
        return std::nullopt;
    }
    const std::string_view key {name.substr(kPrefix.size())};
    if (key.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(key.front());
        if (std::isalpha(c)) {
            return static_cast<input::KeyCode>(std::tolower(c));
        }
        if (std::isdigit(c)) {
            return static_cast<input::KeyCode>(c);
        }
        return std::nullopt;
    }
    if (key == "ESC") {
        return input::KeyCode::Escape;
    }
    static constexpr std::array<input::KeyCode, 12> kFunctionKeys {
        input::KeyCode::F1, input::KeyCode::F2, input::KeyCode::F3, input::KeyCode::F4,
        input::KeyCode::F5, input::KeyCode::F6, input::KeyCode::F7, input::KeyCode::F8,
        input::KeyCode::F9, input::KeyCode::F10, input::KeyCode::F11, input::KeyCode::F12};
    if (key.size() >= 2 && key.size() <= 3 && key.front() == 'F') {
        int number = 0;
        for (char c : key.substr(1)) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return std::nullopt;
            }
            number = number * 10 + (c - '0');
        }
        if (number >= 1 && number <= 12) {
            return kFunctionKeys[number - 1];
        }
    }
    return std::nullopt;
}

/**
 * The page a step of previous (-1) or next (+1) leads to from the current
 * one, skipping pages that cannot be shown and going round from the last to
 * the first. From no page, next is the first page and previous the last.
 * When no page can be shown, it is the current one.
 */
template <class Available>
InGameMenuTab adjacentMenuPage(InGameMenuTab current, int step, Available available) {
    const int count = static_cast<int>(kMenuPagesK1.size());
    int index = -1;
    for (int i = 0; i < count; ++i) {
        if (kMenuPagesK1[i] == current) {
            index = i;
        }
    }
    if (index == -1) {
        index = step > 0 ? count - 1 : 0;
    }
    for (int i = 1; i <= count; ++i) {
        const InGameMenuTab page = kMenuPagesK1[((index + step * i) % count + count) % count];
        if (available(page)) {
            return page;
        }
    }
    return current;
}

} // namespace game

} // namespace reone
