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

#include "reone/resource/id.h"
#include "reone/system/types.h"

#include <filesystem>
#include <optional>

namespace reone {

namespace extract {

std::optional<resource::ResourceId> resourceIdFromPath(const std::filesystem::path &path);

/// Metadata for a resource entry (PyKotor FileResource).
class FileResource {
public:
    FileResource(std::string resRef,
                 resource::ResType type,
                 uint32_t size,
                 uint32_t offset,
                 std::filesystem::path filepath);

    const resource::ResourceId &id() const { return _id; }
    const std::filesystem::path &filepath() const { return _filepath; }
    uint32_t offset() const { return _offset; }
    uint32_t size() const { return _size; }
    bool insideCapsule() const { return _insideCapsule; }
    bool insideBif() const { return _insideBif; }

    ByteBuffer readData() const;

private:
    resource::ResourceId _id;
    uint32_t _size;
    uint32_t _offset;
    std::filesystem::path _filepath;
    bool _insideCapsule;
    bool _insideBif;
};

/// Resolved location before byte load (PyKotor LocationResult).
class LocationResult {
public:
    LocationResult(std::filesystem::path filepath, uint32_t offset, uint32_t size);

    const std::filesystem::path &filepath() const { return _filepath; }
    uint32_t offset() const { return _offset; }
    uint32_t size() const { return _size; }

    void setFileResource(FileResource resource) { _fileResource = std::move(resource); }
    const std::optional<FileResource> &fileResource() const { return _fileResource; }

    ByteBuffer readData() const;

private:
    std::filesystem::path _filepath;
    uint32_t _offset;
    uint32_t _size;
    std::optional<FileResource> _fileResource;
};

} // namespace extract

} // namespace reone
