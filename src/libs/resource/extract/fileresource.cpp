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

#include "reone/extract/fileresource.h"

#include "reone/resource/typeutil.h"
#include "reone/system/stream/gameinput.h"

#include <boost/algorithm/string/case_conv.hpp>

namespace reone {

namespace extract {

namespace {

std::string extensionLower(const std::filesystem::path &path) {
    auto ext = path.extension().string();
    if (!ext.empty() && ext[0] == '.') {
        ext.erase(0, 1);
    }
    boost::to_lower(ext);
    return ext;
}

bool isCapsuleExt(const std::string &ext) {
    return ext == "erf" || ext == "mod" || ext == "rim" || ext == "sav" || ext == "hak";
}

} // namespace

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

ByteBuffer FileResource::readData() const {
    auto stream = openGameInputStream(_filepath);
    stream->seek(static_cast<int>(_offset), SeekOrigin::Begin);
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

ByteBuffer LocationResult::readData() const {
    if (_fileResource) {
        return _fileResource->readData();
    }
    auto stream = openGameInputStream(_filepath);
    stream->seek(static_cast<int>(_offset), SeekOrigin::Begin);
    ByteBuffer buf;
    buf.resize(_size);
    if (_size > 0) {
        stream->read(buf.data(), buf.size());
    }
    return buf;
}

} // namespace extract

} // namespace reone
