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

#pragma once

#include "replacements.h"
#include "resources.h"

namespace reone {

namespace resource {

class ReplacementResources : public IResources, boost::noncopyable {
public:
    ReplacementResources(std::unique_ptr<IResources> backend,
                         IResourceReplacements &replacements) :
        _backend(std::move(backend)),
        _replacements(replacements) {
    }

    void clear() override;
    void clearLocal() override;
    void clearSave() override;

    void addEXE(const std::filesystem::path &path) override;
    void addKEY(const std::filesystem::path &path) override;
    void addERF(const std::filesystem::path &path, ContainerKind kind = ContainerKind::Global) override;
    void addMemERF(ByteBuffer buffer, ContainerKind kind) override;
    void addRIM(const std::filesystem::path &path, ContainerKind kind = ContainerKind::Global) override;
    void addMemRIM(ByteBuffer buffer, ContainerKind kind = ContainerKind::Global) override;
    void addFolder(const std::filesystem::path &path, ContainerKind kind = ContainerKind::Global) override;

    Resource get(const ResourceId &id) override;
    std::optional<Resource> find(const ResourceId &id) override;

private:
    std::unique_ptr<IResources> _backend;
    IResourceReplacements &_replacements;
};

} // namespace resource

} // namespace reone
