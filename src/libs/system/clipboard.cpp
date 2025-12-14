/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/system/clipboard.h"
#include "SDL2/SDL.h"
#include <string.h>

namespace reone {

ClipboardStream::~ClipboardStream() {
    free(_data);
}

std::unique_ptr<ClipboardStream> getClipboard() {
    char *text = SDL_GetClipboardText();
    size_t len = strlen(text);
    if (!len) {
        free(text);
        throw std::runtime_error("SDL_GetClipboardText failed: " + std::string(SDL_GetError()));
    }
    return std::make_unique<ClipboardStream>(text, len);
}

} // namespace reone
