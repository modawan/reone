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

#include "reone/resource/resources.h"

#include "reone/resource/container/erf.h"
#include "reone/resource/container/exe.h"
#include "reone/resource/container/folder.h"
#include "reone/resource/container/keybif.h"
#include "reone/resource/container/rim.h"
#include "reone/resource/exception/notfound.h"

namespace reone {

namespace resource {

namespace {

template <class Container, class Source>
std::unique_ptr<IResourceContainer> open(Source source) {
    auto container = std::make_unique<Container>(std::move(source));
    container->init();
    return container;
}

} // namespace

std::unique_ptr<IResourceContainer> openKeyBif(const std::filesystem::path &keyPath) {
    return open<KeyBifResourceContainer>(keyPath);
}

std::unique_ptr<IResourceContainer> openErf(const std::filesystem::path &path) {
    return open<ErfResourceContainer>(Storage(path));
}

std::unique_ptr<IResourceContainer> openErf(ByteBuffer bytes) {
    return open<ErfResourceContainer>(Storage(std::move(bytes)));
}

std::unique_ptr<IResourceContainer> openRim(const std::filesystem::path &path) {
    return open<RimResourceContainer>(Storage(path));
}

std::unique_ptr<IResourceContainer> openRim(ByteBuffer bytes) {
    return open<RimResourceContainer>(Storage(std::move(bytes)));
}

std::unique_ptr<IResourceContainer> openFolder(const std::filesystem::path &path) {
    return open<FolderResourceContainer>(path);
}

std::unique_ptr<IResourceContainer> openExe(const std::filesystem::path &path) {
    return open<ExeResourceContainer>(path);
}

void Resources::addKEY(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    add(openKeyBif(path), ResourceOwner::Global, bucket);
}

void Resources::addERF(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    add(openErf(path), owner, bucket);
}

void Resources::addMemERF(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    add(openErf(std::move(buffer)), owner, bucket);
}

void Resources::addRIM(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    add(openRim(path), owner, bucket);
}

void Resources::addMemRIM(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    add(openRim(std::move(buffer)), owner, bucket);
}

void Resources::addEXE(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    add(openExe(path), ResourceOwner::Global, bucket);
}

void Resources::addFolder(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    add(openFolder(path), owner, bucket);
}

Resource Resources::get(const ResourceId &id) {
    auto data = find(id);
    if (!data) {
        throw ResourceNotFoundException(id.string());
    }
    return *data;
}

std::optional<Resource> Resources::find(const ResourceId &id) {
    return findExcludingOwners(id, {});
}

std::optional<Resource> Resources::findExcludingOwners(
    const ResourceId &id,
    const std::set<ResourceOwner> &excludedOwners) {
    for (auto &container : _containers) {
        if (excludedOwners.count(container.owner) != 0) {
            continue;
        }
        auto data = container.provider->findResourceData(id);
        if (data) {
            return Resource {*data};
        }
    }
    return std::nullopt;
}

} // namespace resource

} // namespace reone
