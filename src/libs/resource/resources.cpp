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

#include "reone/resource/resources.h"

#include "reone/extract/installation.h"
#include "reone/extract/finder.h"
#include "reone/resource/exception/notfound.h"

namespace reone {

namespace resource {

Resource Resources::get(const ResourceId &id) {
    auto data = find(id);
    if (!data) {
        throw ResourceNotFoundException(id.string());
    }
    return *data;
}

void Resources::useInstallation(extract::Installation *installation) {
    _installation = installation;
}

void Resources::setSearchOrder(extract::SearchScope order) {
    _searchOrder = std::move(order);
}

std::optional<Resource> Resources::find(const ResourceId &id) {
    return find(id, _searchOrder);
}

std::optional<Resource> Resources::find(const ResourceId &id, const extract::SearchScope &order) {
    if (!_installation) {
        return std::nullopt;
    }
    if (auto loc = _installation->resource(id, order)) {
        return Resource {loc->readData()};
    }
    return std::nullopt;
}

} // namespace resource

} // namespace reone
