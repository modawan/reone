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

#include "reone/graphics/uniformbuffer.h"

#include "reone/system/threadutil.h"

namespace reone {

namespace graphics {

void UniformBuffer::init() {
    if (_inited) {
        return;
    }
    checkMainThread();
    // std140 rounds a uniform block's size up to a multiple of 16 (a vec4). The C++ mirror structs end in
    // trailing scalars and are not padded, so sizeof() can be smaller than the shader block. Desktop GL
    // tolerates an undersized buffer bound via glBindBufferBase, but WebGL2/GLES rejects every draw with
    // "uniform buffer too small" (black screen). Allocate the rounded-up size; upload only the real bytes.
    _allocatedSize = (_size + 15) & ~static_cast<ptrdiff_t>(15);
    glGenBuffers(1, &_nameGL);
    glBindBuffer(GL_UNIFORM_BUFFER, _nameGL);
    glBufferData(GL_UNIFORM_BUFFER, _allocatedSize, nullptr, GL_DYNAMIC_DRAW);
    if (_data && _size > 0) {
        glBufferSubData(GL_UNIFORM_BUFFER, 0, _size, _data);
    }
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    _inited = true;
}

void UniformBuffer::deinit() {
    if (!_inited) {
        return;
    }
    checkMainThread();
    glDeleteBuffers(1, &_nameGL);
    _inited = false;
}

void UniformBuffer::bind(int index) {
    glBindBufferBase(GL_UNIFORM_BUFFER, index, _nameGL);
}

void UniformBuffer::unbind(int index) {
    glBindBufferBase(GL_UNIFORM_BUFFER, index, 0);
}

void UniformBuffer::refresh() {
    if (_size > _allocatedSize) {
        // Grow (and re-pad) if the data outgrew the original allocation; keeps the bound range >= block size.
        _allocatedSize = (_size + 15) & ~static_cast<ptrdiff_t>(15);
        glBufferData(GL_UNIFORM_BUFFER, _allocatedSize, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_UNIFORM_BUFFER, 0, _size, _data);
}

} // namespace graphics

} // namespace reone
