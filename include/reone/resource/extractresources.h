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

#include "resources.h"

namespace reone {

namespace resource {

/**
 * IResources backend that reads game data through the extract layer
 * primitives, while reproducing the exact lookup semantics of the legacy
 * container-based Resources: sources are searched newest-first, and
 * clearLocal/clearSave drop only sources mounted with the respective kind.
 *
 * This is a compatibility backend. Native extract::Installation search
 * orders are deliberately not used here; adopting them is a separate,
 * behavior-changing step.
 */
class ExtractResources : public IResources, boost::noncopyable {
public:
    void clear() override {
        _sources.clear();
    }

    void clearLocal() override {
        clearSome(ContainerKind::Local);
    }

    void clearSave() override {
        clearSome(ContainerKind::Save);
    }

    void addEXE(const std::filesystem::path &path) override;
    void addKEY(const std::filesystem::path &path) override;
    void addERF(const std::filesystem::path &path, ContainerKind kind = ContainerKind::Global) override;
    void addMemERF(ByteBuffer buffer, ContainerKind kind) override;
    void addRIM(const std::filesystem::path &path, ContainerKind kind = ContainerKind::Global) override;
    void addMemRIM(ByteBuffer buffer, ContainerKind kind = ContainerKind::Global) override;
    void addFolder(const std::filesystem::path &path, ContainerKind kind = ContainerKind::Global) override;

    Resource get(const ResourceId &id) override;
    std::optional<Resource> find(const ResourceId &id) override;

    size_t sourceCount() const { return _sources.size(); }

private:
    struct Source {
        ContainerKind kind;
        std::function<std::optional<ByteBuffer>(const ResourceId &)> find;
    };

    std::list<Source> _sources;

    void clearSome(ContainerKind kind) {
        auto toErase = std::remove_if(_sources.begin(), _sources.end(), [kind](auto &source) {
            return source.kind == kind;
        });
        _sources.erase(toErase, _sources.end());
    }

    void addSource(ContainerKind kind, std::function<std::optional<ByteBuffer>(const ResourceId &)> find) {
        _sources.push_front(Source {kind, std::move(find)});
    }

    void addMemArchive(ByteBuffer buffer, ContainerKind kind, bool rim);
};

} // namespace resource

} // namespace reone
