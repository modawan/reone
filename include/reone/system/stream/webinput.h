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

#include <vector>

#include "input.h"

namespace reone {

#ifdef __EMSCRIPTEN__

class WebFileInputStream : public IInputStream {
public:
    WebFileInputStream(const std::filesystem::path &path);

    void seek(int64_t offset, SeekOrigin origin) override;

    int readByte() override;

    int read(char *buf, int len) override;

    size_t position() override;

    size_t length() override;

private:
    void invalidateBuffer();

    /** One ranged HTTP fetch per chunk instead of per read(); avoids browser connection_limits / ERR_INVALID_ARGUMENT. */
    void refillBuffer();

    std::string _path;
    int64_t _position {0};
    size_t _length {0};
    std::vector<char> _buf;
    size_t _bufFileOffset {0};
    size_t _bufLen {0};
};

#endif

} // namespace reone
