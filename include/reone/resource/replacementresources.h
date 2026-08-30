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

#include "replacements.h"
#include "resources.h"

namespace reone {

namespace resource {

class ReplacementResources : public IResources, boost::noncopyable {
public:
    ReplacementResources(std::unique_ptr<IResources> backend,
                         IResourceReplacements &replacements) :
        _backend(std::move(backend)),
        _replacements(replacements) {
    }

    void clear() override;
    void clearOwner(ResourceOwner owner) override;
    ResourceMountToken mountToken() const override;
    void rollbackTo(ResourceMountToken token) override;

    void addEXE(const std::filesystem::path &path,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addKEY(const std::filesystem::path &path,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addERF(const std::filesystem::path &path,
                ResourceOwner owner = ResourceOwner::Global,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addMemERF(ByteBuffer buffer,
                   ResourceOwner owner,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addRIM(const std::filesystem::path &path,
                ResourceOwner owner = ResourceOwner::Global,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addMemRIM(ByteBuffer buffer,
                   ResourceOwner owner = ResourceOwner::Global,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addFolder(const std::filesystem::path &path,
                   ResourceOwner owner = ResourceOwner::Global,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override;

    Resource get(const ResourceId &id) override;
    std::optional<Resource> find(const ResourceId &id) override;
    std::optional<Resource> findExcludingOwners(
        const ResourceId &id,
        const std::set<ResourceOwner> &excludedOwners) override;

private:
    std::unique_ptr<IResources> _backend;
    IResourceReplacements &_replacements;
};

} // namespace resource

} // namespace reone
