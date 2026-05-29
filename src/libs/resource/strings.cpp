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

#include "reone/extract/installation.h"
#include "reone/extract/finder.h"
#include "reone/resource/talktable.h"
#include "reone/system/exception/endofstream.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/gameinput.h"

namespace reone {

namespace resource {

void Strings::init(extract::Installation &installation) {
    auto tlkPath = installation.resolveLooseRelativePath(
        "dialog.tlk",
        extract::talkTableSearchOrder());
    if (!tlkPath) {
        return;
    }
    try {
        auto tlk = openGameInputStream(*tlkPath);
        auto tlkReader = TlkReader(*tlk);
        tlkReader.load();
        _table = tlkReader.table();
    } catch (const ValidationException &) {
        // Unsupported talk table revision (e.g. GemRB demo TLK V1); strings stay empty.
    } catch (const EndOfStreamException &) {
        // Truncated or non-retail talk table.
    }
}

std::string Strings::getText(int strRef) {
    if (!_table || strRef < 0 || strRef >= _table->getStringCount())
        return "";

    std::string text(_table->getString(strRef).text);
    process(text);

    return text;
}

std::string Strings::getSound(int strRef) {
    if (!_table || strRef < 0 || strRef >= _table->getStringCount())
        return "";

    return _table->getString(strRef).soundResRef;
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
