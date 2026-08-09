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

#include "reone/graphics/textutil.h"

#include "reone/graphics/font.h"

#include <cctype>
#include <string_view>

namespace reone {

namespace graphics {

static float measureCharacter(Font &font, char ch) {
    return font.measure(std::string_view(&ch, 1));
}

static bool isAlphabetic(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

std::vector<std::string> breakText(const std::string &text, Font &font, int maxWidth) {
    std::vector<std::string> result;
    if (text.empty()) {
        return result;
    }

    size_t lineStart = 0;
    while (lineStart < text.size()) {
        if (text[lineStart] == '\n') {
            result.emplace_back();
            ++lineStart;
            continue;
        }

        float width = 0.0f;
        size_t breakAt = std::string::npos;
        size_t cursor = lineStart;
        bool wrapped = false;

        while (cursor < text.size() && text[cursor] != '\n') {
            char ch = text[cursor];
            width += measureCharacter(font, ch);

            if (ch == ' ' && cursor > lineStart) {
                breakAt = cursor;
            } else if (ch == '-' &&
                       cursor > lineStart &&
                       cursor + 1 < text.size() &&
                       isAlphabetic(text[cursor - 1]) &&
                       isAlphabetic(text[cursor + 1])) {
                breakAt = cursor;
            }

            if (width >= static_cast<float>(maxWidth)) {
                size_t lineEnd;
                size_t nextLineStart;
                if (breakAt != std::string::npos) {
                    bool breakAfter = text[breakAt] == '-';
                    lineEnd = breakAt + static_cast<size_t>(breakAfter);
                    nextLineStart = breakAt + 1;
                } else if (cursor > lineStart) {
                    lineEnd = cursor;
                    nextLineStart = cursor;
                } else {
                    lineEnd = cursor + 1;
                    nextLineStart = cursor + 1;
                }

                result.emplace_back(text.substr(lineStart, lineEnd - lineStart));
                lineStart = nextLineStart;
                wrapped = true;
                break;
            }
            ++cursor;
        }

        if (wrapped) {
            continue;
        }

        result.emplace_back(text.substr(lineStart, cursor - lineStart));
        if (cursor == text.size()) {
            lineStart = cursor;
        } else {
            lineStart = cursor + 1;
        }
    }

    return result;
}

} // namespace graphics

} // namespace reone
