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

#include <string>

#include "types.h"

namespace reone {

namespace game {

struct Portrait {
    std::string resRef;
    int appearanceNumber {0};
    int appearanceS {0};
    int appearanceL {0};
    /** The forpc cell; zero for a portrait no player character uses. */
    int forPC {0};
    int sex {0};
    /** The variants shown at good/evil 40 and below, 30, 20 and 10; blank cells are empty. */
    std::string resRefE;
    std::string resRefVE;
    std::string resRefVVE;
    std::string resRefVVVE;

    /**
     * The portrait shown for \p goodEvil: the variant of its band, or the base
     * above 40 or when the variant's cell is blank.
     */
    const std::string &resRefForGoodEvil(int goodEvil) const {
        const std::string &variant = goodEvil <= kVeryVeryVeryEvilMaxGoodEvil ? resRefVVVE
                                     : goodEvil <= kVeryVeryEvilMaxGoodEvil   ? resRefVVE
                                     : goodEvil <= kVeryEvilMaxGoodEvil       ? resRefVE
                                     : goodEvil <= kEvilMaxGoodEvil           ? resRefE
                                                                              : resRef;
        return variant.empty() ? resRef : variant;
    }
};

} // namespace game

} // namespace reone
