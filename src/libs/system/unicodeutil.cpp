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

#include "reone/system/unicodeutil.h"

#include <array>
#include <optional>

#include "reone/system/checkutil.h"

namespace reone {

std::vector<uint32_t> codePointsFromUTF8(const std::string &s) {
    std::vector<uint32_t> result;
    auto bytes = reinterpret_cast<const uint8_t *>(&s[0]);
    for (size_t i = 0; i < s.length();) {
        uint32_t cp;
        auto b1 = bytes[i];
        if ((b1 & 0x80) == 0) {
            // 1-byte code point
            cp = b1 & 0x7f;
            ++i;
        } else if ((b1 & 0xe0) == 0xc0) {
            // 2-byte code point
            checkGreater("length", s.length(), i + 1);
            auto b2 = bytes[i + 1];
            checkThat((b2 & 0xc0) == 0x80, "second byte must begin with bits 10");
            cp = (b1 & 0x1f) << 6;
            cp |= b2 & 0x3f;
            i += 2;
        } else if ((b1 & 0xf0) == 0xe0) {
            // 3-byte code point
            checkGreater("length", s.length(), i + 2);
            auto b2 = bytes[i + 1];
            checkThat((b2 & 0xc0) == 0x80, "second byte must begin with bits 10");
            auto b3 = bytes[i + 2];
            checkThat((b3 & 0xc0) == 0x80, "third byte must begin with bits 10");
            cp = (b1 & 0x0f) << 12;
            cp |= (b2 & 0x3f) << 6;
            cp |= b3 & 0x3f;
            i += 3;
        } else if ((b1 & 0xf8) == 0xf0) {
            // 4-byte code point
            checkGreater("length", s.length(), i + 3);
            auto b2 = bytes[i + 1];
            checkThat((b2 & 0xc0) == 0x80, "second byte must begin with bits 10");
            auto b3 = bytes[i + 2];
            checkThat((b3 & 0xc0) == 0x80, "third byte must begin with bits 10");
            auto b4 = bytes[i + 3];
            checkThat((b4 & 0xc0) == 0x80, "fourth byte must begin with bits 10");
            cp = (b1 & 0x07) << 18;
            cp |= (b2 & 0x3f) << 12;
            cp |= (b3 & 0x3f) << 6;
            cp |= b4 & 0x3f;
            i += 4;
        } else {
            throw std::invalid_argument(str(boost::format("Unexpected start of UTF-8 code point at %d: %02x") % i % b1));
        }
        result.emplace_back(cp);
    }
    return result;
}

std::string utf8FromCodePoints(const std::vector<uint32_t> codePoints) {
    std::ostringstream result;
    for (size_t i = 0; i < codePoints.size(); ++i) {
        const auto &cp = codePoints[i];
        if (cp <= 0x7f) {
            result.put(cp);
        } else if (cp <= 0x7ff) {
            auto b1 = ((cp & 0x7c0) >> 6) | 0xc0;
            auto b2 = (cp & 0x3f) | 0x80;
            result.put(b1);
            result.put(b2);
        } else if (cp <= 0xffff) {
            auto b1 = ((cp & 0xf000) >> 12) | 0xe0;
            auto b2 = ((cp & 0xfc0) >> 6) | 0x80;
            auto b3 = (cp & 0x3f) | 0x80;
            result.put(b1);
            result.put(b2);
            result.put(b3);
        } else if (cp <= 0x10ffff) {
            auto b1 = ((cp & 0x1c0000) >> 18) | 0xf0;
            auto b2 = ((cp & 0x3f000) >> 12) | 0x80;
            auto b3 = ((cp & 0xfc0) >> 6) | 0x80;
            auto b4 = (cp & 0x3f) | 0x80;
            result.put(b1);
            result.put(b2);
            result.put(b3);
            result.put(b4);
        } else {
            throw std::invalid_argument(str(boost::format("Code point out of range at %d: %08x") % i % cp));
        }
    }
    return result.str();
}

std::vector<uint32_t> codePointsFromUTF16(const std::u16string &s) {
    std::vector<uint32_t> result;
    auto units = reinterpret_cast<const uint16_t *>(&s[0]);
    for (size_t i = 0; i < s.length();) {
        auto &unit1 = units[i];
        if ((unit1 & 0xfc00) == 0xd800) {
            // surrogate pair
            checkGreater("length", s.length(), i + 1);
            auto &unit2 = units[i + 1];
            checkThat((unit2 & 0xfc00) == 0xdc00, "second code unit must begin with bits 110111");
            auto codePoint = (unit1 & 0x3ff) << 10;
            codePoint |= unit2 & 0x3ff;
            codePoint += 0x10000;
            result.emplace_back(codePoint);
            i += 2;
        } else if (unit1 <= 0xd7ff || (0xe000 <= unit1 && unit1 <= 0xffff)) {
            // single code
            result.emplace_back(unit1);
            ++i;
        } else {
            throw std::invalid_argument(str(boost::format("Unsupported UTF-16 code unit at %d: %04x") % i % unit1));
        }
    }
    return std::move(result);
}

std::u16string utf16FromCodePoints(const std::vector<uint32_t> &codePoints) {
    std::basic_ostringstream<char16_t> result;
    for (size_t i = 0; i < codePoints.size(); ++i) {
        auto &cp = codePoints[i];
        if (cp <= 0xd7ff || (0xe000 <= cp && cp <= 0xffff)) {
            result.put(cp);
        } else if (cp >= 0x10000 && cp <= 0x10ffff) {
            auto cp2 = cp - 0x10000;
            auto unit1 = (cp2 >> 10) | 0xd800;
            auto unit2 = (cp2 & 0x3ff) | 0xdc00;
            result.put(unit1);
            result.put(unit2);
        } else {
            throw std::invalid_argument(str(boost::format("Unsupported UTF-16 code point at %d: %08x") % i % cp));
        }
    }
    return result.str();
}

bool isCodePointAlpha(uint32_t codePoint) {
    if (codePoint >= 0x41 && codePoint <= 0x5a) {
        // Capital Latin
        return true;
    }
    if (codePoint >= 0x61 && codePoint <= 0x7a) {
        // Small Latin
        return true;
    }
    if (codePoint >= 0xc0 && codePoint <= 0x024f && codePoint != 0xd7 && codePoint != 0xf7) {
        // Latin with accents: Latin-1's letters, Extended-A and Extended-B
        return true;
    }
    if ((codePoint >= 0x0410 && codePoint <= 0x044f) ||
        codePoint == 0x0401 || codePoint == 0x0451) {
        // Cyrillic
        return true;
    }
    return false;
}

// Latin Extended-A pairs its capitals and small letters side by side: in
// these runs the capital is the even code point and the small letter the
// next one...
static bool isLatinExtendedEvenCapitalRun(uint32_t codePoint) {
    return (codePoint >= 0x0100 && codePoint <= 0x012f) ||
           (codePoint >= 0x0132 && codePoint <= 0x0137) ||
           (codePoint >= 0x014a && codePoint <= 0x0177);
}

// ...and in these the capital is the odd one.
static bool isLatinExtendedOddCapitalRun(uint32_t codePoint) {
    return (codePoint >= 0x0139 && codePoint <= 0x0148) ||
           (codePoint >= 0x0179 && codePoint <= 0x017e);
}

uint32_t codePointToLower(uint32_t codePoint) {
    if (codePoint >= 0x41 && codePoint <= 0x5a) {
        // Latin
        return codePoint + 0x20;
    }
    if (codePoint >= 0xc0 && codePoint <= 0xde && codePoint != 0xd7) {
        // Latin-1
        return codePoint + 0x20;
    }
    if ((isLatinExtendedEvenCapitalRun(codePoint) && codePoint % 2 == 0) ||
        (isLatinExtendedOddCapitalRun(codePoint) && codePoint % 2 == 1)) {
        // Latin Extended-A
        return codePoint + 1;
    }
    if (codePoint == 0x0178) {
        // Y with diaeresis
        return 0xff;
    }
    if (codePoint >= 0x410 && codePoint <= 0x42f) {
        // Cyrillic
        return codePoint + 0x20;
    }
    if (codePoint == 0x0401) {
        // Cyrillic Io
        return 0x0451;
    }
    return codePoint;
}

// Each code page holds ASCII below 0x80; above it, these characters, from
// 0x80 on, in order. 0 marks a byte the code page leaves unused.
static constexpr std::array<uint32_t, 128> kWindows1250High {
    0x20ac, 0x0000, 0x201a, 0x0000, 0x201e, 0x2026, 0x2020, 0x2021,
    0x0000, 0x2030, 0x0160, 0x2039, 0x015a, 0x0164, 0x017d, 0x0179,
    0x0000, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x0000, 0x2122, 0x0161, 0x203a, 0x015b, 0x0165, 0x017e, 0x017a,
    0x00a0, 0x02c7, 0x02d8, 0x0141, 0x00a4, 0x0104, 0x00a6, 0x00a7,
    0x00a8, 0x00a9, 0x015e, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x017b,
    0x00b0, 0x00b1, 0x02db, 0x0142, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
    0x00b8, 0x0105, 0x015f, 0x00bb, 0x013d, 0x02dd, 0x013e, 0x017c,
    0x0154, 0x00c1, 0x00c2, 0x0102, 0x00c4, 0x0139, 0x0106, 0x00c7,
    0x010c, 0x00c9, 0x0118, 0x00cb, 0x011a, 0x00cd, 0x00ce, 0x010e,
    0x0110, 0x0143, 0x0147, 0x00d3, 0x00d4, 0x0150, 0x00d6, 0x00d7,
    0x0158, 0x016e, 0x00da, 0x0170, 0x00dc, 0x00dd, 0x0162, 0x00df,
    0x0155, 0x00e1, 0x00e2, 0x0103, 0x00e4, 0x013a, 0x0107, 0x00e7,
    0x010d, 0x00e9, 0x0119, 0x00eb, 0x011b, 0x00ed, 0x00ee, 0x010f,
    0x0111, 0x0144, 0x0148, 0x00f3, 0x00f4, 0x0151, 0x00f6, 0x00f7,
    0x0159, 0x016f, 0x00fa, 0x0171, 0x00fc, 0x00fd, 0x0163, 0x02d9};

// Windows-1252 is Latin-1 but for 0x80 to 0x9f.
static constexpr std::array<uint32_t, 32> kWindows1252Controls {
    0x20ac, 0x0000, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017d, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x0000, 0x017e, 0x0178};

template <size_t N>
static std::optional<uint8_t> findHighByte(const std::array<uint32_t, N> &high, uint32_t codePoint) {
    for (size_t i = 0; i < high.size(); ++i) {
        if (high[i] != 0 && high[i] == codePoint) {
            return static_cast<uint8_t>(0x80 + i);
        }
    }
    return std::nullopt;
}

std::optional<uint8_t> codePageByteFromCodePoint(uint32_t codePoint, CodePage codePage) {
    if (codePoint < 0x80) {
        return static_cast<uint8_t>(codePoint);
    }
    switch (codePage) {
    case CodePage::Windows1250:
        return findHighByte(kWindows1250High, codePoint);
    case CodePage::Windows1252:
    default:
        if (codePoint >= 0xa0 && codePoint <= 0xff) {
            return static_cast<uint8_t>(codePoint);
        }
        return findHighByte(kWindows1252Controls, codePoint);
    }
}

uint32_t codePointToUpper(uint32_t codePoint) {
    if (codePoint >= 0x61 && codePoint <= 0x7a) {
        // Latin
        return codePoint - 0x20;
    }
    if (codePoint >= 0xe0 && codePoint <= 0xfe && codePoint != 0xf7) {
        // Latin-1
        return codePoint - 0x20;
    }
    if (codePoint == 0xff) {
        // y with diaeresis
        return 0x0178;
    }
    if ((isLatinExtendedEvenCapitalRun(codePoint) && codePoint % 2 == 1) ||
        (isLatinExtendedOddCapitalRun(codePoint) && codePoint % 2 == 0)) {
        // Latin Extended-A
        return codePoint - 1;
    }
    if (codePoint >= 0x430 && codePoint <= 0x44f) {
        // Cyrillic
        return codePoint - 0x20;
    }
    if (codePoint == 0x0451) {
        // Cyrillic Io
        return 0x0401;
    }
    return codePoint;
}

} // namespace reone
