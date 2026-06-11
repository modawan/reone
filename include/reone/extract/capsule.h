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

#pragma once

#include "fileresource.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace reone {

namespace extract {

/// Lazy ERF/MOD/RIM/SAV index (PyKotor LazyCapsule).
class LazyCapsule {
public:
    explicit LazyCapsule(std::filesystem::path path);

    const std::filesystem::path &path() const { return _path; }
    const std::vector<FileResource> &resources() const;

    std::optional<FileResource> find(const resource::ResourceId &id) const;

private:
    std::filesystem::path _path;
    mutable std::vector<FileResource> _resources;
    mutable bool _loaded {false};

    void ensureLoaded() const;
};

bool isCapsuleFile(const std::filesystem::path &path);
bool isModFile(const std::filesystem::path &path);
bool isErfFile(const std::filesystem::path &path);

} // namespace extract

} // namespace reone
