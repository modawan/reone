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

#include "reone/game/event.h"

#include "reone/game/object.h"
#include "reone/script/types.h"

namespace reone {

namespace game {

Event::Event(
    int number,
    std::vector<int32_t> integers,
    std::vector<float> floats,
    std::vector<std::string> strings,
    std::vector<std::shared_ptr<Object>> objects) :
    _number(number),
    _integers(std::move(integers)),
    _floats(std::move(floats)),
    _strings(std::move(strings)) {
    _objects.reserve(objects.size());
    for (const auto &object : objects) {
        _objects.emplace_back(object);
    }
}

std::vector<uint32_t> Event::objects() const {
    std::vector<uint32_t> result;
    result.reserve(_objects.size());
    for (const auto &reference : _objects) {
        auto object = reference.resolve();
        result.push_back(object ? object->id() : script::kObjectInvalid);
    }
    return result;
}

} // namespace game

} // namespace reone
