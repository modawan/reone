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

#include "reone/resource/container/keybif.h"

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

namespace resource {

#ifdef __EMSCRIPTEN__
namespace {

// Large BIFs (models.bif/sounds.bif) exceed the MEMFS full-cache cap, so JS preloads only their
// index table (header + variable resource table) into a JS-side cache. Pull those bytes synchronously
// here — during init(), after __wasm_call_ctors — to avoid the fragile Asyncify file-read path on the
// deep KEY/BIF init stack. Returns the index bytes, or empty when no cached table exists.
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

void KeyBifResourceContainer::init() {
    auto key = openGameInputStream(_keyPath);
    auto keyReader = KeyReader(*key);
    try {
        keyReader.load();
    } catch (const EndOfStreamException &) {
        throw std::runtime_error("EOF reading KEY: " + _keyPath.string());
    }

    auto gamePath = _keyPath.parent_path();
    auto &files = keyReader.files();

    std::unordered_map<int, std::vector<const KeyReader::KeyEntry *>> bifIdxToKey;
    for (auto &key : keyReader.keys()) {
        bifIdxToKey[key.bifIdx].push_back(&key);
    }

    for (auto i = 0; i < keyReader.files().size(); ++i) {
        auto &file = keyReader.files()[i];
        std::filesystem::path bifPath;
        try {
            bifPath = getFileIgnoreCase(gamePath, file.filename);
        } catch (const FileNotFoundException &) {
            throw std::runtime_error(
                "Missing BIF archive: " + file.filename + " (expected under " + gamePath.string() + ")");
        }
        auto bif = openGameInputStream(bifPath);
        std::vector<BifReader::ResourceEntry> bifResources;
#ifdef __EMSCRIPTEN__
        ByteBuffer webIndex = getWebBifIndexBytes(bifPath);
        if (!webIndex.empty()) {
            MemoryInputStream indexStream(webIndex);
            BifReader indexReader(indexStream);
            try {
                indexReader.load();
            } catch (const EndOfStreamException &) {
                throw std::runtime_error("EOF reading BIF index: " + bifPath.string());
            }
            bifResources = indexReader.resources();
        } else
#endif
        {
            BifReader bifReader(*bif);
            try {
                bifReader.load();
            } catch (const EndOfStreamException &) {
                throw std::runtime_error("EOF reading BIF: " + bifPath.string());
            }
            bifResources = bifReader.resources();
        }

        auto &keys = bifIdxToKey.at(i);

        for (auto &key : keys) {
            auto &bifResource = bifResources[key->resIdx];

            auto resource = Resource();
            resource.bifIdx = key->bifIdx;
            resource.bifOffset = bifResource.offset;
            resource.fileSize = bifResource.fileSize;

            _resourceIds.insert(key->resId);
            _idToResource.insert(std::make_pair(key->resId, std::move(resource)));
        }

        _bifs.push_back(std::move(bif));
    }
}

std::optional<ByteBuffer> KeyBifResourceContainer::findResourceData(const ResourceId &id) {
    auto it = _idToResource.find(id);
    if (it == _idToResource.end()) {
        return std::nullopt;
    }
    auto &resource = it->second;
    if (resource.fileSize == 0) {
        return ByteBuffer();
    }
    ByteBuffer buf;
    buf.resize(resource.fileSize);

    auto &bif = _bifs.at(resource.bifIdx);
    bif->seek(resource.bifOffset, SeekOrigin::Begin);
    bif->read(&buf[0], buf.size());

    return buf;
}

} // namespace resource

} // namespace reone
