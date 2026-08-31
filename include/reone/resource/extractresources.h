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

#include "resources.h"

namespace reone {

namespace resource {

/**
 * IResources backend that reads game data through the extract layer
 * primitives, while reproducing the exact lookup semantics of the legacy
 * container-based Resources: sources are searched newest-first, and
 * clearLocal/clearSave drop only sources mounted with the respective kind.
 *
 * This is a compatibility backend. Native extract::Installation search
 * orders are deliberately not used here; adopting them is a separate,
 * behavior-changing step.
 */
class ExtractResources : public IResources, boost::noncopyable {
public:
    void clear() override {
        _sources.clear();
    }

    void clearOwner(ResourceOwner owner) override {
        _sources.clearOwner(owner);
    }

    ResourceMountToken mountToken() const override {
        return _sources.mountToken();
    }

    void rollbackTo(ResourceMountToken token) override {
        _sources.rollbackTo(token);
    }

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

    size_t sourceCount() const { return _sources.size(); }

private:
    struct Source {
        ResourceOwner owner;
        std::optional<ResourceSourceBucket> bucket;
        ResourceMountToken sequence {0};
        std::function<std::optional<ByteBuffer>(const ResourceId &)> find;
    };

    ResourceSourceList<Source> _sources;

    void addSource(ResourceOwner owner,
                   std::optional<ResourceSourceBucket> bucket,
                   std::function<std::optional<ByteBuffer>(const ResourceId &)> find) {
        _sources.add(Source {owner, bucket, 0, std::move(find)});
    }

    void addMemArchive(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket, bool rim);
};

} // namespace resource

} // namespace reone
