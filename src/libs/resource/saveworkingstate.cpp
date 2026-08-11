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

namespace reone {

namespace resource {

SaveWorkingState::SaveWorkingState(const std::filesystem::path &archivePath) :
    _archive(archivePath) {
    _archive.init();
}

std::optional<Resource> SaveWorkingState::find(const ResourceId &id) {
    auto data = _archive.findResourceData(id);
    if (!data) {
        return std::nullopt;
    }
    return Resource {std::move(*data)};
}

bool SaveWorkingState::contains(const ResourceId &id) const {
    return _archive.resourceIds().find(id) != _archive.resourceIds().end();
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
