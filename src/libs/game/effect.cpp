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

#include <algorithm>
#include <stdexcept>

#include "reone/game/combatfeedback.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/abilityincrease.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/areaofeffect.h"
#include "reone/game/effect/assassinate.h"
#include "reone/game/effect/assureddeflection.h"
#include "reone/game/effect/assuredhit.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/beam.h"
#include "reone/game/effect/blasterdeflectiondecrease.h"
#include "reone/game/effect/blasterdeflectionincrease.h"
#include "reone/game/effect/blind.h"
#include "reone/game/effect/bodyfuel.h"
#include "reone/game/effect/bonusfeat.h"
#include "reone/game/effect/concealment.h"
#include "reone/game/effect/creaturestate.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damagedecrease.h"
#include "reone/game/effect/damageforcepoints.h"
#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damageincrease.h"
#include "reone/game/effect/damagereduction.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/damageshield.h"
#include "reone/game/effect/death.h"
#include "reone/game/effect/destroyshields.h"
#include "reone/game/effect/disguise.h"
#include "reone/game/effect/dispelmagicall.h"
#include "reone/game/effect/dispelmagicbest.h"
#include "reone/game/effect/entangle.h"
#include "reone/game/effect/factionmodifier.h"
#include "reone/game/effect/forcebody.h"
#include "reone/game/effect/forcedrain.h"
#include "reone/game/effect/forcefizzle.h"
#include "reone/game/effect/forcejump.h"
#include "reone/game/effect/forcepushed.h"
#include "reone/game/effect/forcepushtargeted.h"
#include "reone/game/effect/forceresistancedecrease.h"
#include "reone/game/effect/forceresistanceincrease.h"
#include "reone/game/effect/forceresisted.h"
#include "reone/game/effect/forceshield.h"
#include "reone/game/effect/forcesight.h"
#include "reone/game/effect/fpregenmodifier.h"
#include "reone/game/effect/fury.h"
#include "reone/game/effect/haste.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/healforcepoints.h"
#include "reone/game/effect/hitpointchangewhendying.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/invisibility.h"
#include "reone/game/effect/knockdown.h"
#include "reone/game/effect/lightsaberthrow.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/misschance.h"
#include "reone/game/effect/modifyattacks.h"
#include "reone/game/effect/movementspeeddecrease.h"
#include "reone/game/effect/movementspeedincrease.h"
#include "reone/game/effect/paralyze.h"
#include "reone/game/effect/poison.h"
#include "reone/game/effect/purealignmentpowers.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/effect/seeinvisible.h"
#include "reone/game/effect/skilldecrease.h"
#include "reone/game/effect/skillincrease.h"
#include "reone/game/effect/sleep.h"
#include "reone/game/effect/spellimmunity.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/effect/temporaryforcepoints.h"
#include "reone/game/effect/temporaryhitpoints.h"
#include "reone/game/effect/timestop.h"
#include "reone/game/effect/trueseeing.h"
#include "reone/game/effect/ultravision.h"
#include "reone/game/effect/vpregenmodifier.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/object/areaofeffect.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/resource/gff.h"
#include "reone/script/variable.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

namespace {

constexpr int kScriptRacialTypeAll = 28;

// A saved effect's subtype holds its duration type in the low three bits and
// its category in the next two.
constexpr uint16_t kSavedDurationTypeMask = 0x07;
constexpr uint16_t kSavedCategoryMask = 0x18;

DurationType durationTypeFromSubType(uint16_t subType) {
    switch (subType & kSavedDurationTypeMask) {
    case 0:
        return DurationType::Instant;
    case 1:
        return DurationType::Temporary;
    case 2:
        return DurationType::Permanent;
    case 3:
        return DurationType::Equipped;
    case 4:
        return DurationType::Innate;
    default:
        return DurationType::Invalid;
    }
}

uint16_t subTypeOf(DurationType durationType, EffectCategory category) {
    return (static_cast<uint16_t>(durationType) & kSavedDurationTypeMask) |
           static_cast<uint16_t>(category);
}

struct VersusParameterIndices {
    size_t race;
    size_t lawChaos;
    size_t goodEvil;
    // The slot compared against the opposing creature's alignment.
    size_t alignmentFilter;
};

uint16_t serializedEffectType(EffectType type) {
    switch (type) {
    case EffectType::Haste:
        return SavedEffectTypes::haste;
    case EffectType::Regenerate:
        return SavedEffectTypes::regenerate;
    case EffectType::TemporaryHitpoints:
        return SavedEffectTypes::temporaryHitPoints;
    case EffectType::ModifyAttacks:
        return SavedEffectTypes::modifyAttacks;
    case EffectType::Disguise:
        return SavedEffectTypes::disguise;
    case EffectType::TimeStop:
        return SavedEffectTypes::timeStop;
    case EffectType::SpellLevelAbsorption:
        return SavedEffectTypes::spellLevelAbsorption;
    case EffectType::MissChance:
        return SavedEffectTypes::missChance;
    case EffectType::ForceDrain:
        return SavedEffectTypes::forceDrain;
    case EffectType::TemporaryForcePoints:
        return SavedEffectTypes::temporaryForcePoints;
    case EffectType::BodyFuel:
        return SavedEffectTypes::bodyFuel;
    case EffectType::AssuredHit:
        return SavedEffectTypes::assuredHit;
    case EffectType::HealForcePoints:
        return SavedEffectTypes::healForcePoints;

    case EffectType::DamageForcePoints:
        return SavedEffectTypes::damageForcePoints;
    case EffectType::Death:
        return SavedEffectTypes::death;
    case EffectType::Resurrection:
        return SavedEffectTypes::resurrection;
    case EffectType::Beam:
        return SavedEffectTypes::beam;
    case EffectType::HitPointChangeWhenDying:
        return SavedEffectTypes::hitPointChangeWhenDying;
    case EffectType::Heal:
        return SavedEffectTypes::heal;
    case EffectType::Damage:
        return SavedEffectTypes::damage;
    case EffectType::Visual:
        return SavedEffectTypes::visualEffect;
    case EffectType::DamageShield:
        return SavedEffectTypes::damageShield;
    case EffectType::MovementSpeedIncrease:
        return SavedEffectTypes::movementSpeedIncrease;
    case EffectType::DamageResistance:
        return SavedEffectTypes::damageResistance;
    case EffectType::AbilityIncrease:
        return SavedEffectTypes::abilityIncrease;
    case EffectType::AbilityDecrease:
        return SavedEffectTypes::abilityDecrease;
    case EffectType::AttackIncrease:
        return SavedEffectTypes::attackIncrease;
    case EffectType::AttackDecrease:
        return SavedEffectTypes::attackDecrease;
    case EffectType::DamageReduction:
        return SavedEffectTypes::damageReduction;
    case EffectType::DamageIncrease:
        return SavedEffectTypes::damageIncrease;
    case EffectType::DamageDecrease:
        return SavedEffectTypes::damageDecrease;
    case EffectType::DamageImmunityIncrease:
        return SavedEffectTypes::damageImmunityIncrease;
    case EffectType::DamageImmunityDecrease:
        return SavedEffectTypes::damageImmunityDecrease;
    case EffectType::Immunity:
        return SavedEffectTypes::immunity;
    case EffectType::SavingThrowIncrease:
        return SavedEffectTypes::savingThrowIncrease;
    case EffectType::SavingThrowDecrease:
        return SavedEffectTypes::savingThrowDecrease;
    case EffectType::Invisibility:
        return SavedEffectTypes::invisibility;
    case EffectType::ACIncrease:
        return SavedEffectTypes::acIncrease;
    case EffectType::ACDecrease:
        return SavedEffectTypes::acDecrease;
    case EffectType::SkillIncrease:
        return SavedEffectTypes::skillIncrease;
    case EffectType::SkillDecrease:
        return SavedEffectTypes::skillDecrease;
    case EffectType::Sanctuary:
        return SavedEffectTypes::sanctuary;
    case EffectType::SeeInvisible:
        return SavedEffectTypes::seeInvisible;
    case EffectType::Ultravision:
        return SavedEffectTypes::ultravision;
    case EffectType::TrueSeeing:
        return SavedEffectTypes::trueSeeing;
    case EffectType::Blindness:
        return SavedEffectTypes::blindness;
    case EffectType::Concealment:
        return SavedEffectTypes::concealment;
    case EffectType::BonusFeat:
        return SavedEffectTypes::bonusFeat;
    case EffectType::Confused:
    case EffectType::Frightened:
    case EffectType::Stunned:
    case EffectType::Paralyze:
    case EffectType::Sleep:
    case EffectType::DroidStun:
    case EffectType::Choke:
    case EffectType::Horrified:
    case EffectType::WhirlWind:
        return SavedEffectTypes::setState;
    case EffectType::Slow:
        return SavedEffectTypes::slow;
    case EffectType::Entangle:
        return SavedEffectTypes::entangle;
    case EffectType::MovementSpeedDecrease:
        return SavedEffectTypes::movementSpeedDecrease;
    case EffectType::ForceResistanceIncrease:
        return SavedEffectTypes::forceResistanceIncrease;
    case EffectType::SpellImmunity:
        return SavedEffectTypes::spellImmunity;
    case EffectType::ForcePushTargeted:
        return SavedEffectTypes::forcePush;
    case EffectType::ForceResistanceDecrease:
        return SavedEffectTypes::forceResistanceDecrease;
    case EffectType::Poison:
        return SavedEffectTypes::poison;
    case EffectType::LinkEffects:
        return SavedEffectTypes::link;
    case EffectType::ForcePushed:
        return SavedEffectTypes::forcePush;
    case EffectType::BlasterDeflectionIncrease:
        return SavedEffectTypes::blasterDeflectionIncrease;
    case EffectType::BlasterDeflectionDecrease:
        return SavedEffectTypes::blasterDeflectionDecrease;
    case EffectType::AssuredDeflection:
        return SavedEffectTypes::assuredDeflection;
    case EffectType::ForceFizzle:
        return SavedEffectTypes::forceFizzle;
    case EffectType::ForceResisted:
        return SavedEffectTypes::forceResisted;
    case EffectType::LightsaberThrow:
        return SavedEffectTypes::lightsaberThrow;
    case EffectType::ForceShield:
        return SavedEffectTypes::forceShield;
    case EffectType::PureGoodPowers:
        return SavedEffectTypes::pureGoodPowers;
    case EffectType::PureEvilPowers:
        return SavedEffectTypes::pureEvilPowers;
    case EffectType::ForceBody:
        return SavedEffectTypes::forceBody;
    case EffectType::Fury:
        return SavedEffectTypes::fury;
    case EffectType::FPRegenModifier:
        return SavedEffectTypes::forcePointRegenerationModifier;
    case EffectType::VPRegenModifier:
        return SavedEffectTypes::vitalityRegenerationModifier;
    case EffectType::ForceSight:
        return SavedEffectTypes::forceSight;
    case EffectType::FactionModifier:
        return SavedEffectTypes::factionModifier;
    case EffectType::DestroyShields:
        return SavedEffectTypes::destroyShields;
    case EffectType::Assassinate:
        return SavedEffectTypes::assassinate;
    case EffectType::Disease:
        return SavedEffectTypes::disease;
    case EffectType::Knockdown:
        return SavedEffectTypes::knockdown;
    case EffectType::Deaf:
        return SavedEffectTypes::deaf;
    case EffectType::EnemyAttackBonus:
        return SavedEffectTypes::enemyAttackBonus;
    case EffectType::ArcaneSpellFailure:
        return SavedEffectTypes::arcaneSpellFailure;
    case EffectType::AreaOfEffect:
        return SavedEffectTypes::areaOfEffect;
    case EffectType::Curse:
        return SavedEffectTypes::curse;
    case EffectType::Silence:
        return SavedEffectTypes::silence;
    case EffectType::DispelMagicAll:
        return SavedEffectTypes::dispelMagicAll;
    case EffectType::DispelMagicBest:
        return SavedEffectTypes::dispelMagicBest;
    case EffectType::Darkness:
        return SavedEffectTypes::darkness;
    case EffectType::NegativeLevel:
        return SavedEffectTypes::negativeLevel;
    case EffectType::PsychicStatic:
        return SavedEffectTypes::psychicStatic;
    case EffectType::ForceJump:
        return SavedEffectTypes::forceJump;
    default:
        return 0; // No identity; never reinterpret a script enum ordinal.
    }
}

EffectType runtimeEffectType(uint16_t serializedType, int state) {
    switch (serializedType) {
    case SavedEffectTypes::haste:
        return EffectType::Haste;
    case SavedEffectTypes::regenerate:
        return EffectType::Regenerate;
    case SavedEffectTypes::temporaryHitPoints:
        return EffectType::TemporaryHitpoints;
    case SavedEffectTypes::modifyAttacks:
        return EffectType::ModifyAttacks;
    case SavedEffectTypes::disguise:
        return EffectType::Disguise;
    case SavedEffectTypes::timeStop:
        return EffectType::TimeStop;
    case SavedEffectTypes::spellLevelAbsorption:
        return EffectType::SpellLevelAbsorption;
    case SavedEffectTypes::missChance:
        return EffectType::MissChance;
    case SavedEffectTypes::forceDrain:
        return EffectType::ForceDrain;
    case SavedEffectTypes::temporaryForcePoints:
        return EffectType::TemporaryForcePoints;
    case SavedEffectTypes::bodyFuel:
        return EffectType::BodyFuel;
    case SavedEffectTypes::assuredHit:
        return EffectType::AssuredHit;
    case SavedEffectTypes::healForcePoints:
        return EffectType::HealForcePoints;

    case SavedEffectTypes::slow:
        return EffectType::Slow;
    case SavedEffectTypes::setStateInternal:
    case SavedEffectTypes::setAIState:
    case SavedEffectTypes::icon:
        return EffectType::Invalid;
    case SavedEffectTypes::damageForcePoints:
        return EffectType::DamageForcePoints;
    case SavedEffectTypes::death:
        return EffectType::Death;
    case SavedEffectTypes::resurrection:
        return EffectType::Resurrection;
    case SavedEffectTypes::beam:
        return EffectType::Beam;
    case SavedEffectTypes::hitPointChangeWhenDying:
        return EffectType::HitPointChangeWhenDying;
    case SavedEffectTypes::heal:
        return EffectType::Heal;
    case SavedEffectTypes::damage:
        return EffectType::Damage;
    case SavedEffectTypes::visualEffect:
        return EffectType::Visual;
    case SavedEffectTypes::damageShield:
        return EffectType::DamageShield;
    case SavedEffectTypes::movementSpeedIncrease:
        return EffectType::MovementSpeedIncrease;
    case SavedEffectTypes::setState:
        switch (static_cast<CreatureState>(state)) {
        case CreatureState::Confusion:
            return EffectType::Confused;
        case CreatureState::Fear:
            return EffectType::Frightened;
        case CreatureState::DroidStun:
            return EffectType::DroidStun;
        case CreatureState::Choke:
            return EffectType::Choke;
        case CreatureState::Whirlwind:
            return EffectType::WhirlWind;
        case CreatureState::Crush:
            return EffectType::Crush;
        case CreatureState::Stun:
            return EffectType::Stunned;
        case CreatureState::Paralysis:
            return EffectType::Paralyze;
        case CreatureState::Sleep:
            return EffectType::Sleep;
        case CreatureState::ForcePushed:
            return EffectType::ForcePushed;
        case CreatureState::Horrified:
            return EffectType::Horrified;
        case CreatureState::DroidConfused:
            return EffectType::DroidConfused;
        case CreatureState::MindTrick:
            return EffectType::MindTrick;
        case CreatureState::DroidScramble:
            return EffectType::DroidScramble;
        default:
            return EffectType::Invalid;
        }
    case SavedEffectTypes::damageResistance:
        return EffectType::DamageResistance;
    case SavedEffectTypes::abilityIncrease:
        return EffectType::AbilityIncrease;
    case SavedEffectTypes::abilityDecrease:
        return EffectType::AbilityDecrease;
    case SavedEffectTypes::attackIncrease:
        return EffectType::AttackIncrease;
    case SavedEffectTypes::attackDecrease:
        return EffectType::AttackDecrease;
    case SavedEffectTypes::damageReduction:
        return EffectType::DamageReduction;
    case SavedEffectTypes::damageIncrease:
        return EffectType::DamageIncrease;
    case SavedEffectTypes::damageDecrease:
        return EffectType::DamageDecrease;
    case SavedEffectTypes::damageImmunityIncrease:
        return EffectType::DamageImmunityIncrease;
    case SavedEffectTypes::damageImmunityDecrease:
        return EffectType::DamageImmunityDecrease;
    case SavedEffectTypes::immunity:
        return EffectType::Immunity;
    case SavedEffectTypes::savingThrowIncrease:
        return EffectType::SavingThrowIncrease;
    case SavedEffectTypes::savingThrowDecrease:
        return EffectType::SavingThrowDecrease;
    case SavedEffectTypes::invisibility:
        return EffectType::Invisibility;
    case SavedEffectTypes::acIncrease:
        return EffectType::ACIncrease;
    case SavedEffectTypes::acDecrease:
        return EffectType::ACDecrease;
    case SavedEffectTypes::skillIncrease:
        return EffectType::SkillIncrease;
    case SavedEffectTypes::skillDecrease:
        return EffectType::SkillDecrease;
    case SavedEffectTypes::sanctuary:
        return EffectType::Sanctuary;
    case SavedEffectTypes::seeInvisible:
        return EffectType::SeeInvisible;
    case SavedEffectTypes::ultravision:
        return EffectType::Ultravision;
    case SavedEffectTypes::trueSeeing:
        return EffectType::TrueSeeing;
    case SavedEffectTypes::blindness:
        return EffectType::Blindness;
    case SavedEffectTypes::concealment:
        return EffectType::Concealment;
    case SavedEffectTypes::bonusFeat:
        return EffectType::BonusFeat;
    case SavedEffectTypes::entangle:
        return EffectType::Entangle;
    case SavedEffectTypes::movementSpeedDecrease:
        return EffectType::MovementSpeedDecrease;
    case SavedEffectTypes::forceResistanceIncrease:
        return EffectType::ForceResistanceIncrease;
    case SavedEffectTypes::spellImmunity:
        return EffectType::SpellImmunity;
    case SavedEffectTypes::forceResistanceDecrease:
        return EffectType::ForceResistanceDecrease;
    case SavedEffectTypes::poison:
        return EffectType::Poison;
    case SavedEffectTypes::link:
        return EffectType::LinkEffects;
    case SavedEffectTypes::forcePush:
        return EffectType::ForcePushed;
    case SavedEffectTypes::blasterDeflectionIncrease:
        return EffectType::BlasterDeflectionIncrease;
    case SavedEffectTypes::blasterDeflectionDecrease:
        return EffectType::BlasterDeflectionDecrease;
    case SavedEffectTypes::assuredDeflection:
        return EffectType::AssuredDeflection;
    case SavedEffectTypes::lightsaberThrow:
        return EffectType::LightsaberThrow;
    case SavedEffectTypes::forceResisted:
        return EffectType::ForceResisted;
    case SavedEffectTypes::forceFizzle:
        return EffectType::ForceFizzle;
    case SavedEffectTypes::forceShield:
        return EffectType::ForceShield;
    case SavedEffectTypes::pureGoodPowers:
        return EffectType::PureGoodPowers;
    case SavedEffectTypes::pureEvilPowers:
        return EffectType::PureEvilPowers;
    case SavedEffectTypes::forceBody:
        return EffectType::ForceBody;
    case SavedEffectTypes::fury:
        return EffectType::Fury;
    case SavedEffectTypes::forcePointRegenerationModifier:
        return EffectType::FPRegenModifier;
    case SavedEffectTypes::vitalityRegenerationModifier:
        return EffectType::VPRegenModifier;
    case SavedEffectTypes::forceSight:
        return EffectType::ForceSight;
    case SavedEffectTypes::factionModifier:
        return EffectType::FactionModifier;
    case SavedEffectTypes::destroyShields:
        return EffectType::DestroyShields;
    case SavedEffectTypes::assassinate:
        return EffectType::Assassinate;
    case SavedEffectTypes::disease:
        return EffectType::Disease;
    case SavedEffectTypes::knockdown:
        return EffectType::Knockdown;
    case SavedEffectTypes::deaf:
        return EffectType::Deaf;
    case SavedEffectTypes::enemyAttackBonus:
        return EffectType::EnemyAttackBonus;
    case SavedEffectTypes::arcaneSpellFailure:
        return EffectType::ArcaneSpellFailure;
    case SavedEffectTypes::areaOfEffect:
        return EffectType::AreaOfEffect;
    case SavedEffectTypes::curse:
        return EffectType::Curse;
    case SavedEffectTypes::silence:
        return EffectType::Silence;
    case SavedEffectTypes::dispelMagicAll:
        return EffectType::DispelMagicAll;
    case SavedEffectTypes::dispelMagicBest:
        return EffectType::DispelMagicBest;
    case SavedEffectTypes::darkness:
        return EffectType::Darkness;
    case SavedEffectTypes::negativeLevel:
        return EffectType::NegativeLevel;
    case SavedEffectTypes::psychicStatic:
        return EffectType::PsychicStatic;
    case SavedEffectTypes::forceJump:
        return EffectType::ForceJump;
    default:
        return EffectType::Invalid; // The raw identity remains in EffectInstance.
    }
}

std::optional<VersusParameterIndices> versusParameterIndices(uint16_t serializedType) {
    switch (serializedType) {
    case SavedEffectTypes::attackIncrease:
    case SavedEffectTypes::attackDecrease:
    case SavedEffectTypes::damageIncrease:
    case SavedEffectTypes::damageDecrease:
    case SavedEffectTypes::acIncrease:
    case SavedEffectTypes::acDecrease:
    case SavedEffectTypes::skillIncrease:
    case SavedEffectTypes::skillDecrease:
        return VersusParameterIndices {2, 3, 4, 4};
    case SavedEffectTypes::immunity: // its law/chaos slot is the one tested against alignment
        return VersusParameterIndices {1, 2, 3, 2};
    case SavedEffectTypes::concealment:
        return VersusParameterIndices {1, 2, 3, 3};
    case SavedEffectTypes::savingThrowIncrease:
    case SavedEffectTypes::savingThrowDecrease:
        return VersusParameterIndices {3, 4, 5, 5};
    case SavedEffectTypes::invisibility:
    case SavedEffectTypes::sanctuary:
        return VersusParameterIndices {1, 2, 3, 3};
    default:
        return std::nullopt;
    }
}

std::shared_ptr<Effect> executableEffect(const EffectInstance &instance) {
    auto integer = [&instance](size_t index, int32_t fallback = 0) {
        return instance.integerParameter(index, fallback);
    };
    switch (instance.serializedType) {
    case SavedEffectTypes::regenerate:
        return std::make_shared<RegenerateEffect>(integer(0), integer(1), integer(4));
    case SavedEffectTypes::temporaryHitPoints:
        return std::make_shared<TemporaryHitPointsEffect>(integer(0));
    case SavedEffectTypes::temporaryForcePoints:
        return std::make_shared<TemporaryForcePointsEffect>(integer(0));
    case SavedEffectTypes::healForcePoints:
        return std::make_shared<HealForcePointsEffect>(integer(0));
    case SavedEffectTypes::bodyFuel:
        return std::make_shared<BodyFuelEffect>();
    case SavedEffectTypes::destroyShields:
        return std::make_shared<DestroyShieldsEffect>();
    case SavedEffectTypes::assassinate:
        return std::make_shared<AssassinateEffect>(integer(0));
    case SavedEffectTypes::forceBody:
        return std::make_shared<ForceBodyEffect>(integer(0));
    case SavedEffectTypes::haste:
    case SavedEffectTypes::slow:
        return std::make_shared<HasteSlowEffect>(instance.serializedType == SavedEffectTypes::haste);
    case SavedEffectTypes::hasteInternal:
        return std::make_shared<HasteSlowInternalEffect>(true);
    case SavedEffectTypes::slowInternal:
        return std::make_shared<HasteSlowInternalEffect>(false);
    case SavedEffectTypes::setState:
        switch (static_cast<CreatureState>(integer(0))) {
        case CreatureState::Stun:
            return std::make_shared<StunnedEffect>();
        case CreatureState::Paralysis:
            return std::make_shared<ParalyzeEffect>();
        case CreatureState::Sleep:
            return std::make_shared<SleepEffect>();
        default:
            return std::make_shared<CreatureStateEffect>(
                static_cast<CreatureState>(integer(0)));
        }
    case SavedEffectTypes::setStateInternal:
        return std::make_shared<CreatureStateInternalEffect>(static_cast<CreatureState>(integer(0)));
    case SavedEffectTypes::setAIState:
        return std::make_shared<CreatureAIStateEffect>(integer(0));
    case SavedEffectTypes::knockdown:
        return std::make_shared<KnockdownEffect>();
    case SavedEffectTypes::limitMovementSpeed:
        return std::make_shared<LimitMovementSpeedEffect>();
    case SavedEffectTypes::icon:
        return std::make_shared<EffectIconMarkerEffect>(integer(0));
    case SavedEffectTypes::modifyAttacks:
        return std::make_shared<ModifyAttacksEffect>(integer(0));
    case SavedEffectTypes::assuredHit:
        return std::make_shared<AssuredHitEffect>();
    case SavedEffectTypes::movementSpeedIncrease:
        return std::make_shared<MovementSpeedIncreaseEffect>(integer(0));
    case SavedEffectTypes::movementSpeedDecrease:
        return std::make_shared<MovementSpeedDecreaseEffect>(integer(0));
    case SavedEffectTypes::damageShield:
        return std::make_shared<DamageShieldEffect>(integer(0), integer(1), static_cast<DamageType>(integer(2)));
    case SavedEffectTypes::visualEffect:
        return std::make_shared<VisualEffectMarkerEffect>(integer(0));
    case SavedEffectTypes::areaOfEffect:
        return std::make_shared<AreaOfEffectEffect>();
    case SavedEffectTypes::beam:
        return std::make_shared<BeamEffect>(integer(0), instance.boundObjectParameter(0), static_cast<BodyNode>(integer(1)), integer(2) != 0);
    case SavedEffectTypes::forcePush:
        if (integer(0) == 1) {
            auto centre = std::make_shared<Location>(glm::vec3(instance.floatParameters[0],
                                                               instance.floatParameters[1], instance.floatParameters[2]),
                                                     0.0f);
            return std::make_shared<ForcePushTargetedEffect>(centre, integer(1) != 0);
        }
        return std::make_shared<ForcePushedEffect>();
    case SavedEffectTypes::forceJump:
        return std::make_shared<ForceJumpEffect>(instance.boundObjectParameter(0), integer(0));
    case SavedEffectTypes::forceJumpDelayed:
        return std::make_shared<ForceJumpDelayedEffect>(instance.boundObjectParameter(0));
    case SavedEffectTypes::forceShield:
        return std::make_shared<ForceShieldEffect>(integer(0));
    case SavedEffectTypes::lightsaberThrow:
        return std::make_shared<LightsaberThrowEffect>(instance.boundObjectParameter(0),
                                                       instance.boundObjectParameter(1), instance.boundObjectParameter(2), integer(0));
    case SavedEffectTypes::forceFizzle:
        return std::make_shared<ForceFizzleEffect>();
    case SavedEffectTypes::forceResisted:
        return std::make_shared<ForceResistedEffect>(instance.boundObjectParameter(0));
    case SavedEffectTypes::forceResistanceIncrease:
        return std::make_shared<ForceResistanceIncreaseEffect>(integer(0));
    case SavedEffectTypes::forceResistanceDecrease:
        return std::make_shared<ForceResistanceDecreaseEffect>(integer(0));
    case SavedEffectTypes::spellImmunity:
        return std::make_shared<SpellImmunityEffect>(static_cast<SpellType>(integer(0)));
    case SavedEffectTypes::blasterDeflectionIncrease:
        return std::make_shared<BlasterDeflectionIncreaseEffect>(integer(0));
    case SavedEffectTypes::blasterDeflectionDecrease:
        return std::make_shared<BlasterDeflectionDecreaseEffect>(integer(0));
    case SavedEffectTypes::assuredDeflection:
        return std::make_shared<AssuredDeflectionEffect>(integer(0));
    case SavedEffectTypes::damageForcePoints:
        return std::make_shared<DamageForcePointsEffect>(integer(0));
    case SavedEffectTypes::damage:
        return std::make_shared<DamageEffect>(instance);
    case SavedEffectTypes::heal:
        return std::make_shared<HealEffect>(integer(0));
    case SavedEffectTypes::death:
        return std::make_shared<DeathEffect>(integer(0) != 0, integer(1) != 0, integer(2) != 0);
    case SavedEffectTypes::poison:
        return std::make_shared<PoisonEffect>(static_cast<Poison>(integer(0)));
    case SavedEffectTypes::resurrection:
        return std::make_shared<ResurrectionEffect>(integer(0));
    case SavedEffectTypes::abilityIncrease:
        return std::make_shared<AbilityIncreaseEffect>(
            static_cast<Ability>(integer(0)), integer(1));
    case SavedEffectTypes::abilityDecrease:
        return std::make_shared<AbilityDecreaseEffect>(
            static_cast<Ability>(integer(0)), integer(1));
    case SavedEffectTypes::disguise:
        return std::make_shared<DisguiseEffect>(integer(0));
    case SavedEffectTypes::bonusFeat:
        return std::make_shared<BonusFeatEffect>(
            static_cast<FeatType>(integer(0)));
    case SavedEffectTypes::attackIncrease:
        return std::make_shared<AttackIncreaseEffect>(
            integer(0), static_cast<AttackBonus>(integer(1)));
    case SavedEffectTypes::attackDecrease:
        return std::make_shared<AttackDecreaseEffect>(
            integer(0), static_cast<AttackBonus>(integer(1)));
    case SavedEffectTypes::damageIncrease:
        return std::make_shared<DamageIncreaseEffect>(
            integer(0), static_cast<DamageType>(integer(1)));
    case SavedEffectTypes::damageDecrease:
        return std::make_shared<DamageDecreaseEffect>(
            integer(0), static_cast<DamageType>(integer(1)));
    case SavedEffectTypes::acIncrease:
        return std::make_shared<ACIncreaseEffect>(
            integer(1), static_cast<ACBonus>(integer(0)),
            integer(5, kPhysicalDamageTypeFlags));
    case SavedEffectTypes::acDecrease:
        return std::make_shared<ACDecreaseEffect>(
            integer(1), static_cast<ACBonus>(integer(0)),
            integer(5, kPhysicalDamageTypeFlags));
    case SavedEffectTypes::savingThrowIncrease:
        return std::make_shared<SavingThrowIncreaseEffect>(
            integer(1), integer(0),
            static_cast<SavingThrowType>(integer(2)));
    case SavedEffectTypes::savingThrowDecrease:
        return std::make_shared<SavingThrowDecreaseEffect>(
            integer(1), integer(0),
            static_cast<SavingThrowType>(integer(2)));
    case SavedEffectTypes::skillIncrease:
        return std::make_shared<SkillIncreaseEffect>(
            static_cast<SkillType>(integer(0)), integer(1));
    case SavedEffectTypes::skillDecrease:
        return std::make_shared<SkillDecreaseEffect>(
            static_cast<SkillType>(integer(0)), integer(1));
    case SavedEffectTypes::immunity:
        return std::make_shared<ImmunityEffect>(
            static_cast<ImmunityType>(integer(0)));
    case SavedEffectTypes::concealment:
        return std::make_shared<ConcealmentEffect>(integer(0));
    case SavedEffectTypes::damageImmunityIncrease:
        return std::make_shared<DamageImmunityIncreaseEffect>(
            static_cast<DamageType>(integer(0)), integer(1));
    case SavedEffectTypes::damageImmunityDecrease:
        return std::make_shared<DamageImmunityDecreaseEffect>(
            static_cast<DamageType>(integer(0)), integer(1));
    case SavedEffectTypes::damageResistance:
        return std::make_shared<DamageResistanceEffect>(
            static_cast<DamageType>(integer(0)), integer(1), integer(2), integer(3));
    case SavedEffectTypes::damageReduction:
        return std::make_shared<DamageReductionEffect>(
            integer(0), static_cast<DamagePower>(integer(1)), integer(2));
    case SavedEffectTypes::invisibility:
        return std::make_shared<InvisibilityEffect>(
            static_cast<InvisibilityType>(integer(0)));
    case SavedEffectTypes::hitPointChangeWhenDying:
        return std::make_shared<HitPointChangeWhenDyingEffect>(instance.floatParameters[0]);
    case SavedEffectTypes::blindness:
        return std::make_shared<BlindEffect>();
    case SavedEffectTypes::missChance:
        return std::make_shared<MissChanceEffect>(integer(0));
    case SavedEffectTypes::vision:
        return std::make_shared<VisionEffect>(integer(0));
    case SavedEffectTypes::seeInvisible:
        return std::make_shared<SeeInvisibleEffect>();
    case SavedEffectTypes::ultravision:
        return std::make_shared<UltravisionEffect>();
    case SavedEffectTypes::trueSeeing:
        return std::make_shared<TrueSeeingEffect>();
    case SavedEffectTypes::entangle:
        return std::make_shared<EntangleEffect>();
    case SavedEffectTypes::forceDrain:
        return std::make_shared<ForceDrainEffect>(integer(0));
    case SavedEffectTypes::pureGoodPowers:
    case SavedEffectTypes::pureEvilPowers:
        return std::make_shared<PureAlignmentPowersEffect>(instance.serializedType == SavedEffectTypes::pureGoodPowers);
    case SavedEffectTypes::fury:
        return std::make_shared<FuryEffect>();
    case SavedEffectTypes::forcePointRegenerationModifier:
        return std::make_shared<FPRegenModifierEffect>(integer(0));
    case SavedEffectTypes::vitalityRegenerationModifier:
        return std::make_shared<VPRegenModifierEffect>(integer(0));
    case SavedEffectTypes::forceSight:
        return std::make_shared<ForceSightEffect>();
    case SavedEffectTypes::factionModifier:
        return std::make_shared<FactionModifierEffect>(integer(0));
    case SavedEffectTypes::dispelMagicAll:
        return std::make_shared<DispelMagicAllEffect>();
    case SavedEffectTypes::dispelMagicBest:
        return std::make_shared<DispelMagicBestEffect>();
    case SavedEffectTypes::timeStop:
        return std::make_shared<TimeStopEffect>();
    default:
        return nullptr;
    }
}

} // namespace

EffectId EffectIdNamespace::allocate() {
    while (_ids.count(_nextId) != 0) {
        if (_nextId == std::numeric_limits<EffectId>::max()) {
            throw std::overflow_error("Effect ID namespace exhausted");
        }
        ++_nextId;
    }
    if (_nextId == kUnassignedEffectId || _nextId == std::numeric_limits<EffectId>::max()) {
        throw std::overflow_error("Effect ID namespace exhausted");
    }
    EffectId id = _nextId++;
    _ids.insert(id);
    return id;
}

EffectIdImportResult EffectIdNamespace::importId(EffectId id) {
    if (id == kUnassignedEffectId) {
        return EffectIdImportResult::Unassigned;
    }
    auto [_, inserted] = _ids.insert(id);
    return inserted ? EffectIdImportResult::Imported : EffectIdImportResult::Existing;
}

bool EffectIdNamespace::setNextId(EffectId id) {
    if (id == kUnassignedEffectId || id == std::numeric_limits<EffectId>::max()) {
        return false;
    }
    _nextId = id;
    return true;
}

void EffectIdNamespace::reset() {
    _nextId = kFirstId;
    _ids.clear();
}

std::shared_ptr<script::EngineType> Effect::cloneForScript() const {
    return std::make_shared<Effect>(*this);
}

EffectApplicationResult Effect::onApply(Object &, EffectInstance &) {
    debug("Unsupported effect type: " + std::to_string(static_cast<int>(_type)));
    return EffectApplicationResult::Retained;
}

EffectRemovalResult Effect::onRemove(Object &, const EffectInstance &) {
    return EffectRemovalResult::Removed;
}

void Effect::onUpdate(Object &, const EffectInstance &, float) {
}

void Effect::setCategory(EffectCategory category) {
    _category = category;
}

EffectInstance Effect::saveFacingInstance() const {
    if (!_saveFacingRepresentable) {
        throw std::runtime_error(
            "live effect has no serializable value");
    }
    EffectInstance result;
    result.id = _saveFacingId;
    result.serializedType = serializedEffectType(_type);
    result.setCategory(_category);
    result.exposed = 1;
    result.creatorId = kSavedEffectInvalidObjectId;
    result.spellId = _saveFacingSpellId;
    result.integerParameters = _saveFacingIntegers;
    // Every creature state is saved as the same type; its first integer says
    // which state it is.
    if (result.serializedType == SavedEffectTypes::setState) {
        result.integerParameters.resize(std::max<size_t>(1, result.integerParameters.size()));
        std::optional<CreatureState> state;
        switch (_type) {
        case EffectType::Confused:
            state = CreatureState::Confusion;
            break;
        case EffectType::Frightened:
            state = CreatureState::Fear;
            break;
        case EffectType::Stunned:
            state = CreatureState::Stun;
            break;
        case EffectType::Paralyze:
            state = CreatureState::Paralysis;
            break;
        case EffectType::Sleep:
            state = CreatureState::Sleep;
            break;
        case EffectType::DroidStun:
            state = CreatureState::DroidStun;
            break;
        case EffectType::Choke:
            state = CreatureState::Choke;
            break;
        case EffectType::Horrified:
            state = CreatureState::Horrified;
            break;
        case EffectType::WhirlWind:
            state = CreatureState::Whirlwind;
            break;
        default:
            break;
        }
        if (state) {
            result.integerParameters[0] = static_cast<int>(*state);
        }
    }
    result.floatParameters = _saveFacingFloats;
    result.stringParameters = _saveFacingStrings;
    // Preserve the exact candidate binding even while its owner graph remains
    // Constructing. Resolution intentionally begins to succeed only after the
    // candidate incarnation is published Live.
    result.creator = _saveFacingCreator;
    if (auto creator = _saveFacingCreator.resolve()) {
        result.creatorId = creator->id();
    }
    for (size_t index = 0; index < _saveFacingObjects.size(); ++index) {
        if (auto object = _saveFacingObjects[index].resolve()) {
            result.objectParameters[index] = object->id();
            result.objectParameterObjects[index] = object;
        }
    }
    return result;
}

void Effect::setSaveFacingCreator(const std::shared_ptr<Object> &creator) {
    _saveFacingCreator = creator;
}

void Effect::setCreatorFromCaller(const std::shared_ptr<Object> &caller) {
    _saveFacingCreator = caller;
    if (!caller || isa<Area>(caller.get()) || isa<Module>(caller.get())) {
        return;
    }
    _saveFacingSpellId = caller->effectSpellId();
    if (auto areaOfEffect = dyn_cast<AreaOfEffect>(caller.get())) {
        _saveFacingCreator = areaOfEffect->creator();
    }
}

void Effect::setSaveFacingSpellId(int32_t spellId) {
    // The game uses an all-bits-set SpellId for an independently applied effect;
    // zero is a valid semantic grouping value.
    _saveFacingSpellId = static_cast<uint32_t>(spellId);
}

namespace {
bool setAlignmentParameters(uint16_t type, std::vector<int32_t> &parameters,
                            int lawChaos, int goodEvil) {
    const auto indices = versusParameterIndices(type);
    if (!indices) {
        return false;
    }
    const bool missingRace = parameters.size() <= indices->race;
    parameters.resize(std::max(parameters.size(), indices->goodEvil + 1));
    if (missingRace) {
        parameters[indices->race] = static_cast<int>(RacialType::All);
    }
    parameters[indices->lawChaos] = lawChaos;
    parameters[indices->goodEvil] = goodEvil;
    return true;
}
bool setRaceParameter(uint16_t type, std::vector<int32_t> &parameters, int race) {
    const auto indices = versusParameterIndices(type);
    if (!indices) {
        return false;
    }
    parameters.resize(std::max(parameters.size(), indices->race + 1));
    parameters[indices->race] = race;
    return true;
}
} // namespace

bool Effect::setVersusAlignment(int lawChaos, int goodEvil) {
    return setAlignmentParameters(serializedEffectType(_type), _saveFacingIntegers,
                                  lawChaos, goodEvil);
}

bool Effect::setVersusRacialType(int racialType) {
    return setRaceParameter(serializedEffectType(_type), _saveFacingIntegers, racialType);
}

void Effect::captureSaveFacingScriptArguments(
    const std::vector<script::Variable> &arguments,
    const Game &game) {
    switch (_type) {
    case EffectType::Invalid:
    case EffectType::Regenerate:
    case EffectType::Damage:
    case EffectType::DamageResistance:
    case EffectType::DamageReduction:
    case EffectType::DamageImmunityIncrease:
    case EffectType::DamageImmunityDecrease:
    case EffectType::ACIncrease:
    case EffectType::ACDecrease:
        // These constructors author the canonical payload themselves.
        // Re-capturing raw arguments would undo their normalization/defaults.
        return;
    case EffectType::DispelMagicAll:
    case EffectType::DispelMagicBest:
        // The caster level argument is discarded; the dispel carries no integer.
        return;
    default:
        break;
    }
    if (_type == EffectType::LinkEffects) {
        // A link is saved as a link record of its own; its members are not
        // saved within it.
        return;
    }

    size_t integerIndex = 0;
    size_t floatIndex = 0;
    size_t stringIndex = 0;
    size_t objectIndex = 0;
    for (const auto &argument : arguments) {
        switch (argument.type) {
        case script::VariableType::Int:
            if (_type == EffectType::Visual && integerIndex == 1) {
                setSaveFacingInteger(2, argument.intValue);
            } else if ((_type == EffectType::SavingThrowIncrease ||
                        _type == EffectType::SavingThrowDecrease) &&
                       integerIndex < 3) {
                static constexpr std::array<size_t, 3> kSaveParameterOrder {1, 0, 2};
                setSaveFacingInteger(
                    kSaveParameterOrder[integerIndex], argument.intValue);
                setSaveFacingInteger(3, static_cast<int>(RacialType::All));
            } else if (_type == EffectType::ForcePushTargeted) {
                setSaveFacingInteger(1, argument.intValue);
            } else if (_type != EffectType::LightsaberThrow &&
                       _type != EffectType::DamageShield &&
                       _type != EffectType::Damage) {
                setSaveFacingInteger(integerIndex, argument.intValue);
            }
            ++integerIndex;
            break;
        case script::VariableType::Float:
            if (floatIndex < _saveFacingFloats.size()) {
                setSaveFacingFloat(floatIndex++, argument.floatValue);
            }
            break;
        case script::VariableType::String:
            if (stringIndex < _saveFacingStrings.size()) {
                setSaveFacingString(stringIndex++, argument.strValue);
            }
            break;
        case script::VariableType::Object:
            if (objectIndex < _saveFacingObjects.size()) {
                setSaveFacingObject(
                    objectIndex++, game.getObjectById(argument.objectId));
            }
            break;
        case script::VariableType::Location: {
            auto location = std::dynamic_pointer_cast<Location>(
                argument.engineType);
            if (!location) {
                _saveFacingRepresentable = false;
                break;
            }
            const auto &position = location->position();
            for (float value : {position.x, position.y, position.z}) {
                if (floatIndex < _saveFacingFloats.size()) {
                    setSaveFacingFloat(floatIndex++, value);
                }
            }
            break;
        }
        case script::VariableType::Effect:
            _saveFacingRepresentable = false;
            break;
        default:
            break;
        }
    }
}

void Effect::setSaveFacingInteger(size_t index, int32_t value) {
    if (_saveFacingIntegers.size() <= index) {
        _saveFacingIntegers.resize(index + 1);
    }
    _saveFacingIntegers[index] = value;
}

void Effect::setSaveFacingFloat(size_t index, float value) {
    if (index >= _saveFacingFloats.size()) {
        throw std::out_of_range("effect float parameter index");
    }
    _saveFacingFloats[index] = value;
}

void Effect::setSaveFacingString(size_t index, std::string value) {
    if (index >= _saveFacingStrings.size()) {
        throw std::out_of_range("effect string parameter index");
    }
    _saveFacingStrings[index] = std::move(value);
}

void Effect::setSaveFacingObject(
    size_t index,
    const std::shared_ptr<Object> &object) {
    if (index >= _saveFacingObjects.size()) {
        throw std::out_of_range("effect object parameter index");
    }
    _saveFacingObjects[index] = object;
}

EffectInstance EffectInstance::fromGff(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    EffectInstance result;
    result.serializedReferenceContext = identityContext;
    result.id = gff.getUint64("Id");
    result.serializedType = static_cast<uint16_t>(gff.getUint("Type"));
    const auto subType = static_cast<uint16_t>(gff.getUint("SubType"));
    result._durationType = durationTypeFromSubType(subType);
    result._category = static_cast<EffectCategory>(subType & kSavedCategoryMask);
    result.duration = gff.getFloat("Duration");
    result.skipOnLoad = gff.getBool("SkipOnLoad");
    result.expiryDay = gff.getUint("ExpireDay");
    result.expiryTime = gff.getUint("ExpireTime");
    if (result.durationType() == DurationType::Temporary) {
        result.expiryOrigin = EffectExpiryOrigin::LoadedAbsoluteGameTime;
    }
    result.creatorId = gff.getUint("CreatorId");
    result.spellId = gff.getUint("SpellId", std::numeric_limits<uint32_t>::max());
    result.exposed = gff.getInt("IsExposed");

    int32_t integerCount = glm::max(0, gff.getInt("NumIntegers"));
    result.integerParameters.assign(static_cast<size_t>(integerCount), 0);
    auto integers = gff.getList("IntList");
    size_t copyIntegerCount = glm::min(result.integerParameters.size(), integers.size());
    for (size_t i = 0; i < copyIntegerCount; ++i) {
        result.integerParameters[i] = integers[i]->getInt("Value");
    }

    auto floats = gff.getList("FloatList");
    for (size_t i = 0; i < glm::min(result.floatParameters.size(), floats.size()); ++i) {
        result.floatParameters[i] = floats[i]->getFloat("Value");
    }

    auto strings = gff.getList("StringList");
    for (size_t i = 0; i < glm::min(result.stringParameters.size(), strings.size()); ++i) {
        result.stringParameters[i] = strings[i]->getString("Value");
    }

    auto objects = gff.getList("ObjectList");
    for (size_t i = 0; i < glm::min(result.objectParameters.size(), objects.size()); ++i) {
        result.objectParameters[i] = objects[i]->getUint("Value", kSavedEffectInvalidObjectId);
    }
    // Executable construction happens after the saved graph binds creators
    // and object parameters, when applyEffect/restoreEffect materializes it.
    return result;
}

std::shared_ptr<resource::Gff> EffectInstance::toGff() const {
    using resource::Gff;
    auto result = Gff::Builder().type(2).field(Gff::Field::newDword64("Id", id)).field(Gff::Field::newWord("Type", serializedType)).field(Gff::Field::newWord("SubType", subTypeOf(_durationType, _category))).field(Gff::Field::newFloat("Duration", duration)).field(Gff::Field::newByte("SkipOnLoad", skipOnLoad)).field(Gff::Field::newDword("ExpireDay", expiryDay)).field(Gff::Field::newDword("ExpireTime", expiryTime)).field(Gff::Field::newDword("CreatorId", creatorId)).field(Gff::Field::newDword("SpellId", spellId)).field(Gff::Field::newInt("IsExposed", exposed)).field(Gff::Field::newInt("NumIntegers", static_cast<int32_t>(integerParameters.size()))).build();

    std::vector<std::shared_ptr<Gff>> ints;
    for (int32_t value : integerParameters) {
        ints.push_back(Gff::Builder().type(3).field(Gff::Field::newInt("Value", value)).build());
    }
    std::vector<std::shared_ptr<Gff>> floats;
    for (float value : floatParameters) {
        floats.push_back(Gff::Builder().type(4).field(Gff::Field::newFloat("Value", value)).build());
    }
    std::vector<std::shared_ptr<Gff>> strings;
    for (const auto &value : stringParameters) {
        strings.push_back(Gff::Builder().type(5).field(Gff::Field::newCExoString("Value", value)).build());
    }
    std::vector<std::shared_ptr<Gff>> objects;
    for (uint32_t value : objectParameters) {
        objects.push_back(Gff::Builder().type(6).field(Gff::Field::newDword("Value", value)).build());
    }
    result->fields().push_back(Gff::Field::newList("IntList", std::move(ints)));
    result->fields().push_back(Gff::Field::newList("FloatList", std::move(floats)));
    result->fields().push_back(Gff::Field::newList("StringList", std::move(strings)));
    result->fields().push_back(Gff::Field::newList("ObjectList", std::move(objects)));
    return result;
}

void EffectInstance::setDuration(DurationType type, float seconds) {
    setDurationType(type);
    duration = seconds;
    remainingDuration = type == DurationType::Temporary
                            ? std::optional<float>(seconds)
                            : std::nullopt;
    expiryOrigin = type == DurationType::Temporary
                       ? EffectExpiryOrigin::RuntimeCountdown
                       : EffectExpiryOrigin::None;
    expiryDay = 0;
    expiryTime = 0;
}

void EffectInstance::setIntegerParameter(size_t index, int32_t value) {
    if (integerParameters.size() <= index) {
        integerParameters.resize(index + 1);
    }
    integerParameters[index] = value;
}

bool EffectInstance::shouldRestoreOnLoad() const {
    if (skipOnLoad || durationType() == DurationType::Equipped) {
        return false;
    }
    // These are operations, never retained EffectList applications. Duration
    // alone is not a retention policy: accepted Instant modifiers are valid.
    switch (serializedType) {
    case SavedEffectTypes::resurrection:
    case SavedEffectTypes::death:
    case SavedEffectTypes::damage:
    case SavedEffectTypes::heal:
    case SavedEffectTypes::vision:
    case SavedEffectTypes::forceDrain:
    case SavedEffectTypes::damageForcePoints:
    case SavedEffectTypes::healForcePoints:
        return false;
    default:
        return true;
    }
}

void EffectInstance::materialize() {
    if (!effect) {
        effect = executableEffect(*this);
    }
}

EffectInstance EffectInstance::linkedChild(const std::shared_ptr<Effect> &child) const {
    EffectInstance result = child->saveFacingInstance();
    if (!std::dynamic_pointer_cast<SavedEffectValue>(child)) {
        result.effect = child;
    }
    result.id = id;
    result.creatorId = creatorId;
    result.creator = creator;
    result.spellId = spellId;
    result.serializedReferenceContext = serializedReferenceContext;
    result._runtimeSession = _runtimeSession;
    // The member takes the link's duration type, and its category unless the
    // link has none.
    result._durationType = _durationType;
    if (_category != EffectCategory::None) {
        result._category = _category;
    }
    result.duration = duration;
    result.remainingDuration = remainingDuration;
    result.expiryOrigin = expiryOrigin;
    result.expiryDay = expiryDay;
    result.expiryTime = expiryTime;
    result.restoring = restoring;
    return result;
}

EffectType EffectInstance::type() const {
    return runtimeEffectType(serializedType, integerParameter(0));
}

int EffectInstance::scriptEffectType(bool tsl) const {
    switch (serializedType) {
    case SavedEffectTypes::damageResistance:
        return static_cast<int>(EffectType::DamageResistance);
    case SavedEffectTypes::regenerate:
        return static_cast<int>(EffectType::Regenerate);
    case SavedEffectTypes::damageReduction:
        return static_cast<int>(EffectType::DamageReduction);
    case SavedEffectTypes::temporaryHitPoints:
        return static_cast<int>(EffectType::TemporaryHitpoints);
    case SavedEffectTypes::entangle:
        return static_cast<int>(EffectType::Entangle);
    case SavedEffectTypes::deaf:
        return static_cast<int>(EffectType::Deaf);
    case SavedEffectTypes::immunity:
        return static_cast<int>(EffectType::Immunity);
    case SavedEffectTypes::enemyAttackBonus:
        return static_cast<int>(EffectType::EnemyAttackBonus);
    case SavedEffectTypes::arcaneSpellFailure:
        return static_cast<int>(EffectType::ArcaneSpellFailure);
    case SavedEffectTypes::areaOfEffect:
        return static_cast<int>(EffectType::AreaOfEffect);
    case SavedEffectTypes::beam:
        return static_cast<int>(EffectType::Beam);
    case SavedEffectTypes::setState:
        // A creature state answers for its state; states without a constant answer 0.
        switch (integerParameter(0)) {
        case static_cast<int>(CreatureState::Confusion):
            return static_cast<int>(EffectType::Confused);
        case static_cast<int>(CreatureState::Fear):
            return static_cast<int>(EffectType::Frightened);
        case static_cast<int>(CreatureState::DroidStun):
            return static_cast<int>(EffectType::Turned);
        case static_cast<int>(CreatureState::Stun):
            return static_cast<int>(EffectType::Stunned);
        case static_cast<int>(CreatureState::Paralysis):
            return static_cast<int>(EffectType::Paralyze);
        case static_cast<int>(CreatureState::Sleep):
            return static_cast<int>(EffectType::Sleep);
        case static_cast<int>(CreatureState::DroidConfused):
            return tsl ? static_cast<int>(EffectType::DroidConfused) : 0;
        case static_cast<int>(CreatureState::MindTrick):
            return tsl ? static_cast<int>(EffectType::MindTrick) : 0;
        case static_cast<int>(CreatureState::DroidScramble):
            return tsl ? static_cast<int>(EffectType::DroidScramble) : 0;
        default:
            return 0;
        }
    case SavedEffectTypes::poison:
        return static_cast<int>(EffectType::Poison);
    case SavedEffectTypes::disease:
        return static_cast<int>(EffectType::Disease);
    case SavedEffectTypes::curse:
        return static_cast<int>(EffectType::Curse);
    case SavedEffectTypes::silence:
        return static_cast<int>(EffectType::Silence);
    case SavedEffectTypes::haste:
        return static_cast<int>(EffectType::Haste);
    case SavedEffectTypes::slow:
        return static_cast<int>(EffectType::Slow);
    case SavedEffectTypes::abilityIncrease:
        return static_cast<int>(EffectType::AbilityIncrease);
    case SavedEffectTypes::abilityDecrease:
        return static_cast<int>(EffectType::AbilityDecrease);
    case SavedEffectTypes::attackIncrease:
        return static_cast<int>(EffectType::AttackIncrease);
    case SavedEffectTypes::attackDecrease:
        return static_cast<int>(EffectType::AttackDecrease);
    case SavedEffectTypes::damageIncrease:
        return static_cast<int>(EffectType::DamageIncrease);
    case SavedEffectTypes::damageDecrease:
        return static_cast<int>(EffectType::DamageDecrease);
    case SavedEffectTypes::damageImmunityIncrease:
        return static_cast<int>(EffectType::DamageImmunityIncrease);
    case SavedEffectTypes::damageImmunityDecrease:
        return static_cast<int>(EffectType::DamageImmunityDecrease);
    case SavedEffectTypes::acIncrease:
        return static_cast<int>(EffectType::ACIncrease);
    case SavedEffectTypes::acDecrease:
        return static_cast<int>(EffectType::ACDecrease);
    case SavedEffectTypes::movementSpeedIncrease:
        return static_cast<int>(EffectType::MovementSpeedIncrease);
    case SavedEffectTypes::movementSpeedDecrease:
        return static_cast<int>(EffectType::MovementSpeedDecrease);
    case SavedEffectTypes::savingThrowIncrease:
        return static_cast<int>(EffectType::SavingThrowIncrease);
    case SavedEffectTypes::savingThrowDecrease:
        return static_cast<int>(EffectType::SavingThrowDecrease);
    case SavedEffectTypes::forceResistanceIncrease:
        return static_cast<int>(EffectType::ForceResistanceIncrease);
    case SavedEffectTypes::forceResistanceDecrease:
        return static_cast<int>(EffectType::ForceResistanceDecrease);
    case SavedEffectTypes::skillIncrease:
        return static_cast<int>(EffectType::SkillIncrease);
    case SavedEffectTypes::skillDecrease:
        return static_cast<int>(EffectType::SkillDecrease);
    case SavedEffectTypes::invisibility:
        return static_cast<int>(EffectType::Invisibility);
    case SavedEffectTypes::darkness:
        return static_cast<int>(EffectType::Darkness);
    case SavedEffectTypes::dispelMagicAll:
        return static_cast<int>(EffectType::DispelMagicAll);
    case SavedEffectTypes::damageShield:
        return static_cast<int>(EffectType::ElementalShield);
    case SavedEffectTypes::negativeLevel:
        return static_cast<int>(EffectType::NegativeLevel);
    case SavedEffectTypes::disguise:
        return static_cast<int>(EffectType::Disguise);
    case SavedEffectTypes::sanctuary:
        return static_cast<int>(EffectType::Sanctuary);
    case SavedEffectTypes::trueSeeing:
        return static_cast<int>(EffectType::TrueSeeing);
    case SavedEffectTypes::seeInvisible:
        return static_cast<int>(EffectType::SeeInvisible);
    case SavedEffectTypes::timeStop:
        return static_cast<int>(EffectType::TimeStop);
    case SavedEffectTypes::blindness:
        return static_cast<int>(EffectType::Blindness);
    case SavedEffectTypes::spellLevelAbsorption:
        return static_cast<int>(EffectType::SpellLevelAbsorption);
    case SavedEffectTypes::dispelMagicBest:
        return static_cast<int>(EffectType::DispelMagicBest);
    case SavedEffectTypes::ultravision:
        return static_cast<int>(EffectType::Ultravision);
    case SavedEffectTypes::missChance:
        return static_cast<int>(EffectType::MissChance);
    case SavedEffectTypes::concealment:
        return static_cast<int>(EffectType::Concealment);
    case SavedEffectTypes::spellImmunity:
        return static_cast<int>(EffectType::SpellImmunity);
    case SavedEffectTypes::assuredHit:
        return static_cast<int>(EffectType::AssuredHit);
    case SavedEffectTypes::visualEffect:
        return static_cast<int>(EffectType::Visual);
    case SavedEffectTypes::lightsaberThrow:
        return static_cast<int>(EffectType::LightsaberThrow);
    case SavedEffectTypes::forceJump:
        return static_cast<int>(EffectType::ForceJump);
    case SavedEffectTypes::assuredDeflection:
        return static_cast<int>(EffectType::AssuredDeflection);
    default:
        return 0;
    }
}

int32_t EffectInstance::integerParameter(
    size_t index, int32_t defaultValue) const {
    return index < integerParameters.size()
               ? integerParameters[index]
               : defaultValue;
}

std::shared_ptr<Object> EffectInstance::boundCreator() const {
    return creator.resolve();
}

std::shared_ptr<Object> EffectInstance::boundObjectParameter(
    size_t index) const {
    return index < objectParameterObjects.size()
               ? objectParameterObjects[index].resolve()
               : nullptr;
}

bool EffectInstance::appliesVersus(std::optional<std::reference_wrapper<const Creature>> creature) const {
    auto indices = versusParameterIndices(serializedType);
    if (!indices) {
        return true;
    }

    int race = integerParameter(indices->race, static_cast<int>(RacialType::All));
    int alignment = integerParameter(indices->alignmentFilter, static_cast<int>(Alignment::All));
    // A saved effect names every race either way: by the racial type All or by
    // the script constant RACIAL_TYPE_ALL.
    bool hasRace = race != static_cast<int>(RacialType::All) && race != kScriptRacialTypeAll;
    bool hasAlignment = alignment != static_cast<int>(Alignment::All);
    if (!hasRace && !hasAlignment) {
        return true;
    }
    if (!creature) {
        return false;
    }
    if (hasRace && race != static_cast<int>(creature->get().racialType())) {
        return false;
    }
    return !hasAlignment || alignment == static_cast<int>(creature->get().alignment());
}

bool EffectInstance::hasLiveRuntimeSource() const {
    return durationType() != DurationType::Equipped ||
           static_cast<bool>(boundCreator());
}

bool EffectInstance::bindCreator(const std::shared_ptr<Object> &object) {
    creator.reset();
    if (!object) {
        return false;
    }
    creator = object;
    return true;
}

bool EffectInstance::bindObjectParameter(
    size_t index, const std::shared_ptr<Object> &object) {
    if (index >= objectParameterObjects.size()) {
        return false;
    }
    objectParameterObjects[index].reset();
    if (!object) {
        return false;
    }
    objectParameterObjects[index] = object;
    return true;
}

void EffectInstance::retireAreaRuntimeBindings(
    const std::set<const Object *> &retainedObjects) {
    auto retain = [&retainedObjects](
                      RuntimeObjectRef<Object> &binding,
                      uint32_t &identity) {
        auto object = binding.resolve();
        if (!object || retainedObjects.count(object.get()) == 0) {
            binding.reset();
            identity = kSavedEffectInvalidObjectId;
            return;
        }
        identity = object->id();
    };

    retain(creator, creatorId);
    for (size_t index = 0; index < objectParameters.size(); ++index) {
        retain(objectParameterObjects[index], objectParameters[index]);
    }
    serializedReferenceContext.reset();
    _runtimeSession.reset();
}

SavedEffectValue::SavedEffectValue(EffectInstance instance) :
    CopyableEffect(instance.type()),
    _instance(std::move(instance)) {
    // Script values copy the current canonical application, not its constructor.
    // A future application decodes these current parameters after reference binding.
    _instance.effect.reset();
}

void SavedEffectValue::setCategory(EffectCategory category) {
    _instance.setCategory(category);
}

bool SavedEffectValue::setVersusAlignment(int lawChaos, int goodEvil) {
    return setAlignmentParameters(_instance.serializedType, _instance.integerParameters,
                                  lawChaos, goodEvil);
}

bool SavedEffectValue::setVersusRacialType(int racialType) {
    return setRaceParameter(_instance.serializedType, _instance.integerParameters, racialType);
}

namespace {

constexpr uint32_t kNoEffectSpell = std::numeric_limits<uint32_t>::max();
constexpr int kDispelResistance = 11;

// A dispel reaches magical effects that last for a time or for good.
bool isDispellable(const EffectInstance &effect) {
    const auto lifetime = effect.durationType();
    return effect.category() == EffectCategory::Magical &&
           (lifetime == DurationType::Temporary || lifetime == DurationType::Permanent);
}

// A dispel is cast by the creature that created it; only a creature casts one.
std::shared_ptr<Creature> dispelCaster(const EffectInstance &dispel) {
    return std::dynamic_pointer_cast<Creature>(dispel.boundCreator());
}

} // namespace

EffectApplicationResult DispelMagicAllEffect::onApply(Object &object, EffectInstance &instance) {
    const auto caster = dispelCaster(instance);
    if (!caster) {
        return EffectApplicationResult::Applied;
    }
    const int casterLevel = caster->getSpellLevel(false);
    // Each group is judged once, by its first dispellable member.
    std::vector<EffectId> judged;
    std::vector<uint32_t> dispelledSpells;
    for (const auto &effect : object.effects()) {
        if (!isDispellable(effect) || std::find(judged.begin(), judged.end(), effect.id) != judged.end()) {
            continue;
        }
        const auto creator = std::dynamic_pointer_cast<Creature>(effect.boundCreator());
        const int resistance = kDispelResistance + (creator ? creator->getSpellLevel(false) : 0);
        if (randomInt(1, 20) + casterLevel > resistance) {
            object.game().queueEffectRemoval(object, effect.id);
            if (effect.spellId != kNoEffectSpell) {
                dispelledSpells.push_back(effect.spellId);
            }
        }
        judged.push_back(effect.id);
    }
    if (!dispelledSpells.empty()) {
        addDispelFeedback(object.game(), object.services(), object, *caster, dispelledSpells);
    }
    return EffectApplicationResult::Applied;
}

EffectApplicationResult DispelMagicBestEffect::onApply(Object &object, EffectInstance &instance) {
    const auto caster = dispelCaster(instance);
    if (!caster) {
        return EffectApplicationResult::Applied;
    }
    const int casterLevel = caster->getSpellLevel(false);
    // A group that resists is set aside and the search starts over, until one
    // group is dispelled or none is left.
    std::vector<EffectId> resisted;
    while (true) {
        const EffectInstance *chosen = nullptr;
        for (const auto &effect : object.effects()) {
            if (!isDispellable(effect) || std::find(resisted.begin(), resisted.end(), effect.id) != resisted.end()) {
                continue;
            }
            const auto creator = std::dynamic_pointer_cast<Creature>(effect.boundCreator());
            if (creator && creator->getSpellLevel(false) == 0) {
                continue;
            }
            chosen = &effect;
            break;
        }
        if (!chosen) {
            return EffectApplicationResult::Applied;
        }
        if (randomInt(1, 20) + casterLevel > kDispelResistance) {
            if (chosen->spellId != kNoEffectSpell) {
                addDispelFeedback(object.game(), object.services(), object, std::nullopt, {chosen->spellId});
            }
            object.game().queueEffectRemoval(object, chosen->id);
            return EffectApplicationResult::Applied;
        }
        resisted.push_back(chosen->id);
    }
}

EffectApplicationResult TimeStopEffect::onApply(Object &object, EffectInstance &instance) {
    auto &game = object.game();
    // The player's pause leaves a new Time Stop without effect.
    if (game.isPaused()) {
        return EffectApplicationResult::Retained;
    }
    if (!game.isTimeStopped()) {
        game.addTimeStopExclusion(object);
        game.toggleTimeStop();
        return EffectApplicationResult::Retained;
    }
    // While time stands still, a Time Stop only renews the target's own: it
    // lasts its duration from the target's present time.
    const auto current = std::find_if(object.effects().begin(), object.effects().end(),
                                      [](const EffectInstance &effect) { return effect.type() == EffectType::TimeStop; });
    if (current == object.effects().end()) {
        return EffectApplicationResult::Retained;
    }
    auto &renewed = object.findEffectApplication(current->applicationOrder)->get();
    const uint32_t day = game.millisecondsPerWorldDay();
    const auto expiry = getEffectExpiryMilliseconds(game.activeTimeMilliseconds(object), instance.duration, day);
    renewed.expiryDay = static_cast<uint32_t>(expiry / day);
    renewed.expiryTime = static_cast<uint32_t>(expiry % day);
    renewed.expiryOrigin = EffectExpiryOrigin::RuntimeAbsoluteGameTime;
    return EffectApplicationResult::Applied;
}

EffectRemovalResult TimeStopEffect::onRemove(Object &object, const EffectInstance &) {
    // Ending a Time Stop always flips the stop; a stop left without an
    // excluded caster ends at the next module update.
    auto &game = object.game();
    game.toggleTimeStop();
    game.removeTimeStopExclusion(object);
    return EffectRemovalResult::Removed;
}

void LinkEffectsEffect::setCategory(EffectCategory category) {
    Effect::setCategory(category);
    if (_childEffect) {
        _childEffect->setCategory(category);
    }
    if (_parentEffect) {
        _parentEffect->setCategory(category);
    }
}

EffectApplicationResult LinkEffectsEffect::onApply(Object &object, EffectInstance &instance) {
    // The members apply in the order the script named them; one that is
    // rejected does not undo those before it.
    std::vector<EffectInstance> members;
    if (_childEffect) {
        members.push_back(instance.linkedChild(_childEffect));
    }
    if (_parentEffect) {
        members.push_back(instance.linkedChild(_parentEffect));
    }
    object.applyEffectPackage(members);
    return EffectApplicationResult::Applied;
}

void LinkEffectsEffect::retireAreaRuntime(
    const std::set<const Object *> &retainedObjects) {
    if (_childEffect) {
        _childEffect->retireAreaRuntime(retainedObjects);
    }
    if (_parentEffect) {
        _parentEffect->retireAreaRuntime(retainedObjects);
    }
}

} // namespace game

} // namespace reone
