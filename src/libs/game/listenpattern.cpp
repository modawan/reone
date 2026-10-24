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

#include "reone/game/listenpattern.h"

#include <boost/algorithm/string.hpp>

namespace reone {
namespace game {

std::optional<ListenPattern> ListenPattern::parse(const std::string &pattern) {
    // Nodes being joined into a pattern: the first, the one the next node goes
    // after, and the one every path through them ends at.
    struct Chain {
        int first {kNoNode};
        int current {kNoNode};
        int last {kNoNode};
    };

    if (pattern.empty()) {
        return std::nullopt;
    }
    const std::string lowered = boost::to_lower_copy(pattern);

    ListenPattern result;
    auto &nodes = result._nodes;
    auto newNode = [&nodes](NodeType type, std::string text = {}) {
        nodes.push_back({type, std::move(text)});
        return static_cast<int>(nodes.size()) - 1;
    };
    // A node goes after the chain's current node and becomes current.
    auto append = [&nodes](Chain &chain, int node) {
        if (chain.first == kNoNode && chain.last == kNoNode) {
            chain.first = chain.last = node;
        } else {
            if (chain.current == chain.last) {
                chain.last = node;
            }
            nodes[node].next = nodes[chain.current].next;
            nodes[chain.current].next = node;
        }
        chain.current = node;
    };

    // One chain for the whole pattern and one for each open group.
    std::vector<Chain> chains(1);
    for (size_t i = 0; i < lowered.size(); ++i) {
        const char c = lowered[i];
        if (c == '(') {
            chains.emplace_back();
        } else if (c == ')') {
            if (chains.size() == 1) {
                return std::nullopt;
            }
            const Chain group = chains.back();
            chains.pop_back();
            // An empty group adds nothing.
            if (group.first == kNoNode) {
                continue;
            }
            Chain &outer = chains.back();
            if (outer.first == kNoNode) {
                outer.first = group.first;
                outer.last = group.last;
            } else {
                nodes[group.last].next = nodes[outer.current].next;
                if (outer.current == outer.last) {
                    outer.last = group.last;
                }
                nodes[outer.current].next = group.first;
            }
            outer.current = group.last;
        } else if (c == '|') {
            Chain &chain = chains.back();
            if (chain.first == kNoNode) {
                return std::nullopt;
            }
            // Both sides lead on to a join. What follows the | is the side
            // tried first.
            const int join = newNode(NodeType::Join);
            const int choice = newNode(NodeType::Choice);
            append(chain, join);
            nodes[choice].alternative = chain.first;
            nodes[choice].next = join;
            chain.first = choice;
            chain.current = choice;
        } else if (c == '*') {
            if (++i == lowered.size()) {
                return std::nullopt;
            }
            NodeType type;
            switch (lowered[i]) {
            case '*':
                type = NodeType::AnyRun;
                break;
            case 'w':
                type = NodeType::Spaces;
                break;
            case 'n':
                type = NodeType::Digits;
                break;
            case 'a':
                type = NodeType::Letters;
                break;
            case 'p':
                type = NodeType::Marks;
                break;
            default:
                return std::nullopt;
            }
            append(chains.back(), newNode(type));
        } else {
            // A run of ordinary characters is one literal.
            size_t end = lowered.find_first_of("()|*", i);
            if (end == std::string::npos) {
                end = lowered.size();
            }
            append(chains.back(), newNode(NodeType::Literal, lowered.substr(i, end - i)));
            i = end - 1;
        }
    }
    if (chains.size() > 1) {
        return std::nullopt;
    }

    Chain &chain = chains.front();
    const int end = newNode(NodeType::End);
    if (chain.last != kNoNode) {
        nodes[chain.last].next = end;
    } else {
        append(chain, end);
    }
    result._head = chain.first;
    return result;
}

ListenPattern ListenPattern::emptyString() {
    ListenPattern result;
    result._nodes.push_back({NodeType::End});
    result._head = 0;
    return result;
}

bool ListenPattern::match(const std::string &str) const {
    std::vector<size_t> ends;
    return matchFrom(_head, boost::to_lower_copy(str), 0, ends) == Outcome::Match;
}

bool ListenPattern::match(const std::string &str, std::vector<std::string> &pieces) const {
    std::vector<size_t> ends;
    if (matchFrom(_head, boost::to_lower_copy(str), 0, ends) != Outcome::Match) {
        return false;
    }
    pieces.clear();
    pieces.push_back(str);
    size_t start = 0;
    for (size_t end : ends) {
        pieces.push_back(str.substr(start, end - start));
        start = end;
    }
    return true;
}

ListenPattern::Outcome ListenPattern::matchFrom(int index, const std::string &str, size_t pos, std::vector<size_t> &ends) const {
    const Node &node = _nodes[index];
    switch (node.type) {
    case NodeType::End:
        return pos == str.size() ? Outcome::Match : Outcome::NoMatch;
    case NodeType::Join:
        return matchFrom(node.next, str, pos, ends);
    case NodeType::Choice: {
        const Outcome outcome = matchFrom(node.next, str, pos, ends);
        return outcome != Outcome::NoMatch ? outcome : matchFrom(node.alternative, str, pos, ends);
    }
    case NodeType::Literal:
        if (str.compare(pos, node.text.size(), node.text) != 0) {
            return Outcome::NoMatch;
        }
        return matchNext(node, str, pos + node.text.size(), ends);
    case NodeType::AnyRun:
        // Nothing first, then one character more each time the rest fails.
        for (size_t end = pos;; ++end) {
            const Outcome outcome = matchNext(node, str, end, ends);
            if (outcome != Outcome::NoMatch) {
                return outcome;
            }
            if (end == str.size()) {
                return Outcome::Abandon;
            }
        }
    default:
        // One character first, then one more each time the rest fails, while
        // the next character is still in the class.
        for (size_t end = pos;;) {
            if (end == str.size()) {
                return Outcome::Abandon;
            }
            const char c = str[end];
            bool inClass = false;
            switch (node.type) {
            case NodeType::Spaces:
                inClass = c == ' ';
                break;
            case NodeType::Digits:
                inClass = c >= '0' && c <= '9';
                break;
            case NodeType::Letters:
                inClass = c >= 'a' && c <= 'z';
                break;
            default:
                inClass = c == '!' || c == '.' || c == '?';
                break;
            }
            if (!inClass) {
                return Outcome::NoMatch;
            }
            const Outcome outcome = matchNext(node, str, ++end, ends);
            if (outcome != Outcome::NoMatch) {
                return outcome;
            }
        }
    }
}

ListenPattern::Outcome ListenPattern::matchNext(const Node &node, const std::string &str, size_t end, std::vector<size_t> &ends) const {
    ends.push_back(end);
    const Outcome outcome = matchFrom(node.next, str, end, ends);
    if (outcome == Outcome::NoMatch) {
        ends.pop_back();
    }
    return outcome;
}

} // namespace game
} // namespace reone
