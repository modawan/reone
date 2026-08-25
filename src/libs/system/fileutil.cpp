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

#include "reone/system/fileutil.h"

#include "reone/system/exception/filenotfound.h"
#include "reone/system/logutil.h"

namespace reone {

static bool splitRelativePath(std::string_view relPath, std::vector<std::string> &tokens) {
    boost::split(tokens, relPath, boost::is_any_of("/\\"), boost::token_compress_off);
    for (const auto &token : tokens) {
        if (token.empty() || token == "." || token == "..") {
            return false;
        }
    }
    return true;
}

/**
 * Resolve one path component below dir, ignoring case.
 *
 * The contract, in order:
 *
 *  - An exact match wins. On the case-insensitive filesystems the games shipped
 *    for this is the only reachable outcome, so it is never ambiguous.
 *  - A unique case-folded match resolves to its real path. This is what the
 *    helper exists for: retail data ships under whatever casing the original
 *    installer used.
 *  - Several case-folded matches with no exact match is a genuine ambiguity.
 *    The requested name designates more than one filesystem entity and nothing
 *    at this layer can know which was meant.
 *
 * Only the on-disk entry is folded; the requested name is expected to already
 * be lowercase, and a name that is not stays unmatched.
 *
 * The ambiguous case still resolves rather than failing, because mod installs
 * and archive extraction do produce collisions ("Override" beside "override")
 * and refusing them would strand installations that work today. It is reported
 * instead of being swallowed, and the winner is chosen by name so that every
 * consumer asking the same question gets the same answer rather than whatever
 * the directory happened to yield first. That ordering is a tie-break for
 * reproducibility only: it is not a claim about which entity is correct.
 */
static std::optional<std::filesystem::path> resolveComponentIgnoreCase(
    const std::filesystem::path &dir,
    const std::string &name) {
    std::vector<std::filesystem::path> folded;
    for (auto &entry : std::filesystem::directory_iterator(dir)) {
        auto filename = entry.path().filename().string();
        if (filename == name) {
            return entry.path();
        }
        if (boost::to_lower_copy(filename) == name) {
            folded.push_back(entry.path());
        }
    }
    if (folded.empty()) {
        return std::nullopt;
    }
    std::sort(folded.begin(), folded.end());
    if (folded.size() > 1) {
        std::string candidates;
        for (const auto &candidate : folded) {
            if (!candidates.empty()) {
                candidates += ", ";
            }
            candidates += candidate.filename().string();
        }
        warn("Ambiguous case-insensitive lookup of '" + name + "' in " +
             dir.string() + ": " + candidates + " differ only by case; using '" +
             folded.front().filename().string() + "'");
    }
    return folded.front();
}

std::filesystem::path getFileIgnoreCase(const std::filesystem::path &dir, std::string_view relPath) {
    auto path = findFileIgnoreCase(dir, relPath);
    if (!path) {
        throw FileNotFoundException((dir / relPath).string());
    }
    return *path;
}

std::optional<std::filesystem::path> findFileIgnoreCase(const std::filesystem::path &dir, std::string_view relPath) {
    std::vector<std::string> tokens;
    if (!splitRelativePath(relPath, tokens)) {
        return std::nullopt;
    }

    auto resolved = resolveComponentIgnoreCase(dir, tokens[0]);
    if (!resolved) {
        return std::nullopt;
    }
    if (tokens.size() == 1) {
        return resolved;
    }
    return findFileIgnoreCase(*resolved, relPath.substr(tokens[0].length() + 1));
}

bool isValidResRef(std::string_view name, size_t maxLen) {
    if (name.empty() || name.size() > maxLen) {
        return false;
    }
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

bool isSafePathComponent(std::string_view name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    for (char c : name) {
        if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
        switch (c) {
        case '/':
        case '\\':
        case ':':
        case '<':
        case '>':
        case '"':
        case '|':
        case '?':
        case '*':
            return false;
        default:
            break;
        }
    }
    return true;
}

} // namespace reone
