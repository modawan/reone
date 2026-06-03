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

#include "searchlocation.h"

namespace reone {

namespace extract {

/// PyKotor tools/finder.canonical_search_order()
const SearchScope &canonicalSearchOrder();

/// PyKotor Installation.texture_resource_result() default order.
const SearchScope &textureSearchOrder();

/// Streamed audio lookup (music, SFX, voice). Extends PyKotor sounds() with Music/Voice.
const SearchScope &soundSearchOrder();

/// Loose installation-root files (e.g. dialog.tlk).
const SearchScope &talkTableSearchOrder();

/// movies/*.bik lookup order.
const SearchScope &movieSearchOrder();

const char *searchLocationName(SearchLocation location);

} // namespace extract

} // namespace reone
