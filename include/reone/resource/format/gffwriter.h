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

#include "reone/system/binarywriter.h"
#include "reone/system/types.h"

#include "../types.h"

namespace reone {

class IOutputStream;

namespace resource {

class Gff;

/** Semantic GFF header, independent of the resource extension. */
class GffFileFormat {
public:
    GffFileFormat(std::string signature, std::string version);

    static GffFileFormat v32(std::string signature) {
        return GffFileFormat(std::move(signature), "V3.2");
    }

    const std::string &signature() const { return _signature; }
    const std::string &version() const { return _version; }

private:
    std::string _signature;
    std::string _version;
};

class GffWriter {
public:
    /** Compatibility constructor for ordinary extension-typed GFFs. */
    GffWriter(ResType resType, const Gff &root);

    GffWriter(GffFileFormat format, const Gff &root) :
        _format(std::move(format)),
        _root(root) {}

    void save(const std::filesystem::path &path);
    void save(IOutputStream &out);
    ByteBuffer toBytes();

private:
    struct WriteStruct {
        uint32_t type {0};
        uint32_t dataOrDataOffset {0};
        uint32_t fieldCount {0};
    };

    struct WriteField {
        uint32_t type {0};
        uint32_t labelIndex {0};
        uint32_t dataOrDataOffset {0};
    };

    struct WriteContext {
        std::vector<WriteStruct> structs;
        std::vector<WriteField> fields;
        std::vector<std::string> labels;
        ByteBuffer fieldData;
        std::vector<uint32_t> fieldIndices;
        std::vector<uint32_t> listIndices;
    };

    GffFileFormat _format;
    const Gff &_root;

    WriteContext _context;
    std::unique_ptr<BinaryWriter> _writer;

    void processTree();

    void writeHeader();
    void writeStructArray();
    void writeFieldArray();
    void writeLabelArray();
    void writeFieldData();
    void writeFieldIndices();
    void writeListIndices();
};

} // namespace resource

} // namespace reone
