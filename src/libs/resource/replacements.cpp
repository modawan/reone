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

#include "reone/resource/replacements.h"

namespace reone {

namespace resource {

void ResourceReplacements::replaceResource(ResourceId id, ByteBuffer data) {
    _resources[id] = std::make_shared<const ByteBuffer>(std::move(data));
    advanceRevision(id);
}

void ResourceReplacements::removeResourceReplacement(const ResourceId &id) {
    if (_resources.erase(id) != 0) {
        advanceRevision(id);
    }
}

void ResourceReplacements::clearResourceReplacements() {
    for (const auto &[id, _] : _resources) {
        advanceRevision(id);
    }
    _resources.clear();
}

std::optional<Resource> ResourceReplacements::findResourceReplacement(const ResourceId &id) const {
    auto it = _resources.find(id);
    if (it == _resources.end()) {
        return std::nullopt;
    }
    return Resource {*it->second};
}

uint64_t ResourceReplacements::revision(const ResourceId &id) const {
    auto it = _revisions.find(id);
    return it == _revisions.end() ? 0 : it->second;
}

void ResourceReplacements::advanceRevision(const ResourceId &id) {
    _revisions[id] = ++_nextRevision;
}

} // namespace resource

} // namespace reone
