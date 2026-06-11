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
#include <vector>

namespace reone {

namespace extract {

/// KEY/BIF virtual resource index (PyKotor Chitin).
class Chitin {
public:
    explicit Chitin(std::filesystem::path keyPath);

    const std::vector<FileResource> &resources() const { return _resources; }

private:
    std::vector<FileResource> _resources;
};

} // namespace extract

} // namespace reone
