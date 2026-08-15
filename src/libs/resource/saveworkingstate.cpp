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

#include "reone/resource/saveworkingstate.h"

#include <fstream>

#include "reone/system/exception/validation.h"

namespace reone {

namespace resource {

static ByteBuffer readDetachedArchive(const std::filesystem::path &archivePath) {
    std::ifstream stream(archivePath, std::ios::binary | std::ios::ate);
    if (!stream.is_open()) {
        throw ValidationException(
            "Unable to open save working-state archive: " + archivePath.string());
    }

    auto end = stream.tellg();
    if (end < 0) {
        throw ValidationException(
            "Unable to determine save working-state archive size: " +
            archivePath.string());
    }
    auto size = static_cast<uintmax_t>(end);
    ByteBuffer result;
    if (size > result.max_size()) {
        throw ValidationException(
            "Save working-state archive exceeds memory representation: " +
            archivePath.string());
    }
    result.resize(static_cast<size_t>(size));

    stream.seekg(0, std::ios::beg);
    if (!stream) {
        throw ValidationException(
            "Unable to seek save working-state archive: " + archivePath.string());
    }
    size_t offset = 0;
    while (offset < result.size()) {
        auto chunk = static_cast<std::streamsize>(std::min<size_t>(
            result.size() - offset,
            static_cast<size_t>(std::numeric_limits<std::streamsize>::max())));
        stream.read(result.data() + offset, chunk);
        if (stream.gcount() != chunk) {
            throw ValidationException(
                "Short read from save working-state archive: " +
                archivePath.string());
        }
        offset += static_cast<size_t>(chunk);
    }
    return result;
}

static std::unique_ptr<ErfResourceContainer> openDetachedArchive(
    const std::filesystem::path &archivePath) {
    auto bytes = readDetachedArchive(archivePath);
    auto archive = std::make_unique<ErfResourceContainer>(Storage(std::move(bytes)));
    archive->init();
    return archive;
}

SaveWorkingState::SaveWorkingState(const std::filesystem::path &archivePath) :
    _archive(openDetachedArchive(archivePath)) {
    _resourceIds = _archive->resourceIds();
}

SaveWorkingState::SaveWorkingState(
    std::shared_ptr<const SaveWorkingState> base,
    std::map<ResourceId, std::shared_ptr<const ByteBuffer>> replacements,
    std::unordered_set<ResourceId> tombstones) :
    _base(std::move(base)),
    _replacements(std::move(replacements)),
    _tombstones(std::move(tombstones)) {
    _resourceIds = _base->resourceIds();
    for (const auto &id : _tombstones) {
        _resourceIds.erase(id);
    }
    for (const auto &[id, data] : _replacements) {
        _resourceIds.insert(id);
    }
}

std::optional<Resource> SaveWorkingState::find(const ResourceId &id) const {
    auto replacement = _replacements.find(id);
    if (replacement != _replacements.end()) {
        return Resource {*replacement->second};
    }
    if (_tombstones.find(id) != _tombstones.end()) {
        return std::nullopt;
    }
    if (_base) {
        return _base->find(id);
    }

    auto data = _archive->findResourceData(id);
    if (!data) {
        return std::nullopt;
    }
    return Resource {std::move(*data)};
}

bool SaveWorkingState::contains(const ResourceId &id) const {
    return _resourceIds.find(id) != _resourceIds.end();
}

SaveResourceView::SaveResourceView(
    std::shared_ptr<const SaveWorkingState> base,
    ResourceId id) :
    _origin(SaveResourceOrigin::Borrowed),
    _base(std::move(base)),
    _id(std::move(id)) {
}

SaveResourceView::SaveResourceView(std::shared_ptr<const ByteBuffer> data) :
    _origin(SaveResourceOrigin::Owned),
    _data(std::move(data)) {
}

std::optional<Resource> SaveResourceView::read() const {
    if (_origin == SaveResourceOrigin::Owned) {
        return Resource {*_data};
    }
    return _base->find(_id);
}

SaveWorkingStateCandidate SaveWorkingStateCandidate::fromCommitted(
    std::shared_ptr<const SaveWorkingState> base) {
    if (!base) {
        throw std::invalid_argument("Save working-state candidate requires a base");
    }
    return SaveWorkingStateCandidate(std::move(base));
}

SaveWorkingStateCandidate::SaveWorkingStateCandidate(
    std::shared_ptr<const SaveWorkingState> base) :
    _base(std::move(base)) {
}

void SaveWorkingStateCandidate::put(ResourceId id, ByteBuffer payload) {
    _tombstones.erase(id);
    _replacements.insert_or_assign(
        std::move(id),
        std::make_shared<const ByteBuffer>(std::move(payload)));
}

void SaveWorkingStateCandidate::erase(const ResourceId &id) {
    _replacements.erase(id);
    _tombstones.insert(id);
}

std::optional<SaveResourceView> SaveWorkingStateCandidate::find(
    const ResourceId &id) const {
    auto replacement = _replacements.find(id);
    if (replacement != _replacements.end()) {
        return SaveResourceView(replacement->second);
    }
    if (_tombstones.find(id) != _tombstones.end() || !_base->contains(id)) {
        return std::nullopt;
    }
    return SaveResourceView(_base, id);
}

bool SaveWorkingStateCandidate::contains(const ResourceId &id) const {
    if (_replacements.find(id) != _replacements.end()) {
        return true;
    }
    if (_tombstones.find(id) != _tombstones.end()) {
        return false;
    }
    return _base->contains(id);
}

std::vector<ResourceId> SaveWorkingStateCandidate::deterministicResourceIds() const {
    std::set<ResourceId> ids(_base->resourceIds().begin(), _base->resourceIds().end());
    for (const auto &id : _tombstones) {
        ids.erase(id);
    }
    for (const auto &[id, data] : _replacements) {
        ids.insert(id);
    }
    return std::vector<ResourceId>(ids.begin(), ids.end());
}

void SaveWorkingStateCandidate::replaceModule(
    ResourceId savedArchiveId,
    ByteBuffer payload,
    std::optional<ResourceId> savedResourceImageId) {
    if (savedArchiveId.type != ResType::Sav) {
        throw std::invalid_argument("Module saved archive must have .sav type");
    }
    if (savedResourceImageId &&
        (savedResourceImageId->type != ResType::Rsv ||
         savedResourceImageId->resRef != savedArchiveId.resRef)) {
        throw std::invalid_argument(
            "Module saved resource image must be the matching .rsv identity");
    }

    put(savedArchiveId, std::move(payload));
    if (savedResourceImageId) {
        erase(*savedResourceImageId);
    }
    _moduleReplacements.push_back(
        ModuleReplacement {std::move(savedArchiveId), std::move(savedResourceImageId)});
}

SaveWorkingStateCandidateValidation SaveWorkingStateCandidate::validate(
    const Validator &additionalValidator) const {
    SaveWorkingStateCandidateValidation result;
    try {
        auto ids = deterministicResourceIds();
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
            result.addError("Final resource enumeration contains a duplicate identity");
        }

        std::unordered_set<ResourceId> finalIds(ids.begin(), ids.end());
        for (const auto &id : _tombstones) {
            if (finalIds.find(id) != finalIds.end()) {
                result.addError("Tombstoned resource remains visible: " + id.string());
            }
        }

        for (const auto &[id, data] : _replacements) {
            auto view = find(id);
            if (!view || view->origin() != SaveResourceOrigin::Owned) {
                result.addError("Replacement does not win lookup: " + id.string());
                continue;
            }
            auto resource = view->read();
            if (!resource || resource->data != *data) {
                result.addError("Replacement payload is not readable: " + id.string());
            }
        }

        for (const auto &id : ids) {
            auto view = find(id);
            if (!view || !view->read()) {
                result.addError("Final resource is not readable: " + id.string());
            }
        }

        for (const auto &module : _moduleReplacements) {
            if (!contains(module.savedArchiveId)) {
                result.addError(
                    "Replacement module archive is absent: " +
                    module.savedArchiveId.string());
            }
            if (module.savedResourceImageId && contains(*module.savedResourceImageId)) {
                result.addError(
                    "Stale module resource image remains visible: " +
                    module.savedResourceImageId->string());
            }
        }

        if (additionalValidator) {
            additionalValidator(*this, result);
        }
    } catch (const std::exception &e) {
        result.addError(std::string("Candidate validation failed: ") + e.what());
    }
    return result;
}

std::shared_ptr<const SaveWorkingState> SaveWorkingStateCandidate::freeze() const {
    auto validation = validate();
    if (!validation) {
        throw ValidationException(validation.errors.front());
    }
    return std::shared_ptr<const SaveWorkingState>(new SaveWorkingState(
        _base,
        _replacements,
        _tombstones));
}

SaveSessionState::SaveSessionState(SaveSlotDescriptor descriptor) :
    _slot(std::move(descriptor)),
    _metadata(_slot.directory),
    _workingState(_slot.archive) {
    _metadata.init();
}

std::optional<Resource> SaveSessionState::findMetadata(const ResourceId &id) {
    auto data = _metadata.findResourceData(id);
    if (!data) {
        return std::nullopt;
    }
    return Resource {std::move(*data)};
}

std::optional<Resource> SaveSessionState::findWorking(const ResourceId &id) {
    return _workingState.find(id);
}

} // namespace resource

} // namespace reone
