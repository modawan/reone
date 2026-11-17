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

#include "reone/resource/provider/walkmeshes.h"

#include "reone/graphics/format/bwmreader.h"
#include "reone/resource/resources.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone::graphics;

namespace reone {

namespace resource {

Walkmeshes::Walkmeshes(IResources &resources) :
    _resources(resources) {
}

void Walkmeshes::clear() {
    _cache.clear();
}

std::shared_ptr<Walkmesh> Walkmeshes::get(const std::string &resRef, ResType type) {
    ResourceId id(resRef, type);
    return _cache.get(id, [this, &id](ResourceReads &reads) -> std::shared_ptr<Walkmesh> {
        auto res = reads.find(_resources, id);
        if (!res) {
            return nullptr;
        }
        auto bwm = MemoryInputStream(res->data);
        auto reader = BwmReader(bwm);
        reader.load();
        return reader.walkmesh();
    }).value;
}

} // namespace resource

} // namespace reone
