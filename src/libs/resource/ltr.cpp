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

#include "reone/resource/ltr.h"

#include <algorithm>

#include "reone/system/randomutil.h"

namespace reone {

namespace resource {

static const std::vector<char> g_letters {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '\'', '-'};

static constexpr int kMaxFailures = 5;
static constexpr int kLengthRoll = 12;
static constexpr int kMinBacktrackLength = 4;

/** The index in the letter tables of a letter of a drawn name. */
static int letterIndex(char letter) {
    if (letter == '\'') {
        return 26;
    }
    if (letter == '-') {
        return 27;
    }
    return letter - 'a';
}

// The first letter whose cumulative probability exceeds a random draw, or
// count when there is none.
static int drawLetter(const std::vector<float> &probabilities, int count) {
    float probability = randomFloat(0.0f, 1.0f);
    int letter = 0;
    while (letter < count && !(probabilities[letter] > probability)) {
        ++letter;
    }
    return letter;
}

Ltr::Opening Ltr::drawOpening() const {
    // Any opening letter missing draws all three again. A table that can open
    // a name ends this with probability one.
    while (true) {
        int first = drawLetter(_singleLetters.start, _letterCount);
        if (first == _letterCount) {
            continue;
        }
        int second = drawLetter(_doubleLetters[first].start, _letterCount);
        if (second == _letterCount) {
            continue;
        }
        int third = drawLetter(_tripleLetters[first][second].start, _letterCount);
        if (third == _letterCount) {
            continue;
        }
        return Opening {first, second, third};
    }
}

std::string Ltr::drawName(int maxLength) const {
    std::string name;
    int first = 0;
    int second = 0;
    int failures = 0;
    bool restart = true;

    // Each step adds a middle letter, drops one, ends the name or fails the
    // attempt. A name of 11 letters takes no middle letter, as the roll
    // never exceeds its length, so an attempt ends with probability one; the
    // fifth failed attempt ends the draw.
    while (true) {
        if (restart) {
            const Opening opening = drawOpening();
            name = {g_letters[opening.first], g_letters[opening.second], g_letters[opening.third]};
            first = opening.second;
            second = opening.third;
            restart = false;
        }

        // A middle letter while the roll exceeds the length, unless the name
        // is one letter short of its limit
        int roll = randomInt(0, kLengthRoll - 1);
        int length = static_cast<int>(name.size());
        bool middle = roll > length && !(maxLength != 0 && length == maxLength - 1);

        const LetterSet &set = _tripleLetters[first][second];
        int letter = drawLetter(middle ? set.mid : set.end, _letterCount);

        if (letter < _letterCount) {
            name += g_letters[letter];
            first = second;
            second = letter;
            if (!middle) {
                break;
            }
            continue;
        }
        if (middle && length >= kMinBacktrackLength) {
            name.pop_back();
            first = letterIndex(name[name.size() - 2]);
            second = letterIndex(name[name.size() - 1]);
            continue;
        }
        if (++failures == kMaxFailures) {
            return "";
        }
        restart = true;
    }

    name[0] = static_cast<char>(toupper(name[0]));
    return name;
}

std::string Ltr::randomName(int maxLength, const std::vector<std::string> &filter) const {
    auto isFiltered = [&filter](const std::string &name) {
        return std::any_of(filter.begin(), filter.end(), [&name](const std::string &filtered) {
            return boost::iequals(filtered, name);
        });
    };
    // A filtered name is drawn again, with no length limit, until one is not
    // filtered. A filter that leaves any name the table draws ends this with
    // probability one. A failed draw is empty and is not filtered.
    std::string name(drawName(maxLength));
    while (!name.empty() && isFiltered(name)) {
        name = drawName(0);
    }
    return name;
}

} // namespace resource

} // namespace reone
