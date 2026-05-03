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

#include "fileinput.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include "webinput.h"
#endif

namespace reone {

inline bool isGamePath(const std::filesystem::path &path) {
    auto generic = path.generic_string();
    return generic == "/game" || generic.rfind("/game/", 0) == 0;
}

#ifdef __EMSCRIPTEN__
inline bool isWebLazyGameFsActive() {
    return EM_ASM_INT({
        return Module.reoneWebLazyGameFsActive ? 1 : 0;
    }) != 0;
}
#endif

inline std::unique_ptr<IInputStream> openGameInputStream(const std::filesystem::path &path) {
#ifdef __EMSCRIPTEN__
    if (isGamePath(path) && isWebLazyGameFsActive()) {
        return std::make_unique<WebFileInputStream>(path);
    }
#endif
    return std::make_unique<FileInputStream>(path);
}

} // namespace reone
