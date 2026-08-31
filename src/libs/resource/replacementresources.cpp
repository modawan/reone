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

#include "reone/resource/replacementresources.h"

#include "reone/resource/exception/notfound.h"

namespace reone {

namespace resource {

void ReplacementResources::clear() {
    _backend->clear();
}

void ReplacementResources::clearOwner(ResourceOwner owner) {
    _backend->clearOwner(owner);
}

ResourceMountToken ReplacementResources::mountToken() const {
    return _backend->mountToken();
}

void ReplacementResources::rollbackTo(ResourceMountToken token) {
    _backend->rollbackTo(token);
}

void ReplacementResources::addEXE(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    _backend->addEXE(path, bucket);
}

void ReplacementResources::addKEY(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    _backend->addKEY(path, bucket);
}

void ReplacementResources::addERF(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    _backend->addERF(path, owner, bucket);
}

void ReplacementResources::addMemERF(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    _backend->addMemERF(std::move(buffer), owner, bucket);
}

void ReplacementResources::addRIM(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    _backend->addRIM(path, owner, bucket);
}

void ReplacementResources::addMemRIM(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    _backend->addMemRIM(std::move(buffer), owner, bucket);
}

void ReplacementResources::addFolder(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    _backend->addFolder(path, owner, bucket);
}

Resource ReplacementResources::get(const ResourceId &id) {
    auto data = find(id);
    if (!data) {
        throw ResourceNotFoundException(id.string());
    }
    return *data;
}

std::optional<Resource> ReplacementResources::find(const ResourceId &id) {
    auto replacement = _replacements.findResourceReplacement(id);
    return replacement ? replacement : _backend->find(id);
}

std::optional<Resource> ReplacementResources::findExcludingOwners(
    const ResourceId &id,
    const std::set<ResourceOwner> &excludedOwners) {
    auto replacement = _replacements.findResourceReplacement(id);
    return replacement
               ? replacement
               : _backend->findExcludingOwners(id, excludedOwners);
}

} // namespace resource

} // namespace reone
