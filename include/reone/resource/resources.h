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

#include "reone/system/types.h"

#include "container.h"
#include "id.h"
#include "resource.h"
#include "sourcelist.h"

namespace reone {

namespace resource {

struct ResourceContainerPair {
    std::unique_ptr<IResourceContainer> provider;
    ContainerKind kind;
    std::optional<ResourceSourceBucket> bucket;
};

using ResourceContainerList = ResourceSourceList<ResourceContainerPair>;

/**
 * Facade over the sources game data is read from.
 *
 * Every mount takes an optional bucket. Passing none keeps the source in the
 * insertion order the engine has always used, which is what all current
 * callers do; passing one places the source in the raw lookup order instead.
 * Which sources belong in which bucket is a decision for the callers, not for
 * this layer.
 *
 * The two are not mixed: whichever a backend is given first fixes how it
 * orders sources until it is emptied, and mounting across that choice throws
 * ValidationException. A caller migrating to buckets migrates every mount it
 * makes, because an unbucketed source has no position in the raw lookup order
 * to be ranked at.
 */
class IResources {
public:
    virtual ~IResources() = default;

    virtual void clear() = 0;
    virtual void clearLocal() = 0;
    virtual void clearSave() = 0;

    virtual void addEXE(const std::filesystem::path &path,
                        std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addKEY(const std::filesystem::path &path,
                        std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addERF(const std::filesystem::path &path,
                        ContainerKind kind = ContainerKind::Global,
                        std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addMemERF(ByteBuffer buffer,
                           ContainerKind kind,
                           std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addRIM(const std::filesystem::path &path,
                        ContainerKind kind = ContainerKind::Global,
                        std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addMemRIM(ByteBuffer buffer,
                           ContainerKind kind = ContainerKind::Global,
                           std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addFolder(const std::filesystem::path &path,
                           ContainerKind kind = ContainerKind::Global,
                           std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;

    virtual Resource get(const ResourceId &id) = 0;
    virtual std::optional<Resource> find(const ResourceId &id) = 0;
};

class Resources : public IResources, boost::noncopyable {
public:
    void clear() override {
        _containers.clear();
    }

    void clearLocal() override {
        _containers.clearKind(ContainerKind::Local);
    }

    void clearSave() override {
        _containers.clearKind(ContainerKind::Save);
    }

    void add(std::unique_ptr<IResourceContainer> provider,
             ContainerKind kind = ContainerKind::Global,
             std::optional<ResourceSourceBucket> bucket = std::nullopt) {
        _containers.add(ResourceContainerPair {std::move(provider), kind, bucket});
    }

    void addEXE(const std::filesystem::path &path,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addKEY(const std::filesystem::path &path,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addERF(const std::filesystem::path &path,
                ContainerKind kind = ContainerKind::Global,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addMemERF(ByteBuffer buffer,
                   ContainerKind kind,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addRIM(const std::filesystem::path &path,
                ContainerKind kind = ContainerKind::Global,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addMemRIM(ByteBuffer buffer,
                   ContainerKind kind = ContainerKind::Global,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override;
    void addFolder(const std::filesystem::path &path,
                   ContainerKind kind = ContainerKind::Global,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override;

    Resource get(const ResourceId &id) override;
    std::optional<Resource> find(const ResourceId &id) override;

    const ResourceContainerList &containers() const { return _containers; }

private:
    ResourceContainerList _containers;
};

} // namespace resource

} // namespace reone
