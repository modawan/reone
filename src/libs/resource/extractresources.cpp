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

#include "reone/resource/extractresources.h"

#include "reone/extract/capsule.h"
#include "reone/extract/chitin.h"
#include "reone/extract/fileresource.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/pereader.h"
#include "reone/resource/format/rimreader.h"
#include "reone/system/stream/gameinput.h"
#include "reone/system/stream/memoryinput.h"

namespace reone {

namespace resource {

namespace {

auto holds(const ResourceId &id) {
    return [&id](const auto &source) { return source.holds(id); };
}

} // namespace

void ExtractResources::addKEY(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    auto index = std::make_shared<std::unordered_map<ResourceId, extract::FileResource>>();
    extract::Chitin chitin(path);
    for (auto &res : chitin.resources()) {
        index->emplace(res.id(), res);
    }
    addSource(
        ResourceOwner::Global, bucket, ResourceSourceForm::KeyBif, path,
        [index](const ResourceId &id) { return index->count(id) != 0; },
        [index](const ResourceId &id) { return index->at(id).readData(); });
}

/**
 * Mount a container from disk.
 *
 * The index is read before the source is registered, so an archive that cannot
 * be opened is rejected by the mount that took it on rather than by whichever
 * lookup happens to reach it first. Deferring that decision would make a
 * corrupt archive mount successfully and fail later, leaving a module that is
 * neither loaded nor failed; it is also the point the other backend has always
 * decided at, and one loading contract cannot be answered at two different
 * moments depending on which backend is in use.
 *
 * Payloads are not read here. What is validated is the container: its signature
 * and the table of what it holds. Resource bytes are still read one at a time,
 * on demand, exactly as before.
 */
void ExtractResources::addERF(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    addCapsule(path, owner, bucket, ResourceSourceForm::Erf);
}

void ExtractResources::addRIM(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    // LazyCapsule picks the reader by file extension; the director only mounts
    // RIMs from *.rim paths.
    addCapsule(path, owner, bucket, ResourceSourceForm::Rim);
}

void ExtractResources::addCapsule(const std::filesystem::path &path,
                                  ResourceOwner owner,
                                  std::optional<ResourceSourceBucket> bucket,
                                  ResourceSourceForm form) {
    auto capsule = std::make_shared<extract::LazyCapsule>(path);
    capsule->load();
    addSource(
        owner, bucket, form, path,
        [capsule](const ResourceId &id) { return capsule->find(id).has_value(); },
        [capsule](const ResourceId &id) { return capsule->find(id)->readData(); });
}

void ExtractResources::addMemArchive(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket, bool rim) {
    auto bytes = std::make_shared<ByteBuffer>(std::move(buffer));
    auto index = std::make_shared<std::unordered_map<ResourceId, std::pair<uint32_t, uint32_t>>>();

    // An entry whose payload lies outside the archive holds nothing, so a
    // lookup passes over it to the next source.
    auto indexEntry = [&bytes, &index](const ResourceId &id, uint32_t offset, uint32_t size) {
        if (offset + static_cast<size_t>(size) <= bytes->size()) {
            index->emplace(id, std::make_pair(offset, size));
        }
    };
    MemoryInputStream stream(*bytes);
    if (rim) {
        RimReader reader(stream);
        reader.load();
        for (auto &res : reader.resources()) {
            indexEntry(res.resId, res.offset, res.size);
        }
    } else {
        ErfReader reader(stream);
        reader.load();
        auto &keys = reader.keys();
        auto &resources = reader.resources();
        for (size_t i = 0; i < keys.size() && i < resources.size(); ++i) {
            indexEntry(keys[i].resId, resources[i].offset, resources[i].size);
        }
    }

    addSource(
        owner, bucket, rim ? ResourceSourceForm::Rim : ResourceSourceForm::Erf, {},
        [index](const ResourceId &id) { return index->count(id) != 0; },
        [bytes, index](const ResourceId &id) {
            auto [offset, size] = index->at(id);
            return ByteBuffer(bytes->begin() + offset, bytes->begin() + offset + size);
        });
}

void ExtractResources::addMemERF(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    addMemArchive(std::move(buffer), owner, bucket, false);
}

void ExtractResources::addMemRIM(ByteBuffer buffer, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    addMemArchive(std::move(buffer), owner, bucket, true);
}

void ExtractResources::addFolder(const std::filesystem::path &path, ResourceOwner owner, std::optional<ResourceSourceBucket> bucket) {
    auto index = std::make_shared<std::unordered_map<ResourceId, std::filesystem::path>>();
    if (std::filesystem::exists(path)) {
        for (auto &entry : std::filesystem::recursive_directory_iterator(path)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto id = extract::resourceIdFromPath(entry.path());
            if (id) {
                index->emplace(*id, entry.path());
            }
        }
    }
    addSource(
        owner, bucket, ResourceSourceForm::Folder, path,
        [index](const ResourceId &id) { return index->count(id) != 0; },
        [index](const ResourceId &id) {
            auto &file = index->at(id);
            auto stream = openGameInputStream(file);
            ByteBuffer buf;
            buf.resize(static_cast<size_t>(std::filesystem::file_size(file)));
            if (!buf.empty()) {
                stream->read(buf.data(), buf.size());
            }
            return buf;
        });
}

void ExtractResources::addEXE(const std::filesystem::path &path, std::optional<ResourceSourceBucket> bucket) {
    static const std::unordered_map<PEResType, ResType> kPEResTypeToResType {
        {PEResType::Cursor, ResType::Cursor},
        {PEResType::CursorGroup, ResType::CursorGroup},
    };

    auto index = std::make_shared<std::unordered_map<ResourceId, extract::FileResource>>();
    auto stream = openGameInputStream(path);
    PeReader reader(*stream);
    reader.load();
    for (auto &peRes : reader.resources()) {
        auto resType = kPEResTypeToResType.find(peRes.type);
        if (resType == kPEResTypeToResType.end()) {
            continue;
        }
        extract::FileResource res(
            std::to_string(peRes.name),
            resType->second,
            peRes.size,
            peRes.offset,
            path);
        index->emplace(res.id(), std::move(res));
    }
    addSource(
        ResourceOwner::Global, bucket, ResourceSourceForm::Executable, path,
        [index](const ResourceId &id) { return index->count(id) != 0; },
        [index](const ResourceId &id) { return index->at(id).readData(); });
}

Resource ExtractResources::get(const ResourceId &id) {
    auto data = find(id);
    if (!data) {
        throw ResourceNotFoundException(id.string());
    }
    return *data;
}

std::optional<Resource> ExtractResources::find(const ResourceId &id) {
    return findExcludingOwners(id, {});
}

std::optional<Resource> ExtractResources::findExcludingOwners(
    const ResourceId &id,
    const std::set<ResourceOwner> &excludedOwners) {
    auto source = _sources.resolve(holds(id), excludedOwners);
    if (!source) {
        return std::nullopt;
    }
    return Resource {source->read(id), SourceList::sourceRef(*source)};
}

std::optional<ResourceSourceRef> ExtractResources::locate(const ResourceId &id) const {
    auto source = _sources.resolve(holds(id));
    if (!source) {
        return std::nullopt;
    }
    return SourceList::sourceRef(*source);
}

std::vector<ResourceSourceRef> ExtractResources::explain(const ResourceId &id) const {
    std::vector<ResourceSourceRef> sources;
    for (auto source : _sources.candidates(holds(id))) {
        sources.push_back(SourceList::sourceRef(*source));
    }
    return sources;
}

} // namespace resource

} // namespace reone
