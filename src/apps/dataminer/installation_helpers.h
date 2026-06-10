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

#include "reone/extract/finder.h"
#include "reone/extract/installation.h"
#include "reone/resource/id.h"

namespace reone {

namespace dataminer {

inline extract::SearchScope chitinOnlyOrder() {
    return {extract::SearchLocation::Chitin};
}

inline extract::SearchScope overrideThenChitinOrder() {
    return {extract::SearchLocation::Override, extract::SearchLocation::Chitin};
}

inline std::optional<ByteBuffer> readResource(extract::Installation &installation,
                                              const resource::ResourceId &id,
                                              const extract::SearchScope &order) {
    auto location = installation.resource(id, order);
    if (!location) {
        return std::nullopt;
    }
    return location->readData();
}

template <typename Fn>
void forEachResource(const std::vector<extract::FileResource> &resources, resource::ResType type, Fn &&fn) {
    for (const auto &resource : resources) {
        if (resource.id().type == type) {
            fn(resource);
        }
    }
}

template <typename Fn>
void forEachResource(const std::unordered_map<std::string, std::vector<extract::FileResource>> &archives,
                     resource::ResType type,
                     Fn &&fn) {
    for (const auto &[_, resources] : archives) {
        forEachResource(resources, type, std::forward<Fn>(fn));
    }
}

} // namespace dataminer

} // namespace reone
