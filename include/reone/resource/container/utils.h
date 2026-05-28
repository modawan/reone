/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/system/stream/gameinput.h"
#include "reone/system/stream/memoryinput.h"

namespace reone {

namespace resource {

class Storage {
public:
    Storage(std::filesystem::path path) :
        _path(path), _file(openGameInputStream(_path)) {}
    Storage(ByteBuffer buffer) :
        _buffer(buffer), _mem(std::make_unique<MemoryInputStream>(_buffer)) {}

    IInputStream &stream() {
        assert((_file || _mem) && "uninitialized storage");
        if (_file) {
            return *_file;
        }
        return *_mem;
    }

private:
    std::filesystem::path _path;
    std::unique_ptr<IInputStream> _file;
    ByteBuffer _buffer;
    std::unique_ptr<MemoryInputStream> _mem;
};

} // namespace resource
} // namespace reone
