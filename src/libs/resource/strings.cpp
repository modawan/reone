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

#include "reone/resource/strings.h"

#include <fstream>

#include "reone/resource/exception/notfound.h"
#include "reone/resource/format/tlkreader.h"
#include "reone/resource/talktable.h"
#include "reone/system/fileutil.h"
#include "reone/system/stream/fileinput.h"

namespace reone {

namespace resource {

// The text a line of a custom strings file holds, in \p codePage. A UTF-8
// line, with or without a byte order mark, is taken character by character,
// and a character the code page lacks is dropped; any other line is taken
// as already in the code page. Each backslash followed by n becomes a line
// break, and the breaks and returns ending the line are dropped.
static std::string customString(const std::string &line, CodePage codePage) {
    static constexpr std::string_view kByteOrderMark {"\xef\xbb\xbf"};
    std::string raw {line.substr(0, line.find('\0'))};
    if (raw.compare(0, kByteOrderMark.size(), kByteOrderMark) == 0) {
        raw.erase(0, kByteOrderMark.size());
    }
    std::string text;
    try {
        for (uint32_t codePoint : codePointsFromUTF8(raw)) {
            if (auto byte = codePageByteFromCodePoint(codePoint, codePage)) {
                text.push_back(static_cast<char>(*byte));
            }
        }
    } catch (const std::exception &) {
        text = raw;
    }
    for (auto escape = text.find("\\n"); escape != std::string::npos; escape = text.find("\\n", escape)) {
        text.replace(escape, 2, "\n");
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

// The lines of a custom strings file, as they are written.
static std::vector<std::string> readCustomStrings(const std::filesystem::path &path) {
    std::vector<std::string> result;
    std::ifstream file(path, std::ios::binary);
    for (std::string line; std::getline(file, line);) {
        result.push_back(std::move(line));
    }
    return result;
}

// The custom strings of a talk table's language follow English's: French,
// Italian, German, Spanish.
static int customStringsLanguage(Language language) {
    switch (language) {
    case Language::French:
        return 1;
    case Language::Italian:
        return 2;
    case Language::German:
        return 3;
    case Language::Spanish:
        return 4;
    default:
        return 0;
    }
}

void Strings::init(const std::filesystem::path &gameDir) {
    // Only a file of the five languages' strings is read, blank lines at its
    // end aside: the mobile releases' longer file holds other strings in the
    // same places.
    if (auto customPath = findFileIgnoreCase(gameDir, "override/custom.txt")) {
        auto lines = readCustomStrings(*customPath);
        while (!lines.empty() && lines.back().find_first_not_of("\r\n") == std::string::npos) {
            lines.pop_back();
        }
        if (lines.size() == static_cast<size_t>(kCustomStringLanguages * kCustomStringsPerLanguage)) {
            _customStrings = std::move(lines);
        }
    }
    auto tlkPath = findFileIgnoreCase(gameDir, "dialog.tlk");
    if (!tlkPath) {
        return;
    }
    auto tlk = FileInputStream(*tlkPath);
    auto tlkReader = TlkReader(tlk);
    tlkReader.load();
    setTalkTable(0, tlkReader.table());
}

void Strings::setTalkTable(std::size_t slot, std::shared_ptr<TalkTable> table) {
    if (slot >= _tables.size()) {
        throw std::out_of_range("talk table slot is out of range");
    }
    _tables[slot] = std::move(table);
}

bool Strings::loadTalkTable(std::size_t slot, const std::filesystem::path &path) {
    setTalkTable(slot, nullptr);
    try {
        auto tlk = FileInputStream(path);
        auto tlkReader = TlkReader(tlk);
        tlkReader.load();
        setTalkTable(slot, tlkReader.table());
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

const TalkTable::String *Strings::findString(int strRef) const {
    if (strRef == -1) {
        return nullptr;
    }

    auto row = static_cast<std::uint32_t>(strRef) & 0x00ffffff;
    for (const auto &table : _tables) {
        if (!table || row >= static_cast<std::uint32_t>(table->getStringCount())) {
            continue;
        }
        return &table->getString(static_cast<int>(row));
    }
    return nullptr;
}

CodePage codePageOfLanguage(Language language) {
    return language == Language::Polish ? CodePage::Windows1250 : CodePage::Windows1252;
}

Language Strings::language() const {
    return _tables[0] ? static_cast<Language>(_tables[0]->languageId()) : Language::English;
}

std::string Strings::getCustomText(int id) {
    const auto index = static_cast<size_t>(customStringsLanguage(language()) * kCustomStringsPerLanguage + id);
    return index < _customStrings.size() ? customString(_customStrings[index], codePageOfLanguage(language())) : std::string();
}

std::string Strings::getText(int strRef) {
    auto string = findString(strRef);
    if (!string) {
        return "";
    }

    return string->text;
}

std::string Strings::getSound(int strRef) {
    auto string = findString(strRef);
    if (!string) {
        return "";
    }

    return string->soundResRef;
}

float Strings::getSoundDuration(int strRef) {
    auto string = findString(strRef);
    return string ? string->soundLength : 0.0f;
}

} // namespace resource

} // namespace reone
