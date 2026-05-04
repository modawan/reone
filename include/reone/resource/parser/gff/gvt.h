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

namespace reone {

namespace resource {

class Gff;

struct GVT {
    using Boolean = std::pair<std::string, bool>;
    using Number = std::pair<std::string, int>;
    using String = std::pair<std::string, std::string>;
    using Location = std::pair<std::string, std::pair<glm::vec3, glm::vec3>>;

    std::vector<Boolean> booleans;
    std::vector<Number> numbers;
    std::vector<String> strings;
    std::vector<Location> locations;
};

GVT parseGVT(const Gff &gff);

} // namespace resource
} // namespace reone
