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

#include "id.h"

namespace reone {

namespace resource {

class IResourceContainer {
public:
    virtual ~IResourceContainer() {
    }

    virtual std::optional<ByteBuffer> findResourceData(const ResourceId &id) = 0;

    virtual const std::unordered_set<ResourceId> &resourceIds() const = 0;
};

/**
 * Open a container, indexed and ready to read.
 *
 * Everything that reads an archive or a folder of loose files by itself opens
 * it here and holds only the interface, so which reader stands behind a
 * format is decided in one place.
 */
std::unique_ptr<IResourceContainer> openKeyBif(const std::filesystem::path &keyPath);
std::unique_ptr<IResourceContainer> openErf(const std::filesystem::path &path);
std::unique_ptr<IResourceContainer> openErf(ByteBuffer bytes);
std::unique_ptr<IResourceContainer> openRim(const std::filesystem::path &path);
std::unique_ptr<IResourceContainer> openRim(ByteBuffer bytes);
std::unique_ptr<IResourceContainer> openFolder(const std::filesystem::path &path);
std::unique_ptr<IResourceContainer> openExe(const std::filesystem::path &path);

} // namespace resource

} // namespace reone
