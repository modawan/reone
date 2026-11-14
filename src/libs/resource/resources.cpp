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

/// Whether a container has the resource. A container builds its ids and its
/// index together, so holding an id means reading it succeeds.
auto holds(const ResourceId &id) {
    return [&id](const ResourceContainerPair &container) {
        return container.provider->resourceIds().count(id) != 0;
    };
}

} // namespace

void Resources::addKEY(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<KeyBifResourceContainer>(path);
    provider->init();
    mount(std::move(provider), ResourceSourceForm::KeyBif, path, ResourceOwner::Global, bucket);
}

void Resources::addERF(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<ErfResourceContainer>(path);
    provider->init();
    mount(std::move(provider), ResourceSourceForm::Erf, path, owner, bucket);
}

void Resources::addMemERF(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<ErfResourceContainer>(std::move(buffer));
    provider->init();
    mount(std::move(provider), ResourceSourceForm::Erf, {}, owner, bucket);
}

void Resources::addRIM(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<RimResourceContainer>(path);
    provider->init();
    mount(std::move(provider), ResourceSourceForm::Rim, path, owner, bucket);
}

void Resources::addMemRIM(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<RimResourceContainer>(buffer);
    provider->init();
    mount(std::move(provider), ResourceSourceForm::Rim, {}, owner, bucket);
}

void Resources::addEXE(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<ExeResourceContainer>(path);
    provider->init();
    mount(std::move(provider), ResourceSourceForm::Executable, path, ResourceOwner::Global, bucket);
}

void Resources::addFolder(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<FolderResourceContainer>(path);
    provider->init();
    mount(std::move(provider), ResourceSourceForm::Folder, path, owner, bucket);
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
    auto container = _containers.resolve(holds(id), excludedOwners);
    if (!container) {
        return std::nullopt;
    }
    return Resource {*container->provider->findResourceData(id), ResourceContainerList::sourceRef(*container)};
}

std::optional<ResourceSourceRef> Resources::locate(const ResourceId &id) const {
    auto container = _containers.resolve(holds(id));
    if (!container) {
        return std::nullopt;
    }
    return ResourceContainerList::sourceRef(*container);
}

std::vector<ResourceSourceRef> Resources::explain(const ResourceId &id) const {
    std::vector<ResourceSourceRef> sources;
    for (auto container : _containers.candidates(holds(id))) {
        sources.push_back(ResourceContainerList::sourceRef(*container));
    }
    return sources;
}

} // namespace resource

} // namespace reone
