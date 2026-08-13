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

#include <functional>

#include "reone/system/types.h"

#include "../id.h"

namespace reone {

class IOutputStream;

namespace resource {

class ErfWriter {
public:
    enum class FileType {
        ERF,
        MOD
    };

    struct Resource {
        using DataReader = std::function<ByteBuffer()>;

        std::string resRef;
        ResType resType {ResType::Invalid};
        ByteBuffer data;
        DataReader dataReader;

        Resource() = default;

        Resource(std::string resRef, ResType resType, ByteBuffer data) :
            resRef(std::move(resRef)),
            resType(resType),
            data(std::move(data)) {}

        Resource(ResourceId id, ByteBuffer data) :
            Resource(id.resRef.value(), id.type, std::move(data)) {}

        static Resource lazy(ResourceId id, DataReader reader) {
            Resource result;
            result.resRef = id.resRef.value();
            result.resType = id.type;
            result.dataReader = std::move(reader);
            return result;
        }
    };

    void add(Resource &&res);

    void save(FileType type, const std::filesystem::path &path);
    void save(FileType type, IOutputStream &out);
    ByteBuffer toBytes(FileType type) const;

private:
    std::vector<Resource> _resources;
};

} // namespace resource

} // namespace reone
