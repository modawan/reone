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

#include "reone/system/textreader.h"

namespace reone {

std::optional<std::string> TextReader::readLine() {
    static constexpr size_t kChunkSize = 256;

    auto start = _stream.position();
    std::string line;
    std::vector<char> buf;
    buf.resize(kChunkSize);

    // Reads in chunks until a terminator turns up, rather than assuming a line
    // fits in one. A fixed-size read used to truncate anything longer, splitting
    // the line mid-token for the caller.
    size_t consumed = 0;
    while (true) {
        _stream.seek(start + consumed);
        int numRead = _stream.read(&buf[0], buf.size());
        if (numRead == 0) {
            if (consumed == 0) {
                return std::nullopt;
            }
            return line;
        }
        int len = 0;
        while (len < numRead && buf[len] != '\r' && buf[len] != '\n') {
            ++len;
        }
        line.append(&buf[0], len);
        if (len == numRead) {
            consumed += numRead;
            continue;
        }
        size_t terminator = 1;
        if (buf[len] == '\r') {
            // Consume the LF of a CRLF pair, but tolerate a bare CR.
            if (len + 1 < numRead) {
                if (buf[len + 1] == '\n') {
                    terminator = 2;
                }
            } else {
                char next;
                _stream.seek(start + consumed + len + 1);
                if (_stream.read(&next, 1) == 1 && next == '\n') {
                    terminator = 2;
                }
            }
        }
        _stream.seek(start + consumed + len + terminator);
        return line;
    }
}

} // namespace reone
