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

#include "reone/extract/chitin.h"

#include "reone/resource/format/bifreader.h"
#include "reone/resource/format/keyreader.h"
#include "reone/system/exception/endofstream.h"
#include "reone/system/exception/filenotfound.h"
#include "reone/system/fileutil.h"
#include "reone/system/stream/gameinput.h"

namespace reone {

namespace extract {

Chitin::Chitin(std::filesystem::path keyPath) {
    if (!std::filesystem::exists(keyPath)) {
        return;
    }

    auto keyStream = openGameInputStream(keyPath);
    resource::KeyReader keyReader(*keyStream);
    try {
        keyReader.load();
    } catch (const EndOfStreamException &) {
        throw std::runtime_error("EOF reading KEY: " + keyPath.string());
    }

    auto gamePath = keyPath.parent_path();
    std::unordered_map<int, std::vector<const resource::KeyReader::KeyEntry *>> bifIdxToKey;
    for (auto &key : keyReader.keys()) {
        bifIdxToKey[key.bifIdx].push_back(&key);
    }

    for (size_t i = 0; i < keyReader.files().size(); ++i) {
        auto &file = keyReader.files()[i];

        // KEY files store BIF paths with backslash separators.
        auto bifRelPath = boost::replace_all_copy(file.filename, "\\", "/");

        std::filesystem::path bifPath;
        try {
            bifPath = getFileIgnoreCase(gamePath, bifRelPath);
        } catch (const FileNotFoundException &) {
            continue;
        }

        auto bifStream = openGameInputStream(bifPath);
        resource::BifReader bifReader(*bifStream);
        try {
            bifReader.load();
        } catch (const EndOfStreamException &) {
            throw std::runtime_error("EOF reading BIF: " + bifPath.string());
        }
        auto &bifResources = bifReader.resources();

        auto keysIt = bifIdxToKey.find(static_cast<int>(i));
        if (keysIt == bifIdxToKey.end()) {
            continue;
        }
        for (auto *key : keysIt->second) {
            if (key->resIdx < 0 || static_cast<size_t>(key->resIdx) >= bifResources.size()) {
                continue;
            }
            auto &bifResource = bifResources[key->resIdx];
            _resources.emplace_back(
                key->resId.resRef.value(),
                key->resId.type,
                bifResource.fileSize,
                bifResource.offset,
                bifPath);
        }
    }
}

} // namespace extract

} // namespace reone
