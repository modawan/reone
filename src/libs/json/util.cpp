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

#include "reone/json/util.h"

namespace reone {
namespace json {

static void printRecursive(std::ostream &os, const boost::json::value &jv, std::string &indent) {
    switch (jv.kind()) {
    case boost::json::kind::object: {
        os << "{\n";
        indent.append(4, ' ');
        auto const &obj = jv.get_object();
        if (!obj.empty()) {
            auto it = obj.begin();
            for (;;) {
                os << indent << boost::json::serialize(it->key()) << " : ";
                printRecursive(os, it->value(), indent);
                if (++it == obj.end())
                    break;
                os << ",\n";
            }
        }
        os << "\n";
        indent.resize(indent.size() - 4);
        os << indent << "}";
        break;
    }

    case boost::json::kind::array: {
        os << "[\n";
        indent.append(4, ' ');
        auto const &arr = jv.get_array();
        if (!arr.empty()) {
            auto it = arr.begin();
            for (;;) {
                os << indent;
                printRecursive(os, *it, indent);
                if (++it == arr.end())
                    break;
                os << ",\n";
            }
        }
        os << "\n";
        indent.resize(indent.size() - 4);
        os << indent << "]";
        break;
    }
    case boost::json::kind::string: {
        os << boost::json::serialize(jv.get_string());
        break;
    }

    case boost::json::kind::uint64:
    case boost::json::kind::int64:
    case boost::json::kind::double_: {
        os << jv;
        break;
    }

    case boost::json::kind::bool_: {
        if (jv.get_bool())
            os << "true";
        else
            os << "false";
        break;
    }

    case boost::json::kind::null: {
        os << "null";
        break;
    }
    }

    if (indent.empty())
        os << "\n";
}

void print(std::ostream &os, const boost::json::value &jv, bool pretty) {
    if (pretty) {
        std::string indent;
        printRecursive(os, jv, indent);
        return;
    }
    os << boost::json::serialize(jv);
}

} // namespace json
} // namespace reone
