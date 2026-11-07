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
#include <utility>

#include "event.h"

namespace reone {

namespace input {

/**
 * The key a scripted "key" command names: a single printable character
 * (letters case-insensitively), or a key name such as "escape", "return",
 * "space", "up" or "f1". Empty for anything else.
 */
inline std::optional<KeyCode> keyCodeFromName(std::string_view name) {
    if (name.size() == 1) {
        unsigned char c = static_cast<unsigned char>(name.front());
        if (std::isalpha(c)) {
            return static_cast<KeyCode>(std::tolower(c));
        }
        if (std::isgraph(c)) {
            return static_cast<KeyCode>(c);
        }
        return std::nullopt;
    }
    static constexpr std::array<std::pair<std::string_view, KeyCode>, 34> kNamed {{
        {"return", KeyCode::Return},
        {"enter", KeyCode::Return},
        {"escape", KeyCode::Escape},
        {"esc", KeyCode::Escape},
        {"space", KeyCode::Space},
        {"tab", KeyCode::Tab},
        {"backspace", KeyCode::Backspace},
        {"delete", KeyCode::Delete},
        {"up", KeyCode::Up},
        {"down", KeyCode::Down},
        {"left", KeyCode::Left},
        {"right", KeyCode::Right},
        {"pageup", KeyCode::PageUp},
        {"pagedown", KeyCode::PageDown},
        {"home", KeyCode::Home},
        {"end", KeyCode::End},
        {"lshift", KeyCode::LeftShift},
        {"rshift", KeyCode::RightShift},
        {"lctrl", KeyCode::LeftControl},
        {"rctrl", KeyCode::RightControl},
        {"lalt", KeyCode::LeftAlt},
        {"ralt", KeyCode::RightAlt},
        {"f1", KeyCode::F1},
        {"f2", KeyCode::F2},
        {"f3", KeyCode::F3},
        {"f4", KeyCode::F4},
        {"f5", KeyCode::F5},
        {"f6", KeyCode::F6},
        {"f7", KeyCode::F7},
        {"f8", KeyCode::F8},
        {"f9", KeyCode::F9},
        {"f10", KeyCode::F10},
        {"f11", KeyCode::F11},
        {"f12", KeyCode::F12},
    }};
    for (const auto &[keyName, code] : kNamed) {
        if (name.size() == keyName.size()) {
            bool same = true;
            for (size_t i = 0; i < name.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(name[i])) != keyName[i]) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return code;
            }
        }
    }
    return std::nullopt;
}

} // namespace input

} // namespace reone
