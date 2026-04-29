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

#include "reone/resource/gff.h"
#include "reone/json/resource.h"
#include <boost/algorithm/hex.hpp>

using namespace reone::resource;

namespace reone {
namespace json {

static boost::json::object serializeGff(const Gff &gff);

static std::string serializeGffFieldType(Gff::FieldType type) {
    switch (type) {
    case Gff::FieldType::Byte:
        return "Byte";
    case Gff::FieldType::Char:
        return "Char";
    case Gff::FieldType::Word:
        return "Word";
    case Gff::FieldType::Short:
        return "Short";
    case Gff::FieldType::Dword:
        return "Dword";
    case Gff::FieldType::Int:
        return "Int";
    case Gff::FieldType::Dword64:
        return "Dword64";
    case Gff::FieldType::Int64:
        return "Int64";
    case Gff::FieldType::Float:
        return "Float";
    case Gff::FieldType::Double:
        return "Double";
    case Gff::FieldType::CExoString:
        return "CExoString";
    case Gff::FieldType::ResRef:
        return "ResRef";
    case Gff::FieldType::CExoLocString:
        return "CExoLocString";
    case Gff::FieldType::Void:
        return "Void";
    case Gff::FieldType::Struct:
        return "Struct";
    case Gff::FieldType::List:
        return "List";
    case Gff::FieldType::Orientation:
        return "Orientation";
    case Gff::FieldType::Vector:
        return "Vector";
    case Gff::FieldType::StrRef:
        return "StrRef";
    }
    assert(0 && "unhandled type");
    return "invalid";
}

static boost::json::value serializeGffFieldValue(const Gff::Field &field) {
    switch (field.type) {
    case Gff::FieldType::Char:
    case Gff::FieldType::Short:
    case Gff::FieldType::Int:
    case Gff::FieldType::StrRef:
        return field.intValue;

    case Gff::FieldType::Byte:
    case Gff::FieldType::Word:
    case Gff::FieldType::Dword:
        return field.uintValue;

    case Gff::FieldType::Int64:
        return field.int64Value;

    case Gff::FieldType::Dword64:
        return field.uint64Value;

    case Gff::FieldType::Float:
        return field.floatValue;

    case Gff::FieldType::Double:
        return field.doubleValue;

    case Gff::FieldType::CExoString:
    case Gff::FieldType::ResRef:
        return boost::json::string(field.strValue);

    case Gff::FieldType::CExoLocString:
        return boost::json::object({{"str", field.strValue}, {"loc", field.intValue}});
    case Gff::FieldType::Void: {
        boost::json::string str;
        boost::algorithm::hex_lower(field.data, std::back_inserter(str));
        return str;
    }
    case Gff::FieldType::Struct: {
        assert(!field.children.empty() && "invalid struct field");
        return serializeGff(*field.children.front());
    }
    case Gff::FieldType::List: {
        boost::json::array arr;
        for (const auto &child : field.children) {
            arr.push_back(serializeGff(*child));
        }
        return arr;
    }
    case Gff::FieldType::Orientation: {
        boost::json::array arr;
        arr.push_back(field.quatValue[0]);
        arr.push_back(field.quatValue[1]);
        arr.push_back(field.quatValue[2]);
        arr.push_back(field.quatValue[3]);
        return arr;
    }
    case Gff::FieldType::Vector:
        boost::json::array arr;
        arr.push_back(field.vecValue[0]);
        arr.push_back(field.vecValue[1]);
        arr.push_back(field.vecValue[2]);
        return arr;
    }
    assert(0 && "unhandled type");
    return nullptr;
}

static boost::json::object serializeGff(const Gff &gff) {
    boost::json::object tree;
    for (const auto &field : gff.fields()) {
        tree[field.label] = {{"type", serializeGffFieldType(field.type)}, {"value", serializeGffFieldValue(field)}};
    }
    return tree;
}

boost::json::object fromGff(const Gff &gff) {
    boost::json::object obj;
    if (const auto &signature = gff.signature()) {
        obj["signature"] = *signature;
    } else {
        obj["signature"] = nullptr;
    }

    obj["gff"] = serializeGff(gff);
    return obj;
}

} // namespace json
} // namespace reone
