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

#include "types.h"

namespace reone {

namespace game {

class Talent : public script::EngineType {
public:
    Talent(
        TalentType type,
        int value,
        uint8_t multiClass = 0,
        uint32_t item = 0x7f000000,
        int itemPropertyIndex = -1,
        uint8_t casterLevel = 0xff,
        uint8_t metaType = 0xff) :
        _type(type),
        _value(value),
        _multiClass(multiClass),
        _item(item),
        _itemPropertyIndex(itemPropertyIndex),
        _casterLevel(casterLevel),
        _metaType(metaType) {
    }

    TalentType type() const { return _type; }
    int value() const { return _value; }
    uint8_t multiClass() const { return _multiClass; }
    uint32_t item() const { return _item; }
    int itemPropertyIndex() const { return _itemPropertyIndex; }
    uint8_t casterLevel() const { return _casterLevel; }
    uint8_t metaType() const { return _metaType; }

private:
    TalentType _type;
    int _value;
    uint8_t _multiClass;
    uint32_t _item;
    int _itemPropertyIndex;
    uint8_t _casterLevel;
    uint8_t _metaType;
};

} // namespace game

} // namespace reone
