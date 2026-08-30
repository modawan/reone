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

#include "reone/script/enginetype.h"

#include "runtimeref.h"

namespace reone {

namespace game {

class Object;

class Event : public script::EngineType {
public:
    Event(int number) :
        _number(number) {
    }
    Event(
        int number,
        std::vector<int32_t> integers,
        std::vector<float> floats,
        std::vector<std::string> strings,
        std::vector<std::shared_ptr<Object>> objects);

    int number() const { return _number; }
    const std::vector<int32_t> &integers() const { return _integers; }
    const std::vector<float> &floats() const { return _floats; }
    const std::vector<std::string> &strings() const { return _strings; }
    std::vector<uint32_t> objects() const;

private:
    int _number;
    std::vector<int32_t> _integers;
    std::vector<float> _floats;
    std::vector<std::string> _strings;
    std::vector<RuntimeObjectRef<Object>> _objects;
};

} // namespace game

} // namespace reone
