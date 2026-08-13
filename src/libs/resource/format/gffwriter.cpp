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

#include "reone/resource/format/gffwriter.h"

#include "reone/resource/gff.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stream/memoryoutput.h"

namespace reone {

namespace resource {

enum class FieldClassification {
    Simple,
    Complex,
    Struct,
    List
};

static const std::unordered_map<ResType, std::string> g_signatures {
    {ResType::Res, "RES "}, {ResType::Are, "ARE "}, {ResType::Dlg, "DLG "},
    {ResType::Fac, "FAC "}, {ResType::Git, "GIT "}, {ResType::Gui, "GUI "},
    {ResType::Ifo, "IFO "}, {ResType::Jrl, "JRL "}, {ResType::Utc, "UTC "},
    {ResType::Utd, "UTD "}, {ResType::Ute, "UTE "}, {ResType::Uti, "UTI "},
    {ResType::Utm, "UTM "}, {ResType::Utp, "UTP "}, {ResType::Uts, "UTS "},
    {ResType::Utt, "UTT "}, {ResType::Utw, "UTW "}, {ResType::Pth, "PTH "}};

static uint32_t checkedUint32(uint64_t value, const std::string &what) {
    if (value > std::numeric_limits<uint32_t>::max()) {
        throw ValidationException(what + " exceeds GFF format capacity");
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

GffFileFormat::GffFileFormat(std::string signature, std::string version) :
    _signature(std::move(signature)),
    _version(std::move(version)) {
    if (_signature.size() != 4 ||
        !std::all_of(_signature.begin(), _signature.end(), [](unsigned char ch) {
            return ch >= 0x20 && ch <= 0x7e;
        })) {
        throw ValidationException("GFF signature must be exactly four printable bytes");
    }
    if (_version != "V3.2") {
        throw ValidationException("Unsupported GFF version: " + _version);
    }
}

GffWriter::GffWriter(ResType resType, const Gff &root) :
    _format([resType]() {
        auto signature = g_signatures.find(resType);
        if (signature == g_signatures.end()) {
            throw ValidationException(
                "Unsupported GFF resource type: " +
                std::to_string(static_cast<int>(resType)));
        }
        return GffFileFormat::v32(signature->second);
    }()),
    _root(root) {
}

void GffWriter::save(const std::filesystem::path &path) {
    ByteBuffer bytes = toBytes();
    auto out = FileOutputStream(path);
    if (!bytes.empty()) {
        out.writeAll(bytes.data(), bytes.size());
    }
}

void GffWriter::save(IOutputStream &out) {
    _context = WriteContext();
    processTree();

    _writer = std::make_unique<BinaryWriter>(out);

    writeHeader();
    writeStructArray();
    writeFieldArray();
    writeLabelArray();
    writeFieldData();
    writeFieldIndices();
    writeListIndices();
}

ByteBuffer GffWriter::toBytes() {
    ByteBuffer result;
    MemoryOutputStream out(result);
    save(out);
    return result;
}

static FieldClassification getFieldData(const Gff::Field &field, uint32_t &simple, ByteBuffer &complex) {
    switch (field.type) {
    case Gff::FieldType::Byte:
    case Gff::FieldType::Word:
    case Gff::FieldType::Dword:
        simple = field.uintValue;
        return FieldClassification::Simple;

    case Gff::FieldType::Char:
    case Gff::FieldType::Short:
    case Gff::FieldType::Int:
        simple = *reinterpret_cast<const uint32_t *>(&field.intValue);
        return FieldClassification::Simple;

    case Gff::FieldType::Dword64:
        complex.resize(8);
        memcpy(&complex[0], &field.uint64Value, 8);
        return FieldClassification::Complex;

    case Gff::FieldType::Int64:
        complex.resize(8);
        memcpy(&complex[0], &field.int64Value, 8);
        return FieldClassification::Complex;

    case Gff::FieldType::Float:
        simple = *reinterpret_cast<const uint32_t *>(&field.floatValue);
        return FieldClassification::Simple;

    case Gff::FieldType::Double:
        complex.resize(8);
        memcpy(&complex[0], &field.doubleValue, sizeof(double));
        return FieldClassification::Complex;

    case Gff::FieldType::CExoString: {
        uint32_t length = checkedUint32(field.strValue.size(), "CExoString length");
        uint32_t recordSize = checkedUint32(
            checkedAdd(4, length, "CExoString record size"),
            "CExoString record size");
        complex.resize(recordSize);
        memcpy(&complex[0], &length, 4);
        if (length > 0) {
            memcpy(&complex[4], field.strValue.data(), length);
        }
        return FieldClassification::Complex;
    }
    case Gff::FieldType::ResRef: {
        if (field.strValue.size() > std::numeric_limits<uint8_t>::max()) {
            throw ValidationException("GFF ResRef exceeds 255-byte format capacity");
        }
        uint32_t length = static_cast<uint32_t>(field.strValue.size());
        complex.resize(1ll + length);
        complex[0] = static_cast<char>(length);
        if (length > 0) {
            memcpy(&complex[1], field.strValue.data(), length);
        }
        return FieldClassification::Complex;
    }
    case Gff::FieldType::CExoLocString: {
        std::vector<Gff::LocSubstring> substrings = field.locSubstrings;
        if (substrings.empty() && !field.strValue.empty()) {
            substrings.push_back(Gff::LocSubstring {0, field.strValue});
        }
        std::sort(substrings.begin(), substrings.end(), [](const auto &lhs, const auto &rhs) {
            return lhs.id < rhs.id;
        });
        for (size_t i = 1; i < substrings.size(); ++i) {
            if (substrings[i - 1].id == substrings[i].id) {
                throw ValidationException("Duplicate CExoLocString substring ID");
            }
        }

        uint32_t numSubstrings = checkedUint32(substrings.size(), "CExoLocString substring count");
        uint64_t totalSize64 = 8;
        for (const auto &substring : substrings) {
            totalSize64 = checkedAdd(totalSize64, 8, "CExoLocString size");
            totalSize64 = checkedAdd(totalSize64, substring.text.size(), "CExoLocString size");
        }
        uint32_t totalSize = checkedUint32(totalSize64, "CExoLocString size");
        uint32_t recordSize = checkedUint32(
            checkedAdd(4, totalSize64, "CExoLocString record size"),
            "CExoLocString record size");
        complex.resize(recordSize);
        memcpy(&complex[0], &totalSize, 4);
        memcpy(&complex[4], &field.intValue, 4);
        memcpy(&complex[8], &numSubstrings, 4);
        size_t offset = 12;
        for (const auto &substring : substrings) {
            uint32_t length = checkedUint32(substring.text.size(), "CExoLocString substring length");
            memcpy(&complex[offset], &substring.id, 4);
            memcpy(&complex[offset + 4], &length, 4);
            if (length > 0) {
                memcpy(&complex[offset + 8], substring.text.data(), length);
            }
            offset += 8ull + length;
        }
        return FieldClassification::Complex;
    }
    case Gff::FieldType::Void: {
        uint32_t dataSize = checkedUint32(field.data.size(), "Void payload size");
        uint32_t recordSize = checkedUint32(
            checkedAdd(4, dataSize, "Void record size"),
            "Void record size");
        complex.resize(recordSize);
        memcpy(&complex[0], &dataSize, 4);
        if (dataSize > 0) {
            memcpy(&complex[4], field.data.data(), dataSize);
        }
        return FieldClassification::Complex;
    }
    case Gff::FieldType::Struct:
        return FieldClassification::Struct;

    case Gff::FieldType::List:
        return FieldClassification::List;

    case Gff::FieldType::Orientation:
        complex.resize(16);
        memcpy(&complex[0], &field.quatValue.w, 4);
        memcpy(&complex[4], &field.quatValue.x, 4);
        memcpy(&complex[8], &field.quatValue.y, 4);
        memcpy(&complex[12], &field.quatValue.z, 4);
        return FieldClassification::Complex;

    case Gff::FieldType::Vector:
        complex.resize(12);
        memcpy(&complex[0], &field.vecValue[0], 12);
        return FieldClassification::Complex;

    case Gff::FieldType::StrRef: {
        uint32_t totalSize = 4;
        complex.resize(8);
        memcpy(&complex[0], &totalSize, 4);
        memcpy(&complex[4], &field.intValue, 4);
        return FieldClassification::Complex;
    }
    default:
        throw ValidationException("Unsupported field type: " + std::to_string(static_cast<int>(field.type)));
    }
}

void GffWriter::processTree() {
    std::queue<const Gff *> aQueue;
    aQueue.push(&_root);
    std::unordered_set<const Gff *> scheduledStructs {&_root};

    uint64_t nextStructIdx = 0;

    while (!aQueue.empty()) {
        const Gff &aStruct = *aQueue.front();
        aQueue.pop();

        std::vector<uint32_t> fieldIndices;

        checkedUint32(aStruct.fields().size(), "GFF struct field count");
        for (const auto &field : aStruct.fields()) {
            // Current number of fields is a new field index
            fieldIndices.push_back(checkedUint32(_context.fields.size(), "GFF field index"));

            if (field.label.size() > 16 || field.label.find('\0') != std::string::npos) {
                throw ValidationException("GFF field label must be at most 16 non-NUL bytes");
            }

            // Append or use existing field label
            uint32_t labelIdx;
            auto maybeLabel = find(_context.labels.begin(), _context.labels.end(), field.label);
            if (maybeLabel != _context.labels.end()) {
                labelIdx = checkedUint32(
                    distance(_context.labels.begin(), maybeLabel),
                    "GFF label index");
            } else {
                labelIdx = checkedUint32(_context.labels.size(), "GFF label index");
                _context.labels.push_back(field.label);
            }

            // Retrieve and save field data
            uint32_t dataOrDataOffset;
            ByteBuffer complexData;
            FieldClassification fieldClass = getFieldData(field, dataOrDataOffset, complexData);
            switch (fieldClass) {
            case FieldClassification::Complex:
                dataOrDataOffset = checkedUint32(_context.fieldData.size(), "GFF field-data offset");
                checkedUint32(
                    checkedAdd(_context.fieldData.size(), complexData.size(), "GFF field-data size"),
                    "GFF field-data size");
                copy(complexData.begin(), complexData.end(), back_inserter(_context.fieldData));
                break;
            case FieldClassification::Struct:
                if (field.children.size() != 1 || !field.children.front()) {
                    throw ValidationException("GFF struct field must reference exactly one struct");
                }
                if (!scheduledStructs.insert(field.children.front().get()).second) {
                    throw ValidationException("GFF struct graph must be a tree");
                }
                // Set data offset to the next struct index
                dataOrDataOffset = checkedUint32(++nextStructIdx, "GFF struct index");
                aQueue.push(field.children.front().get());
                break;
            case FieldClassification::List:
                // Set data offset to the current size of the list indices array
                dataOrDataOffset = checkedUint32(
                    checkedMultiply(4, _context.listIndices.size(), "GFF list-index offset"),
                    "GFF list-index offset");
                checkedUint32(
                    checkedAdd(_context.listIndices.size(), field.children.size() + 1ull, "GFF list-index count"),
                    "GFF list-index count");
                _context.listIndices.push_back(
                    checkedUint32(field.children.size(), "GFF list count"));
                for (const auto &child : field.children) {
                    if (!child) {
                        throw ValidationException("GFF list contains a null struct reference");
                    }
                    if (!scheduledStructs.insert(child.get()).second) {
                        throw ValidationException("GFF struct graph must be a tree");
                    }
                    _context.listIndices.push_back(
                        checkedUint32(++nextStructIdx, "GFF struct index"));
                    aQueue.push(child.get());
                }
                break;
            default:
                break;
            }

            // Save field
            WriteField writeField;
            writeField.type = static_cast<uint32_t>(field.type);
            writeField.labelIndex = labelIdx;
            writeField.dataOrDataOffset = dataOrDataOffset;
            _context.fields.push_back(std::move(writeField));
        }

        uint32_t dataOrDataOffset;
        if (fieldIndices.size() == 1ll) {
            dataOrDataOffset = fieldIndices[0];
        } else {
            dataOrDataOffset = checkedUint32(
                checkedMultiply(4, _context.fieldIndices.size(), "GFF field-index offset"),
                "GFF field-index offset");
            checkedUint32(
                checkedAdd(_context.fieldIndices.size(), fieldIndices.size(), "GFF field-index count"),
                "GFF field-index count");
            copy(fieldIndices.begin(), fieldIndices.end(), back_inserter(_context.fieldIndices));
        }

        WriteStruct writeStruct;
        writeStruct.type = aStruct.type();
        writeStruct.dataOrDataOffset = dataOrDataOffset;
        writeStruct.fieldCount = checkedUint32(aStruct.fields().size(), "GFF struct field count");
        _context.structs.push_back(std::move(writeStruct));
    }
}

void GffWriter::writeHeader() {
    uint32_t numStructs = checkedUint32(_context.structs.size(), "GFF struct count");
    uint32_t numFields = checkedUint32(_context.fields.size(), "GFF field count");
    uint32_t numLabels = checkedUint32(_context.labels.size(), "GFF label count");

    uint32_t sizeStructs = checkedUint32(
        checkedMultiply(numStructs, 12, "GFF struct table size"),
        "GFF struct table size");
    uint32_t sizeFields = checkedUint32(
        checkedMultiply(numFields, 12, "GFF field table size"),
        "GFF field table size");
    uint32_t sizeLabels = checkedUint32(
        checkedMultiply(numLabels, 16, "GFF label table size"),
        "GFF label table size");
    uint32_t sizeFieldData = checkedUint32(_context.fieldData.size(), "GFF field-data size");
    uint32_t sizeFieldIndices = checkedUint32(
        checkedMultiply(_context.fieldIndices.size(), 4, "GFF field-index table size"),
        "GFF field-index table size");
    uint32_t sizeListIndices = checkedUint32(
        checkedMultiply(_context.listIndices.size(), 4, "GFF list-index table size"),
        "GFF list-index table size");

    uint32_t offStructs = 0x38;
    uint32_t offFields = checkedUint32(
        checkedAdd(offStructs, sizeStructs, "GFF field table offset"),
        "GFF field table offset");
    uint32_t offLabels = checkedUint32(
        checkedAdd(offFields, sizeFields, "GFF label table offset"),
        "GFF label table offset");
    uint32_t offFieldData = checkedUint32(
        checkedAdd(offLabels, sizeLabels, "GFF field-data offset"),
        "GFF field-data offset");
    uint32_t offFieldIndices = checkedUint32(
        checkedAdd(offFieldData, sizeFieldData, "GFF field-index table offset"),
        "GFF field-index table offset");
    uint32_t offListIndices = checkedUint32(
        checkedAdd(offFieldIndices, sizeFieldIndices, "GFF list-index table offset"),
        "GFF list-index table offset");
    checkedUint32(
        checkedAdd(offListIndices, sizeListIndices, "GFF total output size"),
        "GFF total output size");

    _writer->writeString(_format.signature());
    _writer->writeString(_format.version());
    _writer->writeUint32(offStructs);       // struct array offset
    _writer->writeUint32(numStructs);       // number of structs
    _writer->writeUint32(offFields);        // field array offset
    _writer->writeUint32(numFields);        // number of fields
    _writer->writeUint32(offLabels);        // label array offset
    _writer->writeUint32(numLabels);        // number of labels
    _writer->writeUint32(offFieldData);     // field data array offset
    _writer->writeUint32(sizeFieldData);    // number of bytes in field data
    _writer->writeUint32(offFieldIndices);  // field indices array offset
    _writer->writeUint32(sizeFieldIndices); // number of bytes in field indices array
    _writer->writeUint32(offListIndices);   // list indices array offset
    _writer->writeUint32(sizeListIndices);  // number of bytes in list indices array
}

void GffWriter::writeStructArray() {
    for (auto &writeStruct : _context.structs) {
        _writer->writeUint32(writeStruct.type);
        _writer->writeUint32(writeStruct.dataOrDataOffset);
        _writer->writeUint32(writeStruct.fieldCount);
    }
}

void GffWriter::writeFieldArray() {
    for (auto &field : _context.fields) {
        _writer->writeUint32(field.type);
        _writer->writeUint32(field.labelIndex);
        _writer->writeUint32(field.dataOrDataOffset);
    }
}

void GffWriter::writeLabelArray() {
    for (const auto &label : _context.labels) {
        std::string tmp(16, '\0');
        std::copy(label.begin(), label.end(), tmp.begin());
        _writer->writeString(tmp);
    }
}

void GffWriter::writeFieldData() {
    if (!_context.fieldData.empty()) {
        _writer->write(_context.fieldData);
    }
}

void GffWriter::writeFieldIndices() {
    for (auto &index : _context.fieldIndices) {
        _writer->writeUint32(index);
    }
}

void GffWriter::writeListIndices() {
    for (auto &index : _context.listIndices) {
        _writer->writeUint32(index);
    }
}

} // namespace resource

} // namespace reone
