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

#include "reone/extract/chitin.h"

#include "reone/resource/format/bifreader.h"
#include "reone/resource/format/keyreader.h"
#include "reone/system/exception/endofstream.h"
#include "reone/system/exception/filenotfound.h"
#include "reone/system/fileutil.h"
#include "reone/system/stream/gameinput.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include "reone/system/stream/memoryinput.h"
#endif

namespace reone {

namespace extract {

#ifdef __EMSCRIPTEN__
namespace {

ByteBuffer getWebBifIndexBytes(const std::filesystem::path &path) {
    int len = EM_ASM_INT(
        {
            if (typeof Module.reoneWebGetBifIndexBytes !== "function") {
                return -1;
            }
            var bytes = Module.reoneWebGetBifIndexBytes(UTF8ToString($0));
            return bytes ? bytes.length | 0 : -1;
        },
        path.generic_string().c_str());
    if (len <= 0) {
        return ByteBuffer();
    }
    ByteBuffer buf;
    buf.resize(static_cast<size_t>(len));
    EM_ASM(
        {
            var bytes = Module.reoneWebGetBifIndexBytes(UTF8ToString($0));
            if (bytes) {
                HEAPU8.set(bytes.subarray(0, $2), $1);
            }
        },
        path.generic_string().c_str(),
        buf.data(),
        len);
    return buf;
}

} // namespace
#endif

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
        std::filesystem::path bifPath;
        try {
            bifPath = getFileIgnoreCase(gamePath, file.filename);
        } catch (const FileNotFoundException &) {
            continue;
        }

        std::vector<resource::BifReader::ResourceEntry> bifResources;
#ifdef __EMSCRIPTEN__
        ByteBuffer webIndex = getWebBifIndexBytes(bifPath);
        if (!webIndex.empty()) {
            MemoryInputStream indexStream(webIndex);
            resource::BifReader indexReader(indexStream);
            try {
                indexReader.load();
            } catch (const EndOfStreamException &) {
                throw std::runtime_error("EOF reading BIF index: " + bifPath.string());
            }
            bifResources = indexReader.resources();
        } else
#endif
        {
            auto bifStream = openGameInputStream(bifPath);
            resource::BifReader bifReader(*bifStream);
            try {
                bifReader.load();
            } catch (const EndOfStreamException &) {
                throw std::runtime_error("EOF reading BIF: " + bifPath.string());
            }
            bifResources = bifReader.resources();
        }

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
