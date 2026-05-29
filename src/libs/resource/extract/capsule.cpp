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

#include "reone/extract/capsule.h"

#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/rimreader.h"
#include "reone/system/exception/endofstream.h"
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

} // namespace

bool isCapsuleFile(const std::filesystem::path &path) {
    auto ext = extensionLower(path);
    return ext == "erf" || ext == "mod" || ext == "rim" || ext == "sav" || ext == "hak";
}

bool isModFile(const std::filesystem::path &path) {
    return extensionLower(path) == "mod";
}

bool isErfFile(const std::filesystem::path &path) {
    auto ext = extensionLower(path);
    return ext == "erf" || ext == "sav" || ext == "hak";
}

LazyCapsule::LazyCapsule(std::filesystem::path path) :
    _path(std::move(path)) {
}

const std::vector<FileResource> &LazyCapsule::resources() const {
    ensureLoaded();
    return _resources;
}

std::optional<FileResource> LazyCapsule::find(const resource::ResourceId &id) const {
    ensureLoaded();
    for (auto &res : _resources) {
        if (res.id() == id) {
            return res;
        }
    }
    return std::nullopt;
}

void LazyCapsule::ensureLoaded() const {
    if (_loaded) {
        return;
    }
    _loaded = true;
    if (!std::filesystem::exists(_path)) {
        return;
    }

    auto stream = openGameInputStream(_path);
    auto ext = extensionLower(_path);
    if (ext == "rim") {
        resource::RimReader reader(*stream);
        try {
            reader.load();
        } catch (const EndOfStreamException &) {
            throw std::runtime_error("EOF reading capsule: " + _path.string());
        }
        for (auto &entry : reader.resources()) {
            _resources.emplace_back(
                entry.resId.resRef.value(),
                entry.resId.type,
                entry.size,
                entry.offset,
                _path);
        }
        return;
    }

    resource::ErfReader reader(*stream);
    try {
        reader.load();
    } catch (const EndOfStreamException &) {
        throw std::runtime_error("EOF reading capsule: " + _path.string());
    }
    auto &keys = reader.keys();
    auto &resEntries = reader.resources();
    for (size_t i = 0; i < keys.size() && i < resEntries.size(); ++i) {
        auto &key = keys[i];
        auto &entry = resEntries[i];
        _resources.emplace_back(
            key.resId.resRef.value(),
            key.resId.type,
            entry.size,
            entry.offset,
            _path);
    }
}

} // namespace extract

} // namespace reone
