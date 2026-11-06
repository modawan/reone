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

namespace reone {

namespace resource {

class Ltr : boost::noncopyable {
public:
    struct LetterSet {
        std::vector<float> start;
        std::vector<float> mid;
        std::vector<float> end;
    };

    Ltr(int letterCount,
        LetterSet singleLetters,
        std::vector<LetterSet> doubleLetters,
        std::vector<std::vector<LetterSet>> tripleLetters) :
        _letterCount(letterCount),
        _singleLetters(std::move(singleLetters)),
        _doubleLetters(std::move(doubleLetters)),
        _tripleLetters(std::move(tripleLetters)) {
    }

    /**
     * Draws a name from the letter tables: three opening letters, then one
     * letter at a time until the ending letter. Each step rolls 0-11 and adds a
     * middle letter while the roll exceeds the current length, so names stop
     * at 12 letters. With a non-zero maxLength the ending letter is forced
     * once the name is one letter short of it. A missing middle letter drops
     * the last letter when the name has at least four, otherwise the attempt
     * fails, as does a missing ending letter; a failed attempt starts over,
     * and after five failures the result is empty. The first letter is upper
     * case. A name listed in filter (in any case) is drawn again with no
     * length limit; an empty result is never filtered.
     *
     * A missing opening letter and a filtered name are drawn again until they
     * succeed: a table that can open a name, and a filter that leaves any name
     * the table draws, end the draws with probability one.
     */
    std::string randomName(int maxLength, const std::vector<std::string> &filter) const;

private:
    /** The letter indices of the three opening letters of a name. */
    struct Opening {
        int first {0};
        int second {0};
        int third {0};
    };

    int _letterCount;
    LetterSet _singleLetters;
    std::vector<LetterSet> _doubleLetters;
    std::vector<std::vector<LetterSet>> _tripleLetters;

    /** Draws the three opening letters, drawing all three again while any is missing. */
    Opening drawOpening() const;
    /** Draws one name as randomName does, without the filter. */
    std::string drawName(int maxLength) const;
};

} // namespace resource

} // namespace reone
