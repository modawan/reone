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

#include "reone/resource/format/2dareader.h"

#include "reone/resource/2da.h"
#include "reone/system/checkutil.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stringbuilder.h"

namespace reone {

namespace resource {

void TwoDAReader::load() {
    checkEqual("2DA signature", _reader.readString(8), std::string("2DA V2.b", 8));

    _reader.skipBytes(1); // newline
    _columns = readTokens();
    _rowCount = _reader.readUint32();
    _labels = readTokens(_rowCount);

    loadRows();
    loadTable();
}

void TwoDAReader::loadRows() {
    if (_rowCount == 0) {
        return;
    }
    _rows.reserve(_rowCount);

    int columnCount = static_cast<int>(_columns.size());
    int cellCount = _rowCount * columnCount;
    std::vector<uint16_t> offsets(cellCount);
    for (int i = 0; i < cellCount; ++i) {
        offsets[i] = _reader.readUint16();
    }

    uint16_t dataSize = _reader.readUint16();
    size_t pos = _reader.position();

    for (int i = 0; i < _rowCount; ++i) {
        TwoDA::Row row;
        // A truncated label block leaves the ordinal, which is what an
        // unlabelled table would have carried anyway.
        row.label = i < static_cast<int>(_labels.size()) ? _labels[i] : std::to_string(i);
        for (int j = 0; j < columnCount; ++j) {
            int cellIdx = i * columnCount + j;
            size_t off = pos + offsets[cellIdx];
            row.values.push_back(_reader.readCStringAt(off, 128));
        }
        _rows.push_back(row);
    }
}

void TwoDAReader::loadTable() {
    _twoDa = std::make_shared<TwoDA>(_columns, _rows);
}

std::vector<std::string> TwoDAReader::readTokens(int maxCount) {
    std::vector<std::string> tokens;
    StringBuilder str;
    for (auto ch = _reader.readChar();; ch = _reader.readChar()) {
        // Retail ships both delimiters for the same layout. The BIF copies of
        // these tables separate labels with tabs; the copies K1 carries inside
        // global.rim separate them with NULs and are otherwise identical, down
        // to the row count that follows. Only a NUL with nothing accumulated
        // ends the section, which is exactly what the tab form produces at its
        // own terminator, so one rule reads both.
        if (ch == '\t' || ch == '\0') {
            if (ch == '\0' && str.string().empty()) {
                break;
            }
            tokens.push_back(str.string());
            if (tokens.size() == maxCount) {
                break;
            }
            str.clear();
        } else {
            str.append(ch);
        }
    }
    return tokens;
}

} // namespace resource

} // namespace reone
