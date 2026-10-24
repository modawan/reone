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

#include <optional>
#include <string>
#include <vector>

namespace reone {

namespace game {

/**
 * A pattern an object listens for, as SetListenPattern and
 * TestStringAgainstPattern take it. Patterns ignore case and must match the
 * whole string:
 * - ** matches any run of characters, the shortest first;
 * - *w, *n, *a and *p match one or more spaces, digits, letters or of the
 *   marks ! . ?, the shortest first;
 * - a|b matches either side, trying b first;
 * - ( ) groups;
 * - anything else matches itself.
 */
class ListenPattern {
public:
    /**
     * Nothing for a malformed pattern: a ) without its (, an unclosed (, a |
     * with nothing before it, a trailing * or a * before any other character.
     * An empty pattern gives nothing too.
     */
    static std::optional<ListenPattern> parse(const std::string &pattern);
    /** A pattern that matches only the empty string. */
    static ListenPattern emptyString();

    /** Whether the pattern matches the whole string. */
    bool match(const std::string &str) const;
    /**
     * On a match, \p pieces receives the whole string, then the part of it
     * each literal, ** or character class took, in order and in the string's
     * own case. A ** that took nothing gives an empty piece.
     */
    bool match(const std::string &str, std::vector<std::string> &pieces) const;

private:
    /** A link to no node. */
    static constexpr int kNoNode = -1;

    enum class NodeType {
        Literal,
        AnyRun,
        Choice,
        Join,
        Spaces,
        Digits,
        Letters,
        Marks,
        End
    };

    struct Node {
        NodeType type;
        std::string text;
        int next {kNoNode};
        int alternative {kNoNode};
    };

    enum class Outcome {
        Match,
        NoMatch,
        /**
         * A character class at the end of the string, or a ** asked to take
         * more there, fails the whole match at once.
         */
        Abandon
    };

    std::vector<Node> _nodes;
    int _head {kNoNode};

    Outcome matchFrom(int node, const std::string &str, size_t pos, std::vector<size_t> &ends) const;
    Outcome matchNext(const Node &node, const std::string &str, size_t end, std::vector<size_t> &ends) const;
};

} // namespace game

} // namespace reone
