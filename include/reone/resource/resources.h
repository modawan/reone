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
    ResourceOwner owner;
    std::optional<ResourceSourceBucket> bucket;
    ResourceMountToken sequence {0};
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
 *
 * Every mount also takes an owner, which says what retires the source. Owner
 * and bucket are independent: clearing an owner never reorders what remains,
 * and a bucket never implies a lifetime.
 */
class IResources {
public:
    virtual ~IResources() = default;

    virtual void clear() = 0;

    /// Retire every source of one owner. The other owners are untouched, and
    /// the order of what remains does not change.
    virtual void clearOwner(ResourceOwner owner) = 0;

    /// The token a mount would take next, for undoing an operation that fails
    /// partway through. Prefer ResourceMountTransaction over calling this and
    /// rollbackTo by hand.
    virtual ResourceMountToken mountToken() const = 0;

    /// Retire every source mounted at or after the token, whatever its owner.
    virtual void rollbackTo(ResourceMountToken token) = 0;

    virtual void addEXE(const std::filesystem::path &path,
                        std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addKEY(const std::filesystem::path &path,
                        std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addERF(const std::filesystem::path &path,
                        ResourceOwner owner = ResourceOwner::Global,
                        std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addMemERF(ByteBuffer buffer,
                           ResourceOwner owner,
                           std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addRIM(const std::filesystem::path &path,
                        ResourceOwner owner = ResourceOwner::Global,
                        std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addMemRIM(ByteBuffer buffer,
                           ResourceOwner owner = ResourceOwner::Global,
                           std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;
    virtual void addFolder(const std::filesystem::path &path,
                           ResourceOwner owner = ResourceOwner::Global,
                           std::optional<ResourceSourceBucket> bucket = std::nullopt) = 0;

    virtual Resource get(const ResourceId &id) = 0;
    virtual std::optional<Resource> find(const ResourceId &id) = 0;
};

class Resources : public IResources, boost::noncopyable {
public:
    void clear() override {
        _containers.clear();
    }

    void clearOwner(ResourceOwner owner) override {
        _containers.clearOwner(owner);
    }

    ResourceMountToken mountToken() const override {
        return _containers.mountToken();
    }

    void rollbackTo(ResourceMountToken token) override {
        _containers.rollbackTo(token);
    }

    void add(std::unique_ptr<IResourceContainer> provider,
             ResourceOwner owner = ResourceOwner::Global,
             std::optional<ResourceSourceBucket> bucket = std::nullopt) {
        _containers.add(ResourceContainerPair {std::move(provider), owner, bucket});
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

    const ResourceContainerList &containers() const { return _containers; }

private:
    ResourceContainerList _containers;
};

} // namespace resource

} // namespace reone
