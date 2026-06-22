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

#include "id.h"

#include "reone/extract/fileresource.h"
#include "reone/system/types.h"

namespace reone {

namespace resource {

struct Resource {
    ByteBuffer data;
};

struct ResourceResult {
    ResourceId id;
    ByteBuffer data;
    extract::LocationResult location;

    const ResourceId &identifier() const { return id; }
    const std::string &resName() const { return id.resRef.value(); }
    const std::string &resname() const { return resName(); }
    const ResRef &resRef() const { return id.resRef; }
    const ResRef &resref() const { return resRef(); }
    ResType type() const { return id.type; }
    ResType restype() const { return type(); }
    const std::filesystem::path &filepath() const { return location.filepath(); }
    uint32_t offset() const { return location.offset(); }
    uint32_t size() const { return location.size(); }
    const extract::FileResource &asFileResource() const { return location.asFileResource(); }
    Resource resource() const { return Resource {data}; }
    std::string decode() const { return std::string(data.begin(), data.end()); }
};

} // namespace resource

} // namespace reone
