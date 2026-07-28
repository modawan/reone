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

#include "id.h"
#include "resource.h"

namespace reone {

namespace resource {

class IResourceReplacements {
public:
    virtual ~IResourceReplacements() = default;

    virtual void replaceResource(ResourceId id, ByteBuffer data) = 0;
    virtual void removeResourceReplacement(const ResourceId &id) = 0;
    virtual void clearResourceReplacements() = 0;

    virtual std::optional<Resource> findResourceReplacement(const ResourceId &id) const = 0;
    virtual uint64_t revision(const ResourceId &id) const = 0;
};

class ResourceReplacements : public IResourceReplacements, boost::noncopyable {
public:
    void replaceResource(ResourceId id, ByteBuffer data) override;
    void removeResourceReplacement(const ResourceId &id) override;
    void clearResourceReplacements() override;

    std::optional<Resource> findResourceReplacement(const ResourceId &id) const override;
    uint64_t revision(const ResourceId &id) const override;

private:
    std::unordered_map<ResourceId, std::shared_ptr<const ByteBuffer>> _resources;
    std::unordered_map<ResourceId, uint64_t> _revisions;
    uint64_t _nextRevision {0};

    void advanceRevision(const ResourceId &id);
};

} // namespace resource

} // namespace reone
