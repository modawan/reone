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

#include "reone/game/effect.h"

#include <atomic>

#include "reone/game/object/creature.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

static std::atomic<uint64_t> gNextEffectId {1};

static uint64_t nextEffectId() {
    return gNextEffectId.fetch_add(1, std::memory_order_relaxed);
}

Effect::Effect(
    EffectType type,
    EffectProvenance provenance) :
    _type(type),
    _provenance(std::move(provenance)) {

    if (_provenance.id == 0) {
        _provenance.id = nextEffectId();
    }
}

bool Effect::onApply(Object &) {
    debug("Unsupported effect type: " + std::to_string(static_cast<int>(_type)));
    return true;
}

void Effect::onRemove(Object &object) {
}

namespace {

bool supportsVersusQualifier(int nativeType) {
    switch (nativeType) {
    case 10: // Attack Increase
    case 11: // Attack Decrease
    case 13: // Damage Increase
    case 14: // Damage Decrease
    case 22: // Immunity
    case 26: // Saving Throw Increase
    case 27: // Saving Throw Decrease
    case 47: // Invisibility
    case 48: // AC Increase
    case 49: // AC Decrease
    case 55: // Skill Increase
    case 56: // Skill Decrease
    case 63: // Sanctuary
    case 76: // Concealment
        return true;
    default:
        return false;
    }
}

} // namespace

bool Effect::setVersusAlignment(int lawChaos, int goodEvil) {
    if (!supportsVersusQualifier(getNativeEffectType(*this))) {
        return false;
    }
    _provenance.versusLawChaos = lawChaos;
    _provenance.versusGoodEvil = goodEvil;
    return true;
}

bool Effect::setVersusRacialType(int racialType) {
    if (!supportsVersusQualifier(getNativeEffectType(*this))) {
        return false;
    }
    _provenance.versusRacialType = racialType;
    return true;
}

bool Effect::appliesVersus(const Creature *creature) const {
    bool hasRace = _provenance.versusRacialType &&
                   *_provenance.versusRacialType !=
                       static_cast<int>(RacialType::All);
    bool hasAlignment = _provenance.versusGoodEvil &&
                        *_provenance.versusGoodEvil !=
                            static_cast<int>(Alignment::All);
    if (!hasRace && !hasAlignment) {
        return true;
    }
    if (!creature) {
        return false;
    }

    if (hasRace &&
        *_provenance.versusRacialType !=
            static_cast<int>(creature->racialType())) {
        return false;
    }
    return !hasAlignment ||
           *_provenance.versusGoodEvil ==
               static_cast<int>(creature->alignment());
}

int getNativeEffectType(const Effect &effect) {
    if (effect.nativeType() >= 0) {
        return effect.nativeType();
    }

    switch (effect.type()) {
    case EffectType::DamageResistance:
        return 2;
    case EffectType::AttackIncrease:
        return 10;
    case EffectType::AttackDecrease:
        return 11;
    case EffectType::DamageReduction:
        return 12;
    case EffectType::DamageIncrease:
        return 13;
    case EffectType::DamageDecrease:
        return 14;
    case EffectType::DamageImmunityIncrease:
        return 16;
    case EffectType::DamageImmunityDecrease:
        return 17;
    case EffectType::Immunity:
        return 22;
    case EffectType::SavingThrowIncrease:
        return 26;
    case EffectType::SavingThrowDecrease:
        return 27;
    case EffectType::Invisibility:
        return 47;
    case EffectType::ACIncrease:
        return 48;
    case EffectType::ACDecrease:
        return 49;
    case EffectType::SkillIncrease:
        return 55;
    case EffectType::SkillDecrease:
        return 56;
    case EffectType::SeeInvisible:
        return 70;
    case EffectType::Ultravision:
        return 71;
    case EffectType::TrueSeeing:
        return 72;
    case EffectType::Sanctuary:
        return 63;
    case EffectType::Blindness:
        return 73;
    case EffectType::Concealment:
        return 76;
    case EffectType::BonusFeat:
        return 83;
    default:
        return -1;
    }
}

} // namespace game

} // namespace reone
