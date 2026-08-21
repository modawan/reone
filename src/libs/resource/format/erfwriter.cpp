/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/resource/format/erfwriter.h"

#include "reone/system/binarywriter.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryoutput.h"

namespace reone {

namespace resource {

static constexpr uint64_t kHeaderSize = 0xa0;
static constexpr uint64_t kKeyStructSize = 24;
static constexpr uint64_t kResourceStructSize = 8;
static constexpr size_t kErfMaxResRefLength = 16;

struct MaterializedErfResource {
    std::string resRef;
    ResourceId id;
    ByteBuffer data;
};

static uint32_t checkedUint32(uint64_t value, const std::string &what) {
    if (value > std::numeric_limits<uint32_t>::max()) {
        throw ValidationException(what + " exceeds ERF format capacity");
    }
    return static_cast<uint32_t>(value);
}

static uint64_t checkedAdd(uint64_t lhs, uint64_t rhs, const std::string &what) {
    if (rhs > std::numeric_limits<uint64_t>::max() - lhs) {
        throw ValidationException(what + " overflows");
    }
    return lhs + rhs;
}

static uint64_t checkedMultiply(uint64_t lhs, uint64_t rhs, const std::string &what) {
    if (lhs != 0 && rhs > std::numeric_limits<uint64_t>::max() / lhs) {
        throw ValidationException(what + " overflows");
    }
    return lhs * rhs;
}

static std::vector<MaterializedErfResource> materialize(
    const std::vector<ErfWriter::Resource> &resources) {
    checkedUint32(resources.size(), "ERF resource count");

    std::vector<MaterializedErfResource> result;
    result.reserve(resources.size());
    std::set<ResourceId> ids;
    for (const auto &resource : resources) {
        if (resource.resRef.empty() || resource.resRef.size() > kErfMaxResRefLength ||
            resource.resRef.find('\0') != std::string::npos) {
            throw ValidationException(
                "ERF ResRef must contain 1 to 16 non-NUL bytes: " +
                resource.resRef);
        }
        if (resource.resType == ResType::Invalid) {
            throw ValidationException("ERF resource has an invalid type: " + resource.resRef);
        }

        ResourceId id(resource.resRef, resource.resType);
        if (!ids.insert(id).second) {
            throw ValidationException("Duplicate ERF resource identity: " + id.string());
        }

        ByteBuffer data = resource.dataReader ? resource.dataReader() : resource.data;
        checkedUint32(data.size(), "ERF resource payload size");
        result.push_back(MaterializedErfResource {
            id.resRef.value(),
            std::move(id),
            std::move(data)});
    }

    std::sort(result.begin(), result.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.id < rhs.id;
    });
    return result;
}

void ErfWriter::add(Resource &&res) {
    _resources.push_back(std::move(res));
}

void ErfWriter::save(FileType type, const std::filesystem::path &path) {
    ByteBuffer bytes = toBytes(type);
    auto out = FileOutputStream(path);
    if (!bytes.empty()) {
        out.writeAll(bytes.data(), bytes.size());
    }
}

void ErfWriter::save(FileType type, IOutputStream &out) {
    ByteBuffer bytes = toBytes(type);
    if (!bytes.empty()) {
        out.writeAll(bytes.data(), bytes.size());
    }
}

ByteBuffer ErfWriter::toBytes(FileType type) const {
    auto resources = materialize(_resources);
    uint32_t numResources = checkedUint32(resources.size(), "ERF resource count");

    uint64_t offResources64 = checkedAdd(
        kHeaderSize,
        checkedMultiply(kKeyStructSize, numResources, "ERF key table size"),
        "ERF resource table offset");
    uint64_t payloadOffset64 = checkedAdd(
        offResources64,
        checkedMultiply(kResourceStructSize, numResources, "ERF resource table size"),
        "ERF payload offset");
    uint64_t totalSize = payloadOffset64;
    for (const auto &resource : resources) {
        totalSize = checkedAdd(totalSize, resource.data.size(), "ERF total output size");
    }
    checkedUint32(totalSize, "ERF total output size");

    ByteBuffer result;
    result.reserve(static_cast<size_t>(totalSize));
    MemoryOutputStream out(result);
    BinaryWriter writer(out);

    switch (type) {
    case FileType::MOD:
        writer.writeString("MOD V1.0");
        break;
    case FileType::ERF:
        writer.writeString("ERF V1.0");
        break;
    default:
        throw ValidationException("Unsupported ERF file type");
    }
    writer.writeUint32(0); // language count
    writer.writeUint32(0); // localized string size
    writer.writeUint32(numResources);
    writer.writeUint32(static_cast<uint32_t>(kHeaderSize)); // localized strings
    writer.writeUint32(static_cast<uint32_t>(kHeaderSize)); // key list
    writer.writeUint32(checkedUint32(offResources64, "ERF resource table offset"));
    writer.writeUint32(0); // deterministic build year since 1900
    writer.writeUint32(0); // deterministic build day since January 1st
    writer.writeInt32(-1); // description StrRef
    writer.write(116, 0);

    uint32_t resourceIndex = 0;
    for (const auto &resource : resources) {
        std::string resRef = resource.resRef;
        resRef.resize(kErfMaxResRefLength, '\0');
        writer.writeString(resRef);
        writer.writeUint32(resourceIndex++);
        writer.writeUint16(static_cast<uint16_t>(resource.id.type));
        writer.writeUint16(0);
    }

    uint64_t offset = payloadOffset64;
    for (const auto &resource : resources) {
        writer.writeUint32(checkedUint32(offset, "ERF payload offset"));
        writer.writeUint32(checkedUint32(resource.data.size(), "ERF payload size"));
        offset = checkedAdd(offset, resource.data.size(), "ERF payload end");
    }

    for (const auto &resource : resources) {
        if (!resource.data.empty()) {
            writer.write(resource.data);
        }
    }
    return result;
}

} // namespace resource

} // namespace reone
