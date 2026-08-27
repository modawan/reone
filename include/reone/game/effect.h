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

#include <cstdint>
#include <optional>

#include "reone/script/enginetype.h"
#include "reone/script/types.h"

#include "types.h"

namespace reone {

namespace game {

class Object;
class Creature;

enum class EffectLinkClass : uint8_t {
    None = 0x00,
    Creator = 0x08,
    Native10 = 0x10,
    ItemOnHit = 0x18,
};

struct EffectProvenance {
    uint64_t id {0};
    uint32_t creatorId {script::kObjectInvalid};
    uint32_t sourceItemId {script::kObjectInvalid};
    // Native CGameEffect SpellId field at +0x1c. The engine also uses this signed
    // value as the non-item grouping key; -1 means an independent entry and
    // zero is a valid shared spell/source value.
    int spellId {-1};
    EffectLinkClass linkClass {EffectLinkClass::None};
    int nativeType {-1};
    std::optional<int> versusRacialType;
    std::optional<int> versusLawChaos;
    // Target-specific consumers evaluate this qualifier through
    // Effect::appliesVersus().
    std::optional<int> versusGoodEvil;
};

class Effect : public script::EngineType {
public:
    explicit Effect(
        EffectType type,
        EffectProvenance provenance = {});

    // Validates and activates the effect. Returning false rejects it and
    // prevents a later onRemove call.
    virtual bool onApply(Object &object);
    virtual void onRemove(Object &object);

    EffectType type() const { return _type; }
    uint64_t id() const { return _provenance.id; }
    uint32_t creatorId() const { return _provenance.creatorId; }
    uint32_t sourceItemId() const { return _provenance.sourceItemId; }
    int spellId() const { return _provenance.spellId; }
    EffectLinkClass linkClass() const { return _provenance.linkClass; }
    int nativeType() const { return _provenance.nativeType; }
    const std::optional<int> &versusRacialType() const {
        return _provenance.versusRacialType;
    }
    const std::optional<int> &versusLawChaos() const {
        return _provenance.versusLawChaos;
    }
    const std::optional<int> &versusGoodEvil() const {
        return _provenance.versusGoodEvil;
    }
    const EffectProvenance &provenance() const { return _provenance; }

    void setCreatorId(uint32_t creatorId) { _provenance.creatorId = creatorId; }
    void setSourceItemId(uint32_t sourceItemId) {
        _provenance.sourceItemId = sourceItemId;
    }
    void setSpellId(int spellId) { _provenance.spellId = spellId; }
    void setLinkClass(EffectLinkClass linkClass) { _provenance.linkClass = linkClass; }
    void setNativeType(int nativeType) { _provenance.nativeType = nativeType; }

    bool setVersusAlignment(int lawChaos, int goodEvil);
    bool setVersusRacialType(int racialType);
    bool appliesVersus(const Creature *creature) const;

protected:
    EffectType _type;
    EffectProvenance _provenance;
};

/** Returns the native CGameEffect true type, or -1 when no mapping is defined. */
int getNativeEffectType(const Effect &effect);

} // namespace game

} // namespace reone
