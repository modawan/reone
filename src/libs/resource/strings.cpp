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

#include "reone/resource/exception/notfound.h"
#include "reone/resource/format/tlkreader.h"
#include "reone/resource/talktable.h"
#include "reone/system/fileutil.h"
#include "reone/system/stream/fileinput.h"

namespace reone {

namespace resource {

void Strings::init(const std::filesystem::path &gameDir) {
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

std::string Strings::getText(int strRef) {
    auto string = findString(strRef);
    if (!string) {
        return "";
    }

    std::string text(string->text);
    process(text);

    return text;
}

std::string Strings::getSound(int strRef) {
    auto string = findString(strRef);
    if (!string) {
        return "";
    }

    return string->soundResRef;
}

void Strings::process(std::string &str) {
    stripDeveloperNotes(str);
}

void Strings::stripDeveloperNotes(std::string &str) {
    do {
        size_t openBracketIdx = str.find_first_of('{', 0);
        if (openBracketIdx == -1)
            break;

        size_t closeBracketIdx = str.find_first_of('}', static_cast<int64_t>(openBracketIdx) + 1);
        if (closeBracketIdx == -1)
            break;

        int textLen = static_cast<int>(str.size());
        size_t noteLen = closeBracketIdx - openBracketIdx + 1;

        for (size_t i = openBracketIdx; i + noteLen < textLen; ++i) {
            str[i] = str[i + noteLen];
        }

        str.resize(textLen - noteLen);

    } while (true);
}

} // namespace resource

} // namespace reone
