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
    mount(openKeyBif(path), ResourceSourceForm::KeyBif, path, ResourceOwner::Global, bucket);
}

void Resources::addERF(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    mount(openErf(path), ResourceSourceForm::Erf, path, owner, bucket);
}

void Resources::addMemERF(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    mount(openErf(std::move(buffer)), ResourceSourceForm::Erf, {}, owner, bucket);
}

void Resources::addRIM(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    mount(openRim(path), ResourceSourceForm::Rim, path, owner, bucket);
}

void Resources::addMemRIM(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    mount(openRim(std::move(buffer)), ResourceSourceForm::Rim, {}, owner, bucket);
}

void Resources::addEXE(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    mount(openExe(path), ResourceSourceForm::Executable, path, ResourceOwner::Global, bucket);
}

void Resources::addFolder(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    mount(openFolder(path), ResourceSourceForm::Folder, path, owner, bucket);
}

std::string describe(const ResourceSourceRef &source) {
    if (source.form == ResourceSourceForm::Replacement) {
        return "runtime replacement, revision " + std::to_string(source.replacementRevision);
    }
    static const std::unordered_map<ResourceSourceForm, std::string> forms {
        {ResourceSourceForm::KeyBif, "KEY/BIF"},
        {ResourceSourceForm::Erf, "ERF"},
        {ResourceSourceForm::Rim, "RIM"},
        {ResourceSourceForm::Folder, "folder"},
        {ResourceSourceForm::Executable, "executable"},
        {ResourceSourceForm::Memory, "memory"}};
    static const std::unordered_map<ResourceSourceBucket, std::string> buckets {
        {ResourceSourceBucket::LooseDirectory, "loose directory"},
        {ResourceSourceBucket::EncapsulatedClass1, "encapsulated class 1"},
        {ResourceSourceBucket::ResourceImage, "resource image"},
        {ResourceSourceBucket::EncapsulatedClass2, "encapsulated class 2"},
        {ResourceSourceBucket::KeyBif, "KEY/BIF"}};
    static const std::unordered_map<ResourceOwner, std::string> owners {
        {ResourceOwner::Global, "global"},
        {ResourceOwner::SaveSlot, "save slot"},
        {ResourceOwner::ActiveModule, "module"},
        {ResourceOwner::ActiveModuleState, "module state"}};
    auto text = forms.at(source.form) + " " + (source.path.empty() ? std::string("in memory") : source.path.string());
    text += " (";
    if (source.bucket) {
        text += buckets.at(*source.bucket) + ", ";
    }
    text += owners.at(source.owner) + ")";
    return text;
}

Resource Resources::get(const ResourceId &id) {
    auto data = find(id);
    if (!data) {
        throw ResourceNotFoundException(id.string());
    }
    return *data;
}

std::optional<Resource> Resources::find(const ResourceId &id) {
    auto container = _containers.resolve(holds(id));
    if (!container) {
        return std::nullopt;
    }
    return Resource {*container->provider->findResourceData(id), ResourceContainerList::sourceRef(*container)};
}

std::unique_ptr<IResources> Resources::detached(const std::set<ResourceOwner> &excludedOwners) const {
    auto copy = std::make_unique<Resources>();
    copy->_containers = _containers.detached(excludedOwners);
    return copy;
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
