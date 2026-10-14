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

/**
 * A spell, feat or skill as scripts handle it, with the class or item it is
 * used through and its caster level and meta type.
 */
class Talent : public script::EngineType {
public:
    /** The meta type of a talent that has none. */
    static constexpr int kNoMetaType = 255;
    /** The type of the talent answered when there is none to offer. */
    static constexpr TalentType kNoType = static_cast<TalentType>(-1);

    Talent(
        TalentType type,
        int value,
        int castingClass = 0,
        uint32_t item = 0x7f000000,
        int itemPropertyIndex = -1,
        int casterLevel = kUnspecifiedCasterLevel,
        int metaType = kNoMetaType) :
        _type(type),
        _value(value),
        _castingClass(castingClass),
        _item(item),
        _itemPropertyIndex(itemPropertyIndex),
        _casterLevel(casterLevel),
        _metaType(metaType) {
    }

    TalentType type() const { return _type; }
    int value() const { return _value; }
    /** The class index the talent is used through, or a casting source kind (castingSourceKind). */
    int castingClass() const { return _castingClass; }
    uint32_t item() const { return _item; }
    int itemPropertyIndex() const { return _itemPropertyIndex; }
    int casterLevel() const { return _casterLevel; }
    int metaType() const { return _metaType; }

private:
    TalentType _type;
    int _value;
    int _castingClass;
    uint32_t _item;
    int _itemPropertyIndex;
    int _casterLevel;
    int _metaType;
};

} // namespace game

} // namespace reone
