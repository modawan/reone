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

void Resources::addKEY(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<KeyBifResourceContainer>(path);
    provider->init();
    add(std::move(provider), ResourceOwner::Global, bucket);
}

void Resources::addERF(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<ErfResourceContainer>(path);
    provider->init();
    add(std::move(provider), owner, bucket);
}

void Resources::addMemERF(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<ErfResourceContainer>(std::move(buffer));
    provider->init();
    add(std::move(provider), owner, bucket);
}

void Resources::addRIM(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<RimResourceContainer>(path);
    provider->init();
    add(std::move(provider), owner, bucket);
}

void Resources::addMemRIM(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<RimResourceContainer>(buffer);
    provider->init();
    add(std::move(provider), owner, bucket);
}

void Resources::addEXE(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<ExeResourceContainer>(path);
    provider->init();
    add(std::move(provider), ResourceOwner::Global, bucket);
}

void Resources::addFolder(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto provider = std::make_unique<FolderResourceContainer>(path);
    provider->init();
    add(std::move(provider), owner, bucket);
}

Resource Resources::get(const ResourceId &id) {
    auto data = find(id);
    if (!data) {
        throw ResourceNotFoundException(id.string());
    }
    return *data;
}

std::optional<Resource> Resources::find(const ResourceId &id) {
    for (auto &container : _containers) {
        auto data = container.provider->findResourceData(id);
        if (data) {
            return Resource {*data};
        }
    }
    return std::nullopt;
}

} // namespace resource

} // namespace reone
