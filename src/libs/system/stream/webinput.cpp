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

#include "reone/system/stream/webinput.h"

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <cstring>

#include <emscripten.h>

namespace reone {

namespace {

constexpr size_t kWebMirrorReadChunk = 1 * 1024 * 1024;
/** Max bytes per EM_ASYNC_JS read — large single awaits can stall Asyncify under headless Chrome. */
constexpr size_t kWebMirrorAsyncReadMax = 256 * 1024;

} // namespace

// Requires -sASYNCIFY=1 on the final link: suspends the whole C++ stack while awaiting JS I/O.
EM_ASYNC_JS(double, reone_web_file_length, (const char *path), {
    if (!Module.reoneWebFileLengthAsync) {
        return -1;
    }
    var v = await Module.reoneWebFileLengthAsync(UTF8ToString(path));
    return typeof v === "number" && Number.isFinite(v) ? v : -1;
});

EM_ASYNC_JS(int, reone_web_file_read, (const char *path, double offset, char *buf, int len), {
    if (!Module.reoneWebFileReadAsync) {
        return -1;
    }
    if (len <= 0) {
        return 0;
    }
    var data = await Module.reoneWebFileReadAsync(UTF8ToString(path), offset, len);
    if (!data) {
        return -1;
    }
    var n = Math.min(data.length | 0, len | 0);
    HEAPU8.set(data.subarray(0, n), buf);
    return n;
});

WebFileInputStream::WebFileInputStream(const std::filesystem::path &path) :
    _path(path.generic_string()) {
}

void WebFileInputStream::ensureLength() {
    if (_lengthKnown) {
        return;
    }
    auto len = reone_web_file_length(_path.c_str());
    if (len < 0) {
        throw std::runtime_error("Failed to stat web game file: " + _path);
    }
    _length = static_cast<size_t>(len);
    _lengthKnown = true;
}

void WebFileInputStream::invalidateBuffer() {
    _bufLen = 0;
}

void WebFileInputStream::refillBuffer() {
    ensureLength();
    size_t pos = static_cast<size_t>(_position);
    if (_bufLen != 0 && pos >= _bufFileOffset && pos < _bufFileOffset + _bufLen) {
        return;
    }
    if (pos >= _length) {
        return;
    }
    // Align the buffer window to the full read chunk (kWebMirrorReadChunk) so the loaded window always
    // contains pos. Each underlying async read stays capped at kWebMirrorAsyncReadMax. A previous bug
    // aligned to kWebMirrorReadChunk (1 MiB) but read only kWebMirrorAsyncReadMax (256 KiB), so any pos
    // more than 256 KiB past a 1 MiB boundary missed the window and read() returned 0 (zero-filled
    // resources -> corrupt models/WAVs).
    size_t readStart = (pos / kWebMirrorReadChunk) * kWebMirrorReadChunk;
    size_t chunk = std::min(kWebMirrorReadChunk, _length - readStart);
    _buf.resize(chunk);
    size_t filled = 0;
    while (filled < chunk) {
        size_t want = std::min(kWebMirrorAsyncReadMax, chunk - filled);
        int r = reone_web_file_read(
            _path.c_str(),
            static_cast<double>(readStart + filled),
            _buf.data() + filled,
            static_cast<int>(want));
        if (r < 0) {
            throw std::runtime_error("Failed to read web game file: " + _path);
        }
        if (r == 0) {
            break;
        }
        filled += static_cast<size_t>(r);
    }
    if (filled == 0) {
        throw std::runtime_error("Unexpected EOF reading web game file: " + _path);
    }
    _bufFileOffset = readStart;
    _bufLen = filled;
}

void WebFileInputStream::seek(int64_t offset, SeekOrigin origin) {
    ensureLength();
    if (origin == SeekOrigin::Begin) {
        _position = offset;
    } else if (origin == SeekOrigin::Current) {
        _position += offset;
    } else if (origin == SeekOrigin::End) {
        _position = static_cast<int64_t>(_length) + offset;
    } else {
        throw std::invalid_argument("Invalid origin: " + std::to_string(static_cast<int>(origin)));
    }
    if (_position < 0) {
        _position = 0;
    }
    invalidateBuffer();
}

int WebFileInputStream::readByte() {
    char ch = 0;
    int n = read(&ch, 1);
    return n == 1 ? static_cast<unsigned char>(ch) : -1;
}

int WebFileInputStream::read(char *buf, int len) {
    if (len <= 0 || static_cast<size_t>(_position) >= _length) {
        return 0;
    }
    int total = 0;
    while (total < len && static_cast<size_t>(_position) < _length) {
        refillBuffer();
        size_t pos = static_cast<size_t>(_position);
        if (_bufLen == 0 || pos < _bufFileOffset || pos >= _bufFileOffset + _bufLen) {
            throw std::runtime_error("Short read from web game file: " + _path);
        }
        size_t offInBuf = pos - _bufFileOffset;
        size_t avail = _bufLen - offInBuf;
        size_t need = static_cast<size_t>(len - total);
        size_t remain = _length - pos;
        size_t n = std::min({avail, need, remain});
        std::memcpy(buf + total, _buf.data() + offInBuf, n);
        total += static_cast<int>(n);
        _position += static_cast<int64_t>(n);
    }
    return total;
}

size_t WebFileInputStream::position() {
    return static_cast<size_t>(_position);
}

size_t WebFileInputStream::length() {
    ensureLength();
    return _length;
}

} // namespace reone

#endif
