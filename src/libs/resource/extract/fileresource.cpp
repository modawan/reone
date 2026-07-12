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

#include "reone/extract/fileresource.h"

#include "reone/resource/typeutil.h"
#include "reone/system/stream/gameinput.h"

namespace reone {

namespace extract {

static std::string extensionLower(const std::filesystem::path &path) {
    auto ext = path.extension().string();
    if (!ext.empty() && ext[0] == '.') {
        ext.erase(0, 1);
    }
    boost::to_lower(ext);
    return ext;
}

static bool isCapsuleExt(const std::string &ext) {
    return ext == "erf" || ext == "mod" || ext == "rim" || ext == "sav" || ext == "hak";
}

std::optional<resource::ResourceId> resourceIdFromPath(const std::filesystem::path &path) {
    auto filename = path.filename().string();
    if (filename.empty()) {
        return std::nullopt;
    }
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return std::nullopt;
    }
    auto resRef = boost::to_lower_copy(filename.substr(0, dot));
    auto ext = boost::to_lower_copy(filename.substr(dot + 1));
    auto type = resource::getResTypeByExt(ext, false);
    if (type == resource::ResType::Invalid) {
        return std::nullopt;
    }
    return resource::ResourceId(std::move(resRef), type);
}

std::optional<FileResource> FileResource::fromPath(const std::filesystem::path &path) {
    auto id = resourceIdFromPath(path);
    if (!id || !std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }
    return FileResource(
        id->resRef.value(),
        id->type,
        static_cast<uint32_t>(std::filesystem::file_size(path)),
        0,
        path);
}

FileResource::FileResource(std::string resRef,
                           resource::ResType type,
                           uint32_t size,
                           uint32_t offset,
                           std::filesystem::path filepath) :
    _id(std::move(resRef), type),
    _size(size),
    _offset(offset),
    _filepath(std::move(filepath)) {
    auto ext = extensionLower(_filepath);
    _insideCapsule = isCapsuleExt(ext);
    _insideBif = ext == "bif";
}

std::filesystem::path FileResource::pathIdentifier() const {
    if (_insideCapsule || _insideBif) {
        return _filepath / filename();
    }
    return _filepath;
}

bool FileResource::exists() const {
    if (!std::filesystem::is_regular_file(_filepath)) {
        return false;
    }
    if (!_insideCapsule && !_insideBif) {
        return true;
    }
    auto fileSize = std::filesystem::file_size(_filepath);
    auto begin = static_cast<uintmax_t>(_offset);
    auto length = static_cast<uintmax_t>(_size);
    if (begin > fileSize) {
        return false;
    }
    return length <= fileSize - begin;
}

ByteBuffer FileResource::readData() const {
    auto stream = openGameInputStream(_filepath);
    stream->seek(static_cast<int64_t>(_offset), SeekOrigin::Begin);
    ByteBuffer buf;
    buf.resize(_size);
    if (_size > 0) {
        stream->read(buf.data(), buf.size());
    }
    return buf;
}

LocationResult::LocationResult(std::filesystem::path filepath, uint32_t offset, uint32_t size) :
    _filepath(std::move(filepath)),
    _offset(offset),
    _size(size) {
}

void LocationResult::setFileResource(FileResource resource) {
    if (_fileResource) {
        throw std::logic_error("LocationResult file resource can only be assigned once");
    }
    _fileResource = std::move(resource);
}

const FileResource &LocationResult::asFileResource() const {
    if (!_fileResource) {
        throw std::logic_error("LocationResult does not have file resource metadata");
    }
    return *_fileResource;
}

const resource::ResourceId &LocationResult::id() const {
    return asFileResource().id();
}

ByteBuffer LocationResult::readData() const {
    if (_fileResource) {
        return _fileResource->readData();
    }
    auto stream = openGameInputStream(_filepath);
    stream->seek(static_cast<int64_t>(_offset), SeekOrigin::Begin);
    ByteBuffer buf;
    buf.resize(_size);
    if (_size > 0) {
        stream->read(buf.data(), buf.size());
    }
    return buf;
}

} // namespace extract

} // namespace reone
