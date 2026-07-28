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

#include "reone/resource/replacementresources.h"

#include "reone/resource/exception/notfound.h"

namespace reone {

namespace resource {

void ReplacementResources::clear() {
    _backend->clear();
}

void ReplacementResources::clearLocal() {
    _backend->clearLocal();
}

void ReplacementResources::clearSave() {
    _backend->clearSave();
}

void ReplacementResources::addEXE(const std::filesystem::path &path) {
    _backend->addEXE(path);
}

void ReplacementResources::addKEY(const std::filesystem::path &path) {
    _backend->addKEY(path);
}

void ReplacementResources::addERF(const std::filesystem::path &path, ContainerKind kind) {
    _backend->addERF(path, kind);
}

void ReplacementResources::addMemERF(ByteBuffer buffer, ContainerKind kind) {
    _backend->addMemERF(std::move(buffer), kind);
}

void ReplacementResources::addRIM(const std::filesystem::path &path, ContainerKind kind) {
    _backend->addRIM(path, kind);
}

void ReplacementResources::addMemRIM(ByteBuffer buffer, ContainerKind kind) {
    _backend->addMemRIM(std::move(buffer), kind);
}

void ReplacementResources::addFolder(const std::filesystem::path &path, ContainerKind kind) {
    _backend->addFolder(path, kind);
}

Resource ReplacementResources::get(const ResourceId &id) {
    auto data = find(id);
    if (!data) {
        throw ResourceNotFoundException(id.string());
    }
    return *data;
}

std::optional<Resource> ReplacementResources::find(const ResourceId &id) {
    auto replacement = _replacements.findResourceReplacement(id);
    return replacement ? replacement : _backend->find(id);
}

} // namespace resource

} // namespace reone
