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

#include <optional>

namespace reone {

std::vector<uint32_t> codePointsFromUTF8(const std::string &s);
std::string utf8FromCodePoints(const std::vector<uint32_t> codePoints);

std::vector<uint32_t> codePointsFromUTF16(const std::u16string &s);
std::u16string utf16FromCodePoints(const std::vector<uint32_t> &codePoints);

/**
 * Whether \p codePoint is a letter: Latin, with or without accents, or
 * Russian Cyrillic. No code page here holds Cyrillic, so it is never typed.
 */
bool isCodePointAlpha(uint32_t codePoint);

uint32_t codePointToLower(uint32_t codePoint);
uint32_t codePointToUpper(uint32_t codePoint);

/** A single-byte code page the game's text is kept in. */
enum class CodePage {
    /** Central European, the Polish release's. */
    Windows1250,
    /** Western European, the English, French, German, Italian and Spanish releases'. */
    Windows1252
};

/**
 * @return the byte of the code point in \p codePage, or none when the code
 *         page has no such character
 */
std::optional<uint8_t> codePageByteFromCodePoint(uint32_t codePoint, CodePage codePage);

} // namespace reone
