/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/game/attack.h"

#include "action/attackanimations.h"

#include "reone/game/animations.h"
#include "reone/game/d20/feats.h"
#include "reone/game/di/services.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/projectiles.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/system/arrayref.h"
#include "reone/system/randomutil.h"

#include <algorithm>
#include <initializer_list>

namespace reone {

namespace game {

static constexpr char kModelEventDetonate[] = "detonate";
static constexpr float kProjectileSpeed = 16.0f;
static constexpr int kUnarmedCriticalThreat = 1;
static constexpr int kCriticalHitImmunityPropertySubtype = 8;
static constexpr float kSpecialAttackDefensePenaltyDuration = 3.0f;

static int toNativeSignedByte(int value) {
    unsigned int byte = static_cast<unsigned int>(value) & 0xffu;
    if (byte < 0x80u) {
        return static_cast<int>(byte);
    }
    return static_cast<int>(byte) - 0x100;
}

static int toNativeUnsignedByte(int value) {
    return static_cast<int>(static_cast<unsigned int>(value) & 0xffu);
}

static int toNativeSignedWord(int value) {
    unsigned int word = static_cast<unsigned int>(value) & 0xffffu;
    if (word < 0x8000u) {
        return static_cast<int>(word);
    }
    return static_cast<int>(word) - 0x10000;
}

static int getNativeDamageSlot(DamageType type) {
    int damageFlags = static_cast<int>(type);
    if (damageFlags <= 0) {
        throw std::invalid_argument("Damage breakdown type must be positive");
    }

    int slot = 0;
    while (damageFlags > 1) {
        damageFlags >>= 1;
        ++slot;
    }
    if (slot >= 14) {
        throw std::invalid_argument("Damage breakdown type is out of range");
    }
    return slot;
}

DamageBreakdown::DamageBreakdown() {
    rawDamageSlots.fill(-1);
}

void DamageBreakdown::addRawDamage(int amount, DamageType type) {
    int slot = getNativeDamageSlot(type);
    int16_t &current = rawDamageSlots[slot];
    int result;
    if (current > 0) {
        result = current + amount;
        if (result <= 0) {
            result = 1;
        }
    } else {
        result = std::max(amount, 0);
    }
    current = toNativeSignedWord(result);
}

bool isMeleeWieldType(CreatureWieldType type) {
    switch (type) {
    case CreatureWieldType::SingleSword:
    case CreatureWieldType::DoubleBladedSword:
    case CreatureWieldType::DualSwords:
        return true;
    default:
        return false;
    }
}

bool isRangedWieldType(CreatureWieldType type) {
    switch (type) {
    case CreatureWieldType::BlasterPistol:
    case CreatureWieldType::DualPistols:
    case CreatureWieldType::BlasterRifle:
    case CreatureWieldType::HeavyWeapon:
        return true;
    default:
        return false;
    }
}

bool isAttackSuccessful(AttackResultType result) {
    switch (result) {
    case AttackResultType::HitSuccessful:
    case AttackResultType::CriticalHit:
    case AttackResultType::AutomaticHit:
        return true;
    default:
        return false;
    }
}

bool isPhysicalAttackFeat(FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
    case FeatType::Flurry:
    case FeatType::ImprovedFlurry:
    case FeatType::WhirlwindAttack:
    case FeatType::PowerAttack:
    case FeatType::ImprovedPowerAttack:
    case FeatType::MasterPowerAttack:
    case FeatType::RapidShot:
    case FeatType::ImprovedRapidShot:
    case FeatType::MultiShot:
    case FeatType::SniperShot:
    case FeatType::ImprovedSniperShot:
    case FeatType::MasterSniperShot:
    case FeatType::PowerBlast:
    case FeatType::ImprovedPowerBlast:
    case FeatType::MasterPowerBlast:
        return true;
    default:
        return false;
    }
}

static bool hasActiveItemProperty(
    const Item &item,
    ItemProperty type,
    int subtype = -1) {

    for (const Item::PropertyEntry &property : item.properties()) {
        if (!item.isPropertyActive(property) ||
            property.propertyName != static_cast<uint16_t>(type) ||
            (subtype >= 0 && property.subtype != subtype)) {
            continue;
        }
        return true;
    }
    return false;
}

static bool hasEffectImmunity(
    const Creature &target,
    ImmunityType immunityType) {

    for (const Object::AppliedEffect &applied : target.effects()) {
        if (applied.effect->type() != EffectType::Immunity) {
            continue;
        }

        const auto &effect = static_cast<const ImmunityEffect &>(*applied.effect);
        if (effect.immunityType() == immunityType) {
            return true;
        }
    }
    return false;
}

static bool hasCriticalHitImmunity(const Creature &target) {
    if (hasEffectImmunity(target, ImmunityType::CriticalHit)) {
        return true;
    }

    for (const auto &[slot, item] : target.equipment()) {
        if (!item ||
            slot == InventorySlots::rightWeapon2 ||
            slot == InventorySlots::leftWeapon2) {
            continue;
        }
        if (hasActiveItemProperty(
                *item,
                ItemProperty::Immunity,
                kCriticalHitImmunityPropertySubtype)) {
            return true;
        }
    }
    return false;
}

static int getBaseCriticalThreat(const Item *weapon) {
    return weapon ? weapon->criticalThreat() : kUnarmedCriticalThreat;
}

static int getCriticalThreat(const Item *weapon, int threatBonus) {
    int threat = getBaseCriticalThreat(weapon);
    if (weapon && hasActiveItemProperty(*weapon, ItemProperty::Keen)) {
        threat *= 2;
    }
    return threat + threatBonus;
}

struct AttackResolution {
    AttackResultType result {AttackResultType::Invalid};
    int roll {0};
    DefenseBreakdown defenseBreakdown;
    bool naturalTwenty {false};
    bool naturalOne {false};
    bool coupDeGrace {false};
    int criticalThreatThreshold {0};
    bool criticalThreatened {false};
    int criticalConfirmationRoll {0};
    bool criticalConfirmed {false};
};

static AttackResolution computeAttack(
    const Creature &attacker,
    const Object &target,
    int attackBonus,
    int criticalThreat,
    int damageFlags,
    bool ranged) {

    AttackResolution resolution;

    // Determine defense of a target
    const auto *targetCreature = dyn_cast<Creature>(&target);
    if (targetCreature) {
        resolution.defenseBreakdown = targetCreature->getDefenseBreakdown(
            &attacker,
            damageFlags);
    }
    int defense = resolution.defenseBreakdown.total;

    // Consume the d20 before applying Coup de Grace's automatic result.
    resolution.roll = randomInt(1, 20);

    if (!ranged &&
        targetCreature &&
        targetCreature->isDebilitated() &&
        targetCreature->attributes().getAggregateLevel() <= 4 &&
        !targetCreature->isPartyMember()) {
        resolution.result = AttackResultType::AutomaticHit;
        resolution.roll = 20;
        resolution.coupDeGrace = true;
        return resolution;
    }

    if (attacker.hasAssuredHit()) {
        resolution.result = AttackResultType::HitSuccessful;
        debug(str(boost::format("computeAttack: assured hit: roll(%d)") % resolution.roll),
              LogChannel::Combat);
        return resolution;
    }

    if (resolution.roll == 1) {
        resolution.result = AttackResultType::Miss;
        resolution.naturalOne = true;
        debug(str(boost::format("computeAttack: miss: roll(1)")), LogChannel::Combat);
        return resolution;
    }

    if (resolution.roll != 20 &&
        (resolution.roll + attackBonus) < defense) {
        resolution.result = AttackResultType::Miss;
        debug(str(boost::format("computeAttack: miss: roll(%d), bonus(%d), defense(%d)") %
                  resolution.roll % attackBonus % defense),
              LogChannel::Combat);
        return resolution;
    }

    resolution.naturalTwenty = resolution.roll == 20;

    // Critical threat
    resolution.criticalThreatThreshold = 21 - criticalThreat;
    if (resolution.roll >= resolution.criticalThreatThreshold) {
        resolution.criticalThreatened = true;

        // Critical confirmation
        resolution.criticalConfirmationRoll = randomInt(1, 20);
        if ((resolution.criticalConfirmationRoll + attackBonus) >= defense) {
            resolution.criticalConfirmed = true;

            bool criticalHitImmune =
                targetCreature && hasCriticalHitImmunity(*targetCreature);
            if (!criticalHitImmune) {
                resolution.result = AttackResultType::CriticalHit;
                debug(str(boost::format("computeAttack: critical hit: roll(%d), confirmation(%d),"
                                        " bonus(%d), defense(%d), critical threat(%d)") %
                          resolution.roll % resolution.criticalConfirmationRoll % attackBonus %
                          defense % criticalThreat),
                      LogChannel::Combat);
                return resolution;
            }
        }
    }

    resolution.result = AttackResultType::HitSuccessful;
    debug(str(boost::format("computeAttack: hit: roll(%d), bonus(%d), defense(%d),"
                            " critical threat(%d)") %
              resolution.roll % attackBonus % defense % criticalThreat),
          LogChannel::Combat);

    return resolution;
}

static int rollDamageDice(int numDice, int die) {
    int result = 0;
    for (int i = 0; i < numDice; ++i) {
        result += randomInt(1, die);
    }
    return result;
}

static void computeWeaponDamage(
    const Creature &attacker, const Object &target, const Item &weapon,
    AttackBuffer::Source source, AttackResultType result,
    bool criticalConfirmed, int damageBonus,
    DamagePacket &damage, DamageBreakdown &breakdown) {

    int multiplier = result == AttackResultType::CriticalHit
                         ? weapon.criticalHitMultiplier()
                         : 1;
    bool offHand = source == AttackBuffer::Source::Offhand;
    PhysicalDamageBonus physicalBonus = attacker.getPhysicalDamageBonus(
        &weapon,
        offHand);

    int baseDamage = 0;
    int amount = multiplier * (
        damageBonus + physicalBonus.total());
    if (!hasActiveItemProperty(weapon, ItemProperty::NoDamage)) {
        for (int multiple = 0; multiple < multiplier; ++multiple) {
            baseDamage += rollDamageDice(weapon.numDice(), weapon.dieToRoll());
        }
    }
    amount += baseDamage;

    int massiveCriticalDamage = attacker.getMassiveCriticalDamage(
        &weapon, result == AttackResultType::CriticalHit);
    amount += massiveCriticalDamage;

    breakdown.strengthModifier = weapon.isRanged()
                                     ? 0
                                     : toNativeSignedByte(
                                           multiplier * physicalBonus.strengthModifier);
    breakdown.weaponSpecialization = toNativeUnsignedByte(
        multiplier * physicalBonus.weaponSpecialization);
    breakdown.otherSpecialBonus = toNativeUnsignedByte(
        multiplier * damageBonus + massiveCriticalDamage);
    breakdown.criticalMultiplier = criticalConfirmed
                                       ? toNativeSignedByte(
                                             weapon.criticalHitMultiplier())
                                       : 0;

    DamageType type = getPrimaryDamageType(weapon.damageFlags());
    if (baseDamage > 0) {
        breakdown.addRawDamage(baseDamage, type);
    }
    damage.add(std::max(amount, 1), type);
    damage.setDamageFlags(weapon.damageFlags());
    const auto *targetCreature = dyn_cast<Creature>(&target);
    attacker.addPhysicalDamageModifiers(
        damage,
        breakdown,
        targetCreature,
        &weapon,
        offHand,
        multiplier);
    damage.setPower(attacker.calculateDamagePower(
        targetCreature,
        &weapon,
        offHand));
    if (toNativeSignedWord(amount) <= 0) {
        breakdown.addRawDamage(1, type);
    }

    debug(str(boost::format("computeWeaponDamage: %s -> %s (%d)") % attacker.tag() % target.tag() % damage.total()),
          LogChannel::Combat);
}

static int getUnarmedDamageDie(const Creature &attacker) {
    return attacker.size() <= CreatureSize::Small ? 2 : 1;
}

static void computeUnarmedDamage(
    const Creature &attacker, const Object &target,
    AttackResultType result, bool criticalConfirmed, int damageBonus,
    DamagePacket &damage, DamageBreakdown &breakdown) {

    int multiplier = (result == AttackResultType::CriticalHit) ? 2 : 1;
    PhysicalDamageBonus physicalBonus = attacker.getPhysicalDamageBonus(
        nullptr,
        false);

    int baseDamage = 0;
    int amount = multiplier * (
        damageBonus + physicalBonus.total());
    for (int multiple = 0; multiple < multiplier; ++multiple) {
        baseDamage += randomInt(1, getUnarmedDamageDie(attacker));
    }
    amount += baseDamage;

    int massiveCriticalDamage = attacker.getMassiveCriticalDamage(
        nullptr, result == AttackResultType::CriticalHit);
    amount += massiveCriticalDamage;

    breakdown.strengthModifier = toNativeSignedByte(
        multiplier * physicalBonus.strengthModifier);
    breakdown.weaponSpecialization = toNativeUnsignedByte(
        multiplier * physicalBonus.weaponSpecialization);
    breakdown.otherSpecialBonus = toNativeUnsignedByte(
        multiplier * damageBonus + massiveCriticalDamage);
    breakdown.criticalMultiplier = criticalConfirmed
                                       ? 2
                                       : 0;

    if (baseDamage > 0) {
        breakdown.addRawDamage(baseDamage, DamageType::Bludgeoning);
    }
    damage.add(std::max(amount, 1), DamageType::Bludgeoning);
    damage.setDamageFlags(static_cast<int>(DamageType::Bludgeoning));
    const auto *targetCreature = dyn_cast<Creature>(&target);
    attacker.addPhysicalDamageModifiers(
        damage,
        breakdown,
        targetCreature,
        nullptr,
        false,
        multiplier);
    damage.setPower(attacker.calculateDamagePower(
        targetCreature,
        nullptr,
        false));
    if (toNativeSignedWord(amount) <= 0) {
        breakdown.addRawDamage(1, DamageType::Bludgeoning);
    }

    debug(str(boost::format("computeUnarmedDamage: %s -> %s (%d)") % attacker.tag() % target.tag() % damage.total()),
          LogChannel::Combat);
}

static bool grantsExtraMainHandAttack(FeatType feat) {
    switch (feat) {
    case FeatType::Flurry:
    case FeatType::ImprovedFlurry:
    case FeatType::WhirlwindAttack:
    case FeatType::RapidShot:
    case FeatType::ImprovedRapidShot:
    case FeatType::MultiShot:
        return true;
    default:
        return false;
    }
}

static int getSpecialAttackRollBonus(FeatType feat) {
    switch (feat) {
    case FeatType::PowerAttack:
    case FeatType::ImprovedPowerAttack:
    case FeatType::MasterPowerAttack:
        return -3;
    case FeatType::Flurry:
        return -4;
    case FeatType::ImprovedFlurry:
        return -2;
    case FeatType::WhirlwindAttack:
        return -1;
    default:
        return 0;
    }
}

static int getSpecialAttackDamageBonus(FeatType feat) {
    switch (feat) {
    case FeatType::PowerAttack:
        return 5;
    case FeatType::ImprovedPowerAttack:
        return 8;
    case FeatType::MasterPowerAttack:
        return 10;
    default:
        return 0;
    }
}

static int getSpecialAttackThreatBonus(
    FeatType feat,
    const Item *weapon) {

    int multiplier = 0;
    switch (feat) {
    case FeatType::CriticalStrike:
        multiplier = 1;
        break;
    case FeatType::ImprovedCriticalStrike:
        multiplier = 2;
        break;
    case FeatType::MasterCriticalStrike:
        multiplier = 3;
        break;
    default:
        break;
    }
    return multiplier * getBaseCriticalThreat(weapon);
}

static int getMeleeSpecialAttackDefensePenalty(FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
        return 5;
    case FeatType::Flurry:
        return 4;
    case FeatType::ImprovedFlurry:
        return 2;
    case FeatType::WhirlwindAttack:
        return 1;
    default:
        return 0;
    }
}

void AttackBuffer::addPhysicalAttacks(const Creature &attacker, const Object &target,
                                      FeatType feat) {
    _feat = feat;

    int mainHandAttacks = 1 + attacker.modifiedAttacks();
    if (grantsExtraMainHandAttack(feat)) {
        ++mainHandAttacks;
    }

    int attackRollBonus = getSpecialAttackRollBonus(feat);
    int damageBonus = getSpecialAttackDamageBonus(feat);

    auto main = attacker.getEquippedItem(InventorySlots::rightWeapon);
    if (!main) {
        auto gloves = attacker.getEquippedItem(InventorySlots::hands);
        int attackThreatBonus = getSpecialAttackThreatBonus(feat, gloves.get());
        for (int i = 0; i < mainHandAttacks; ++i) {
            addPhysicalAttack(
                attacker,
                target,
                nullptr,
                Source::Main,
                attackRollBonus,
                attackThreatBonus,
                damageBonus);
        }
        return;
    }

    int mainThreatBonus = getSpecialAttackThreatBonus(feat, main.get());
    for (int i = 0; i < mainHandAttacks; ++i) {
        addPhysicalAttack(
            attacker,
            target,
            main.get(),
            Source::Main,
            attackRollBonus,
            mainThreatBonus,
            damageBonus);
    }

    auto offhand = attacker.getOffhandAttackWeapon();
    if (offhand) {
        int offhandThreatBonus = getSpecialAttackThreatBonus(feat, offhand.get());
        addPhysicalAttack(
            attacker,
            target,
            offhand.get(),
            Source::Offhand,
            attackRollBonus,
            offhandThreatBonus,
            damageBonus);
    }
}

void AttackBuffer::resolveMeleeSpecialAttack(
    FeatType feat,
    Creature &attacker,
    Game &game) {

    if (_attacks.empty() || _attacks.front().ranged) {
        return;
    }

    int defensePenalty = getMeleeSpecialAttackDefensePenalty(feat);
    if (defensePenalty != 0) {
        auto effect = game.newEffect<ACDecreaseEffect>(
            defensePenalty,
            ACBonus::Dodge,
            kAllDamageTypeFlags);
        attacker.applyEffect(
            std::move(effect),
            DurationType::Temporary,
            kSpecialAttackDefensePenaltyDuration);
    }
}

void AttackBuffer::addPhysicalAttack(
    const Creature &attacker,
    const Object &target,
    const Item *weapon,
    Source source,
    int attackRollBonus,
    int attackThreatBonus,
    int damageBonus) {

    bool offHand = source == Source::Offhand;
    const auto *targetCreature = dyn_cast<Creature>(&target);
    AttackBonusBreakdown attackBonusBreakdown = attacker.getAttackBonusBreakdown(
        targetCreature,
        weapon,
        offHand);
    attackBonusBreakdown.featBonus = attackRollBonus;
    attackRollBonus = attackBonusBreakdown.total();

    auto handItem = weapon
                        ? std::shared_ptr<Item>()
                        : attacker.getEquippedItem(InventorySlots::hands);
    const Item *criticalWeapon = weapon ? weapon : handItem.get();
    int criticalThreat = getCriticalThreat(criticalWeapon, attackThreatBonus);
    int damageFlags = weapon
                          ? weapon->damageFlags()
                          : static_cast<int>(DamageType::Bludgeoning);

    AttackResolution resolution = computeAttack(
        attacker,
        target,
        attackRollBonus,
        criticalThreat,
        damageFlags,
        weapon && weapon->isRanged());

    Attack &attack = _attacks.emplace_back(
        source,
        weapon && weapon->isRanged(),
        std::move(attackBonusBreakdown));
    attack.result = resolution.result;
    attack.roll = resolution.roll;
    attack.defenseBreakdown = resolution.defenseBreakdown;
    attack.naturalTwenty = resolution.naturalTwenty;
    attack.naturalOne = resolution.naturalOne;
    attack.coupDeGrace = resolution.coupDeGrace;
    attack.criticalThreat.threshold = resolution.criticalThreatThreshold;
    attack.criticalThreat.threatened = resolution.criticalThreatened;
    attack.criticalThreat.confirmationRoll = resolution.criticalConfirmationRoll;
    attack.criticalThreat.confirmed = resolution.criticalConfirmed;

    if (!isAttackSuccessful(resolution.result)) {
        return;
    }

    if (attacker.game().isConversationActive() &&
        attacker.isPartyMember()) {
        return;
    }

    if (weapon) {
        computeWeaponDamage(
            attacker,
            target,
            *weapon,
            source,
            resolution.result,
            resolution.criticalConfirmed,
            damageBonus,
            attack.damage,
            attack.damageBreakdown);
    } else {
        computeUnarmedDamage(
            attacker,
            target,
            resolution.result,
            resolution.criticalConfirmed,
            damageBonus,
            attack.damage,
            attack.damageBreakdown);
    }
}

void AttackBuffer::resolveDamage(Object &target) {
    for (Attack &attack : _attacks) {
        if (attack.damage.empty()) {
            continue;
        }

        attack.damage.resolve(target);
    }
}

void AttackBuffer::resolve(Creature &attacker, Object &target) {
    if (_attacks.empty()) {
        throw std::logic_error("Physical attack buffer is empty");
    }

    resolveDamage(target);

    if (auto *targetCreature = dyn_cast<Creature>(&target)) {
        targetCreature->runAttackedScript(attacker.id());
    }
}

static void addMitigationFeedback(
    Game &game,
    ServicesView &services,
    const Creature &attacker,
    const Object &target,
    const DamagePacket &damage);

void AttackBuffer::applyEffects(
    Attack &attack,
    Creature &attacker,
    Object &target,
    Game &game) {

    if (!attack.ranged && !isAttackSuccessful(attack.result)) {
        game.floatingText().addMiss(attacker, target);
    }
    if (!attack.damage.empty()) {
        DamageEffect::ApplicationContext context;
        std::copy(
            attack.damageBreakdown.rawDamageSlots.begin(),
            attack.damageBreakdown.rawDamageSlots.end(),
            context.damageAmounts.begin());
        context.damageAmounts.back() = toNativeSignedWord(
            attack.damage.resolvedDamage());
        context.preResolved = true;
        context.suppressDamageShields = attack.ranged;

        auto effect = game.newEffect<DamageEffect>(
            std::move(attack.damage),
            attacker.id(),
            std::move(context));
        target.applyEffect(std::move(effect), DurationType::Instant);
    }
}

void AttackBuffer::signalAttack(
    Attack &attack,
    Game &game,
    ServicesView &services,
    Creature &attacker,
    Object &target) {

    addCombatFeedback(game, services, attacker, target, attack);
    addMitigationFeedback(
        game, services, attacker, target, attack.damage);
    applyEffects(attack, attacker, target, game);
}

void AttackBuffer::signal(
    Game &game,
    ServicesView &services,
    Creature &attacker,
    Object &target) {

    for (const Attack &attack : _attacks) {
        addCombatFeedback(game, services, attacker, target, attack);
        addMitigationFeedback(
            game, services, attacker, target, attack.damage);
    }
    for (Attack &attack : _attacks) {
        applyEffects(attack, attacker, target, game);
    }
}

void AttackBuffer::prepareMeleeSequence(
    const IAnimations &animations,
    const std::vector<std::string> &attackAnimations) {

    if (_attacks.empty()) {
        throw std::logic_error("Physical attack buffer is empty");
    }
    if (std::any_of(
            _attacks.begin(),
            _attacks.end(),
            [](const Attack &attack) { return attack.ranged; })) {
        throw std::logic_error("Ranged attack in melee sequence");
    }
    if (attackAnimations.size() != _attacks.size()) {
        throw std::logic_error(
            "Melee attack animation count does not match attack count");
    }

    for (size_t index = 0; index < _attacks.size(); ++index) {
        Attack &attack = _attacks[index];
        attack.impactTimeMilliseconds =
            animations.getMeleeImpactTime(attackAnimations[index], index);
        attack.meleeSignaled = false;
    }

    _pendingMeleeAttacks = _attacks.size();
    _meleeSequencePrepared = true;
}

size_t AttackBuffer::signalReadyMelee(
    int elapsedMilliseconds,
    Game &game,
    ServicesView &services,
    Creature &attacker,
    Object &target) {

    if (!_meleeSequencePrepared) {
        throw std::logic_error("Melee attack sequence is not prepared");
    }
    if (!hasPendingMelee()) {
        return 0;
    }

    std::vector<size_t> ready;
    ready.reserve(_pendingMeleeAttacks);
    for (size_t index = 0; index < _attacks.size(); ++index) {
        const Attack &attack = _attacks[index];
        if (attack.meleeSignaled ||
            elapsedMilliseconds < attack.impactTimeMilliseconds) {
            continue;
        }
        ready.push_back(index);
    }
    std::stable_sort(
        ready.begin(),
        ready.end(),
        [this](size_t left, size_t right) {
            return _attacks[left].impactTimeMilliseconds <
                   _attacks[right].impactTimeMilliseconds;
        });

    for (size_t index : ready) {
        Attack &attack = _attacks[index];
        signalAttack(attack, game, services, attacker, target);
        attack.meleeSignaled = true;
        --_pendingMeleeAttacks;
    }
    return ready.size();
}

int AttackBuffer::latestMeleeImpactMilliseconds() const {
    if (!_meleeSequencePrepared) {
        throw std::logic_error("Melee attack sequence is not prepared");
    }

    int latest = 0;
    for (const Attack &attack : _attacks) {
        latest = std::max(latest, attack.impactTimeMilliseconds);
    }
    return latest;
}

void AttackBuffer::discardPendingMelee() {
    if (_meleeSequencePrepared) {
        for (Attack &attack : _attacks) {
            attack.meleeSignaled = true;
        }
        _pendingMeleeAttacks = 0;
    }
}

bool AttackBuffer::hasPendingMelee() const {
    return _meleeSequencePrepared && _pendingMeleeAttacks != 0;
}

static constexpr float kCombatFeedbackRange2 = 900.0f;

static bool canReceiveCombatFeedback(
    const Creature &player,
    const Creature &subject) {

    return player.faction() == subject.faction() &&
           player.getSquareDistanceTo(subject) <= kCombatFeedbackRange2;
}

static constexpr int kStrRefAttackSummary = 42042;
static constexpr int kStrRefAttackSuccessVerb = 42043;
static constexpr int kStrRefAttackFailureVerb = 42044;
static constexpr int kStrRefAttackFeat = 42046;
static constexpr int kStrRefAttackRoll = 42119;
static constexpr int kStrRefAttackRollSuccess = 42133;
static constexpr int kStrRefAttackRollFailure = 42134;
static constexpr int kStrRefAttackBreakdown = 42146;
static constexpr int kStrRefCriticalThreatBreakdown = 42148;
static constexpr int kStrRefDefenseBreakdown = 42149;
static constexpr int kStrRefDamageBreakdown = 42150;
static constexpr int kStrRefStrengthModifier = 42154;
static constexpr int kStrRefOtherDamageBonus = 42155;
static constexpr int kStrRefSneakAttackDamage = 42156;
static constexpr int kStrRefDamageResistance = 1454;
static constexpr int kStrRefDamageReduction = 1455;
static constexpr int kStrRefFiniteDamageResistance = 1456;
static constexpr int kStrRefFiniteDamageReduction = 1457;
static constexpr int kStrRefDamageImmunity = 1458;
static constexpr int kStrRefConfirmedCritical = 1511;
static constexpr int kStrRefCriticalThreatConfirmed = 1392;
static constexpr int kStrRefCriticalThreatFailed = 1393;
static constexpr int kStrRefUniversalDamage = 1422;
static constexpr int kStrRefPhysicalDamage = 1423;
static constexpr int kStrRefAcidDamage = 1440;
static constexpr int kStrRefColdDamage = 1441;
static constexpr int kStrRefLightSideDamage = 1442;
static constexpr int kStrRefElectricalDamage = 1443;
static constexpr int kStrRefFireDamage = 1444;
static constexpr int kStrRefDarkSideDamage = 1445;
static constexpr int kStrRefSonicDamage = 1446;
static constexpr int kStrRefIonDamage = 1447;
static constexpr int kStrRefEnergyDamage = 1448;
static constexpr int kStrRefMainhand = 42314;
static constexpr int kStrRefOffhand = 42315;
static constexpr int kStrRefAttackRollComponent = 42316;
static constexpr int kStrRefMeleeOnRangedBonus = 42317;
static constexpr int kStrRefFeatAttackBonus = 42318;
static constexpr int kStrRefCloseProximityRangedBonus = 42330;
static constexpr int kStrRefWeaponFocusBonus = 42331;
static constexpr int kStrRefEffectBonus = 42332;
static constexpr int kStrRefDualWieldPenalty = 42333;
static constexpr int kStrRefSmallOffhandBonus = 42334;
static constexpr int kStrRefDefenseArmor = 42338;
static constexpr int kStrRefDefenseDexterity = 42339;
static constexpr int kStrRefDefenseClass = 42340;
static constexpr int kStrRefDefenseNatural = 42341;
static constexpr int kStrRefDefenseEffects = 42342;
static constexpr int kStrRefDefenseFeat = 42343;
static constexpr int kStrRefWeaponSpecializationDamage = 42363;
static constexpr int kStrRefDexterityModifier = 42375;
static constexpr int kStrRefCriticalDamageMultiplier = 42386;
static constexpr int kStrRefCoupDeGrace = 42303;
static constexpr int kStrRefAutomaticHit = 42390;
static constexpr int kStrRefAutomaticMiss = 42391;
static constexpr int kStrRefBaseAttackBonus = 42392;
static constexpr int kStrRefNativeDamageSlot2000 = 41902;
static constexpr int kStrRefDefenseDebilitated = 42427;
static constexpr int kStrRefImprovedToughnessDamage = 42433;
static constexpr int kStrRefWookieeEnduranceDamage = 42434;

static std::string getFeedbackString(
    Game &game,
    ServicesView &services,
    int strRef,
    std::initializer_list<std::pair<int, std::string>> tokens) {

    std::string text = services.resource.strings.getText(strRef);
    for (const auto &[token, value] : tokens) {
        text = game.substituteCustomToken(
            std::move(text),
            token,
            value);
    }
    return text;
}

static void appendDefenseComponent(
    Game &game,
    ServicesView &services,
    std::string &breakdown,
    int strRef,
    int value) {

    value = toNativeSignedByte(value);
    if (value == 0) {
        return;
    }
    breakdown += getFeedbackString(
        game,
        services,
        strRef,
        {{0, std::to_string(value)}});
}

static void appendDamageComponent(
    Game &game,
    ServicesView &services,
    std::string &breakdown,
    int strRef,
    int value) {

    if (value <= 0) {
        return;
    }
    if (!breakdown.empty()) {
        breakdown += " + ";
    }
    breakdown += getFeedbackString(
        game,
        services,
        strRef,
        {{0, std::to_string(value)}});
}

static void appendDamageModifier(
    Game &game,
    ServicesView &services,
    std::string &breakdown,
    const char *separator,
    int strRef,
    int value) {

    breakdown += separator;
    breakdown += getFeedbackString(
        game,
        services,
        strRef,
        {{0, std::to_string(value)}});
}

static std::string getDamageBreakdown(
    Game &game,
    ServicesView &services,
    const DamageBreakdown &values,
    const DamagePacket &damage) {

    const DamageResolution &resolution = damage.resolution();
    int finalDamage = toNativeSignedWord(resolution.finalDamage);
    if (finalDamage <= 0) {
        return {};
    }

    const auto &slots = values.rawDamageSlots;
    int physicalDamage = 0;
    for (int slot = 0; slot != 3; ++slot) {
        if (slots[slot] > 0) {
            physicalDamage += slots[slot];
        }
    }

    std::string breakdown;
    appendDamageComponent(
        game,
        services,
        breakdown,
        kStrRefEnergyDamage,
        slots[12]);
    appendDamageComponent(
        game,
        services,
        breakdown,
        kStrRefPhysicalDamage,
        physicalDamage);
    static constexpr std::pair<int, int> kDamageComponents[] {
        {3, kStrRefUniversalDamage},
        {4, kStrRefAcidDamage},
        {5, kStrRefColdDamage},
        {6, kStrRefLightSideDamage},
        {7, kStrRefElectricalDamage},
        {8, kStrRefFireDamage},
        {9, kStrRefDarkSideDamage},
        {10, kStrRefSonicDamage},
        {11, kStrRefIonDamage},
        {13, kStrRefNativeDamageSlot2000},
    };
    for (const auto &[slot, strRef] : kDamageComponents) {
        appendDamageComponent(
            game,
            services,
            breakdown,
            strRef,
            slots[slot]);
    }

    if (values.strengthModifier != 0) {
        appendDamageModifier(
            game,
            services,
            breakdown,
            " ",
            kStrRefStrengthModifier,
            values.strengthModifier);
    }
    if (values.weaponSpecialization != 0) {
        appendDamageModifier(
            game,
            services,
            breakdown,
            " ",
            kStrRefWeaponSpecializationDamage,
            values.weaponSpecialization);
    }
    if (values.otherSpecialBonus > 0) {
        appendDamageModifier(
            game,
            services,
            breakdown,
            " + ",
            kStrRefOtherDamageBonus,
            values.otherSpecialBonus);
    }
    if (values.sneakAttack > 0) {
        appendDamageModifier(
            game,
            services,
            breakdown,
            " + ",
            kStrRefSneakAttackDamage,
            values.sneakAttack);
    }

    int improvedToughness = toNativeUnsignedByte(
        resolution.improvedToughnessBonus);
    if (improvedToughness != 0) {
        appendDamageModifier(
            game,
            services,
            breakdown,
            " - ",
            kStrRefImprovedToughnessDamage,
            improvedToughness);
    }
    int wookieeEndurance = toNativeUnsignedByte(
        resolution.wookieeEnduranceBonus);
    if (wookieeEndurance != 0) {
        appendDamageModifier(
            game,
            services,
            breakdown,
            " - ",
            kStrRefWookieeEnduranceDamage,
            wookieeEndurance);
    }

    std::string finalText;
    if (values.criticalMultiplier > 0) {
        finalText = getFeedbackString(
            game,
            services,
            kStrRefCriticalDamageMultiplier,
            {{0, std::to_string(values.criticalMultiplier)}});
    }
    finalText += std::to_string(finalDamage);

    return getFeedbackString(
        game,
        services,
        kStrRefDamageBreakdown,
        {
            {0, finalText},
            {1, breakdown},
        });
}

static void addDamageBreakdownFeedback(
    Game &game,
    ServicesView &services,
    const DamageBreakdown &values,
    const DamagePacket &damage,
    int broadcasts) {

    if (damage.empty()) {
        return;
    }

    std::string breakdown = getDamageBreakdown(
        game,
        services,
        values,
        damage);
    if (breakdown.empty()) {
        return;
    }

    for (int broadcast = 0; broadcast < broadcasts; ++broadcast) {
        game.messageLog().add(
            MessageLog::kFeedbackMessageType,
            MessageLog::Style::Normal,
            breakdown);
    }
}

static std::string getMitigationDamageTypeName(
    ServicesView &services,
    int damageFlags) {

    if ((damageFlags & static_cast<int>(DamageType::Physical)) != 0) {
        return services.resource.strings.getText(kStrRefPhysicalDamage);
    }

    static constexpr std::pair<int, int> kDamageTypes[] {
        {0x0008, kStrRefUniversalDamage},
        {0x0010, kStrRefAcidDamage},
        {0x0020, kStrRefColdDamage},
        {0x0040, kStrRefLightSideDamage},
        {0x0080, kStrRefElectricalDamage},
        {0x0100, kStrRefFireDamage},
        {0x0200, kStrRefDarkSideDamage},
        {0x0400, kStrRefSonicDamage},
        {0x0800, kStrRefIonDamage},
        {0x1000, kStrRefEnergyDamage},
        {0x2000, kStrRefNativeDamageSlot2000},
    };
    for (const auto &[flag, strRef] : kDamageTypes) {
        if ((damageFlags & flag) != 0) {
            return services.resource.strings.getText(strRef);
        }
    }
    return {};
}

static std::string getMitigationFeedback(
    Game &game,
    ServicesView &services,
    const Object &target,
    const MitigationFeedback &feedback) {

    const std::string &targetName = target.name();
    switch (feedback.type) {
    case MitigationFeedbackType::DamageImmunity:
        return getFeedbackString(
            game,
            services,
            kStrRefDamageImmunity,
            {
                {0, targetName},
                {1, getMitigationDamageTypeName(
                        services,
                        feedback.damageFlags)},
            });
    case MitigationFeedbackType::DamageResistance:
        return getFeedbackString(
            game,
            services,
            kStrRefDamageResistance,
            {
                {0, targetName},
                {1, std::to_string(feedback.amount)},
            });
    case MitigationFeedbackType::DamageReduction:
        return getFeedbackString(
            game,
            services,
            kStrRefDamageReduction,
            {
                {0, targetName},
                {1, std::to_string(feedback.amount)},
            });
    case MitigationFeedbackType::FiniteDamageResistance:
        if (!feedback.remaining) {
            throw std::logic_error(
                "Finite damage-resistance feedback has no remainder");
        }
        return getFeedbackString(
            game,
            services,
            kStrRefFiniteDamageResistance,
            {
                {0, targetName},
                {1, std::to_string(feedback.amount)},
                {2, std::to_string(*feedback.remaining)},
            });
    case MitigationFeedbackType::FiniteDamageReduction:
        if (!feedback.remaining) {
            throw std::logic_error(
                "Finite damage-reduction feedback has no remainder");
        }
        return getFeedbackString(
            game,
            services,
            kStrRefFiniteDamageReduction,
            {
                {0, targetName},
                {1, std::to_string(feedback.amount)},
                {2, std::to_string(*feedback.remaining)},
            });
    }
    throw std::logic_error("Invalid mitigation feedback type");
}

static void addMitigationFeedback(
    Game &game,
    ServicesView &services,
    const Creature &attacker,
    const Object &target,
    const DamagePacket &damage) {

    if (damage.empty()) {
        return;
    }

    auto leader = game.party().getLeader();
    if (!leader) {
        return;
    }

    const auto *targetCreature = dyn_cast<Creature>(&target);
    if (!targetCreature) {
        return;
    }

    bool targetHasRecipient = targetCreature->id() == leader->id();
    bool attackerHasRecipient = attacker.id() == leader->id();
    if (!targetHasRecipient && !attackerHasRecipient) {
        return;
    }

    for (const MitigationFeedback &feedback :
         damage.resolution().mitigationFeedback) {
        std::string text = getMitigationFeedback(
            game,
            services,
            target,
            feedback);
        if (targetHasRecipient) {
            game.messageLog().add(
                MessageLog::kFeedbackMessageType,
                MessageLog::Style::Normal,
                text);
        }
        if (attackerHasRecipient) {
            game.messageLog().add(
                MessageLog::kFeedbackMessageType,
                MessageLog::Style::Normal,
                text);
        }
    }
}

void AttackBuffer::addCombatFeedback(
    Game &game,
    ServicesView &services,
    const Creature &attacker,
    const Object &target,
    const Attack &attack) const {

    auto leader = game.party().getLeader();
    if (!leader) {
        return;
    }

    bool broadcastFromAttacker = canReceiveCombatFeedback(*leader, attacker);
    const auto *targetCreature = dyn_cast<Creature>(&target);
    bool broadcastFromTarget = targetCreature &&
                               canReceiveCombatFeedback(*leader, *targetCreature);
    int broadcasts = static_cast<int>(broadcastFromAttacker) +
                     static_cast<int>(broadcastFromTarget);
    if (broadcasts == 0) {
        return;
    }

    const std::string &attackerName = attacker.name();
    const std::string &targetName = target.name();
    int defense = toNativeSignedByte(attack.defenseBreakdown.total);

    bool successful = isAttackSuccessful(attack.result);
    std::string feedback = getFeedbackString(
        game,
        services,
        kStrRefAttackSummary,
        {
            {0, attackerName},
            {1, services.resource.strings.getText(
                    successful ? kStrRefAttackSuccessVerb : kStrRefAttackFailureVerb)},
            {2, targetName},
        });
    feedback += ". ";

    if (isPhysicalAttackFeat(_feat)) {
        auto feat = services.game.feats.get(_feat);
        feedback += getFeedbackString(
            game,
            services,
            kStrRefAttackFeat,
            {{0, feat->name}});
        feedback += ". ";
    }

    feedback += getFeedbackString(
        game,
        services,
        kStrRefAttackRoll,
        {
            {0, services.resource.strings.getText(
                    successful ? kStrRefAttackRollSuccess : kStrRefAttackRollFailure)},
            {1, std::to_string(
                    attack.roll + attack.attackBonusBreakdown.total())},
            {2, std::to_string(defense)},
            {3, std::to_string(
                    attack.damage.empty()
                        ? 0
                        : game.scaleDamageForDifficulty(
                              attack.damage.resolvedDamage(),
                              target))},
        });

    if (attack.coupDeGrace) {
        feedback += services.resource.strings.getText(kStrRefCoupDeGrace);
        feedback += " ";
    }
    if (attack.criticalThreat.confirmed) {
        feedback += services.resource.strings.getText(kStrRefConfirmedCritical);
        feedback += " ";
    }
    if (attack.naturalTwenty) {
        feedback += services.resource.strings.getText(kStrRefAutomaticHit);
        feedback += " ";
    }
    if (attack.naturalOne) {
        feedback += services.resource.strings.getText(kStrRefAutomaticMiss);
        feedback += " ";
    }

    for (int broadcast = 0; broadcast < broadcasts; ++broadcast) {
        game.messageLog().add(
            MessageLog::kFeedbackMessageType,
            MessageLog::Style::Combat,
            feedback);
    }

    const AttackBonusBreakdown &bonus = attack.attackBonusBreakdown;
    std::string breakdown = getFeedbackString(
        game,
        services,
        kStrRefAttackBreakdown,
        {
            {0, services.resource.strings.getText(
                    attack.source == Source::Main
                        ? kStrRefMainhand
                        : kStrRefOffhand)},
            {1, std::to_string(
                    attack.roll + attack.attackBonusBreakdown.total())},
        });
    breakdown += getFeedbackString(
        game,
        services,
        kStrRefAttackRollComponent,
        {{0, std::to_string(attack.roll)}});

    if (attack.naturalTwenty) {
        breakdown += " ";
        breakdown += services.resource.strings.getText(kStrRefAutomaticHit);
    } else if (attack.naturalOne) {
        breakdown += " ";
        breakdown += services.resource.strings.getText(kStrRefAutomaticMiss);
    } else {
        breakdown += getFeedbackString(
            game,
            services,
            kStrRefBaseAttackBonus,
            {{0, std::to_string(bonus.baseAttackBonus)}});

        if (bonus.dualWieldPenalty != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefDualWieldPenalty,
                {{0, std::to_string(bonus.dualWieldPenalty)}});
        }
        if (bonus.smallOffhandBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefSmallOffhandBonus,
                {{0, std::to_string(bonus.smallOffhandBonus)}});
        }
        if (_feat != FeatType::Invalid && bonus.featBonus != 0) {
            auto feat = services.game.feats.get(_feat);
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefFeatAttackBonus,
                {
                    {0, feat->name},
                    {1, std::to_string(bonus.featBonus)},
                });
        }
        if (bonus.duelingBonus != 0) {
            auto feat = services.game.feats.get(bonus.duelingFeat);
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefFeatAttackBonus,
                {
                    {0, feat->name},
                    {1, std::to_string(bonus.duelingBonus)},
                });
        }
        if (bonus.closeProximityRangedBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefCloseProximityRangedBonus,
                {{0, std::to_string(bonus.closeProximityRangedBonus)}});
        }
        if (bonus.meleeOnRangedBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefMeleeOnRangedBonus,
                {{0, std::to_string(bonus.meleeOnRangedBonus)}});
        }
        if (bonus.dexterityModifier != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefDexterityModifier,
                {{0, std::to_string(bonus.dexterityModifier)}});
        } else if (bonus.strengthModifier != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefStrengthModifier,
                {{0, std::to_string(bonus.strengthModifier)}});
        }
        if (bonus.weaponFocusBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefWeaponFocusBonus,
                {{0, std::to_string(bonus.weaponFocusBonus)}});
        }
        if (bonus.effectBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefEffectBonus,
                {{0, std::to_string(bonus.effectBonus)}});
        }
    }

    for (int broadcast = 0; broadcast < broadcasts; ++broadcast) {
        game.messageLog().add(
            MessageLog::kFeedbackMessageType,
            MessageLog::Style::Normal,
            breakdown);
    }

    if (attack.criticalThreat.threatened) {
        std::string criticalThreatBreakdown = getFeedbackString(
            game,
            services,
            kStrRefCriticalThreatBreakdown,
            {
                {0, std::to_string(attack.roll)},
                {1, std::to_string(attack.criticalThreat.threshold)},
                {2, services.resource.strings.getText(
                        attack.criticalThreat.confirmed
                            ? kStrRefCriticalThreatConfirmed
                            : kStrRefCriticalThreatFailed)},
                {3, std::to_string(
                        attack.criticalThreat.confirmationRoll + bonus.total())},
                {4, std::to_string(defense)},
            });

        for (int broadcast = 0; broadcast < broadcasts; ++broadcast) {
            game.messageLog().add(
                MessageLog::kFeedbackMessageType,
                MessageLog::Style::Normal,
                criticalThreatBreakdown);
        }
    }

    if (defense == 0) {
        addDamageBreakdownFeedback(
            game,
            services,
            attack.damageBreakdown,
            attack.damage,
            broadcasts);
        return;
    }

    const DefenseBreakdown &defenseValues = attack.defenseBreakdown;
    std::string defenseBreakdown = getFeedbackString(
        game,
        services,
        kStrRefDefenseBreakdown,
        {{0, std::to_string(defense)}});
    appendDefenseComponent(
        game,
        services,
        defenseBreakdown,
        kStrRefDefenseArmor,
        defenseValues.armor);
    appendDefenseComponent(
        game,
        services,
        defenseBreakdown,
        kStrRefDefenseDexterity,
        defenseValues.dexterity);
    appendDefenseComponent(
        game,
        services,
        defenseBreakdown,
        kStrRefDefenseClass,
        defenseValues.classDefense);
    appendDefenseComponent(
        game,
        services,
        defenseBreakdown,
        kStrRefDefenseNatural,
        defenseValues.natural);
    appendDefenseComponent(
        game,
        services,
        defenseBreakdown,
        kStrRefDefenseEffects,
        defenseValues.dodgeAndDeflection);
    appendDefenseComponent(
        game,
        services,
        defenseBreakdown,
        kStrRefDefenseFeat,
        defenseValues.feat);
    appendDefenseComponent(
        game,
        services,
        defenseBreakdown,
        kStrRefDefenseDebilitated,
        defenseValues.debilitationPenalty);

    for (int broadcast = 0; broadcast < broadcasts; ++broadcast) {
        game.messageLog().add(
            MessageLog::kFeedbackMessageType,
            MessageLog::Style::Normal,
            defenseBreakdown);
    }

    addDamageBreakdownFeedback(
        game,
        services,
        attack.damageBreakdown,
        attack.damage,
        broadcasts);
}

AttackResultType AttackBuffer::result() const {
    AttackResultType sorted[] {
        AttackResultType::Invalid,
        AttackResultType::Miss,
        AttackResultType::AttackResisted,
        AttackResultType::AttackFailed,
        AttackResultType::Parried,
        AttackResultType::Deflected,
        AttackResultType::HitSuccessful,
        AttackResultType::CriticalHit,
        AttackResultType::AutomaticHit,
    };
    ArrayRef<AttackResultType> sortedByScore(sorted);

    unsigned bestIndex = 0;

    for (const Attack &attack : _attacks) {
        for (unsigned i = 0; i < sortedByScore.size(); ++i) {
            if (sortedByScore[i] == attack.result) {
                bestIndex = std::max(bestIndex, i);
            }
        }
    }

    return sortedByScore[bestIndex];
}

std::shared_ptr<Item> determineProjectileWeapon(Creature &attacker, Projectile::Source source) {
    int slot = (source == Projectile::Main)
                   ? InventorySlots::rightWeapon
                   : InventorySlots::leftWeapon;

    std::shared_ptr<Item> weapon(attacker.getEquippedItem(slot));
    if (!weapon) {
        slot = (source == Projectile::Main)
                   ? InventorySlots::leftWeapon
                   : InventorySlots::rightWeapon;

        weapon = attacker.getEquippedItem(slot);
    }

    return weapon;
}

static glm::vec3 determineProjectileOrigin(scene::ModelSceneNode &model, Projectile::Source source) {
    std::string attachment = (source == Projectile::Main) ? "rhand" : "lhand";
    auto weaponModel = static_cast<scene::ModelSceneNode *>(model.getAttachment(attachment));
    if (weaponModel) {
        auto bulletHook = weaponModel->getNodeByName("bullethook");
        if (bulletHook) {
            return bulletHook->origin();
        }
        return weaponModel->origin();
    }

    // Droids do not have weapon model, but they have hooks in the main (body) model.
    std::string directAttachment = (source == Projectile::Main) ? "rbullet" : "lbullet";
    if (scene::SceneNode *direct = model.getNodeByName(directAttachment)) {
        return direct->origin();
    }

    return model.origin();
}

static std::optional<glm::vec3> determineMuzzleFlashOrigin(scene::ModelSceneNode &model, Projectile::Source source) {
    std::string attachment = (source == Projectile::Main) ? "rhand" : "lhand";
    auto weaponModel = static_cast<scene::ModelSceneNode *>(model.getAttachment(attachment));
    if (weaponModel) {
        if (auto muzzleHook = weaponModel->getNodeByName("muzzlehook")) {
            return muzzleHook->origin();
        }
    }
    return std::nullopt;
}

void Projectile::fire(Creature &attacker, Object &target, scene::ISceneGraph &sceneGraph) {
    auto attackerModel = std::static_pointer_cast<scene::ModelSceneNode>(attacker.sceneNode());
    auto targetModel = std::static_pointer_cast<scene::ModelSceneNode>(target.sceneNode());
    if (!attackerModel || !targetModel)
        return;

    std::shared_ptr<Item> weapon = determineProjectileWeapon(attacker, _source);
    if (!weapon)
        return;

    std::shared_ptr<Item::AmmunitionType> ammunitionType(weapon->ammunitionType());
    if (!ammunitionType)
        return;

    glm::vec3 projectilePos = determineProjectileOrigin(*attackerModel, _source);

    // Determine projectile direction
    auto impact = targetModel->getNodeByName("impact");
    if (impact) {
        _target = impact->origin();
    } else {
        _target = targetModel->origin();
    }

    if (_miss) {
        float offsetRadius = 1.5f * glm::length(targetModel->origin() - _target);
        glm::vec3 offsetDir = glm::normalize(
            glm::vec3(randomFloat(0.0f, 1.0f),
                      randomFloat(0.0f, 1.0f),
                      randomFloat(0.0f, 1.0f)));
        glm::vec3 offsetTarget = _target + offsetDir * offsetRadius;
        glm::vec3 dir = _target - projectilePos;
        _target = projectilePos + dir * 1000.0f;

        scene::Collision collision;
        if (sceneGraph.testLineOfSight(projectilePos, _target, collision)) {
            _target = collision.intersection;
        }
    }

    // Create and add a projectile to the scene graph
    _model = sceneGraph.newModel(*ammunitionType->model, scene::ModelUsage::Projectile);
    _model->signalEvent(kModelEventDetonate);
    _model->setLocalTransform(glm::translate(projectilePos));
    sceneGraph.addRoot(_model);

    if (ammunitionType->muzzleFlash) {
        glm::vec3 origin = determineMuzzleFlashOrigin(*attackerModel, _source)
                               .value_or(projectilePos);

        _flash = sceneGraph.newModel(*ammunitionType->muzzleFlash, scene::ModelUsage::Projectile);
        _flash->setLocalTransform(glm::translate(projectilePos));
        _flash->signalEvent(kModelEventDetonate);
        sceneGraph.addRoot(_flash);
    }

    // Play shot sound, if any
    weapon->playShotSound(0, projectilePos);
}

bool Projectile::update(float dt) {
    if (!_model) {
        return false;
    }

    glm::vec3 position = _model->origin();
    glm::vec3 vec = _target - position;
    float length = glm::length(vec);

    float dist = dt * kProjectileSpeed;
    if (dist >= length) {
        return true;
    }

    glm::vec3 dir = vec / length;
    position += dir * dist;

    float facing = glm::half_pi<float>() - glm::atan(dir.x, dir.y);

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, position);
    transform *= glm::eulerAngleZ(facing);

    _model->setLocalTransform(transform);

    return false;
}

void Projectile::reset() {
    if (!_model) {
        return;
    }

    _model->graph().removeRoot(*_model);
    _model.reset();
    _flash->graph().removeRoot(*_flash);
    _flash.reset();
}

void ProjectileSequence::push_back(float time, Projectile::Source source, bool miss) {
    _projectiles.emplace_back(source, miss);
    _events.push_back(time, _projectiles.size());
}

void ProjectileSequence::update(float dt, Creature &attacker, Object &target,
                                scene::ISceneGraph &sceneGraph) {
    // Update projectiles in flight
    for (Projectile &proj : _projectiles) {
        if (proj.update(dt)) {
            // Projectile hit the target
            proj.reset();
        }
    }

    // Fire new projectiles
    _events.update(dt);
    while (TimeEvents::Event ev = _events.next()) {
        size_t index = ev - 1;
        _projectiles[index].fire(attacker, target, sceneGraph);
    }
}

void ProjectileSequence::reset() {
    for (Projectile &proj : _projectiles) {
        proj.reset();
    }
}

static void addProjectile(ProjectileSequence &seq, std::pair<float, int> timeKind, bool miss) {
    float time = timeKind.first;
    float kind = timeKind.second;
    seq.push_back(time, kind == 0 ? Projectile::Main : Projectile::Offhand, miss);
}

void addProjectilesFromSpec(ProjectileSequence &seq, const ProjectileSpec &spec) {
    uint32_t remainingMisses = spec.misses;
    size_t numProjectiles = spec.projectiles.size();
    for (size_t i = 0; i < numProjectiles; ++i) {
        bool autoMiss = (i + remainingMisses) >= numProjectiles;
        bool miss = autoMiss || (remainingMisses && randomInt(0, 1));
        if (miss) {
            --remainingMisses;
        }
        addProjectile(seq, spec.projectiles[i], miss);
    }
}

// Standard physical attack actions pass 1500 to SetPauseTimer.
static constexpr int kPhysicalAttackPauseMilliseconds = 1500;

AttackSchedule::State AttackSchedule::update(
    const CombatRound &round, Action &action, float dt) {

    _time += dt;
    if (_melee && _state != AttackSchedule::WaitAttack) {
        float elapsedMilliseconds =
            dt * 1000.0f + _meleeElapsedRemainderMilliseconds;
        int wholeMilliseconds = static_cast<int>(elapsedMilliseconds);
        _meleeElapsedRemainderMilliseconds =
            elapsedMilliseconds - wholeMilliseconds;
        _meleeElapsedMilliseconds += wholeMilliseconds;
    }

    switch (_state) {
    case AttackSchedule::WaitAttack: {
        if (round.canExecute(action)) {
            _state = AttackSchedule::Attack;
        }
        break;
    }
    case AttackSchedule::Attack: {
        if (_melee &&
            _meleeElapsedMilliseconds >=
                _meleeCompletionMilliseconds) {
            _state = AttackSchedule::Damage;
        } else {
            _state = AttackSchedule::WaitDamage;
        }
        break;
    }
    case AttackSchedule::WaitDamage: {
        if ((_melee &&
             _meleeElapsedMilliseconds >=
                 _meleeCompletionMilliseconds) ||
            (!_melee && _time >= kAttackDamageDelay)) {
            _state = AttackSchedule::Damage;
        }
        break;
    }
    case AttackSchedule::Damage: {
        _state = AttackSchedule::WaitFinish;
        break;
    }
    case AttackSchedule::WaitFinish: {
        if (round.state == CombatRound::Finished) {
            _state = AttackSchedule::Finish;
        }
        break;
    }
    case AttackSchedule::Finish: {
        break;
    }
    }

    return _state;
}

void AttackSchedule::startMelee(int latestImpactMilliseconds) {
    if (_state != AttackSchedule::Attack) {
        throw std::logic_error("Melee attack schedule has not started");
    }

    _melee = true;
    _meleeElapsedMilliseconds = 0;
    _meleeCompletionMilliseconds = std::max(
        kPhysicalAttackPauseMilliseconds,
        latestImpactMilliseconds);
    _meleeElapsedRemainderMilliseconds = 0.0f;
}

bool navigateToAttackTarget(Creature &attacker, Object &target, float dt, bool &reachedOnce) {
    if (reachedOnce) {
        return true;
    }

    if (!attacker.navigateTo(target.position(), true, attacker.getAttackRange(), dt)) {
        return false;
    }

    reachedOnce = true;
    return true;
}

bool isCreatureCombat(
    const Creature &attacker,
    const Object &target) {

    if (attacker.modelType() == Creature::ModelType::Creature) {
        return true;
    }

    const auto *targetCreature = dyn_cast<Creature>(&target);
    return targetCreature &&
           targetCreature->modelType() == Creature::ModelType::Creature;
}

std::string selectPhysicalMeleeAttackAnimation(
    Creature &attacker,
    const Object &target,
    CreatureWieldType wield) {

    bool creatureCombat = isCreatureCombat(attacker, target);
    bool cinematic = !creatureCombat && isMeleeWieldType(wield);
    int variant = attacker.selectMeleeAttackVariant(cinematic);
    return formatPhysicalMeleeAttackAnimation(
        wield,
        variant,
        attacker.modelType() == Creature::ModelType::Creature,
        cinematic);
}

static bool hasAnim(const graphics::Model &model, const std::string &anim) {
    if (model.animations().count(anim)) {
        return true;
    }

    if (std::shared_ptr<graphics::Model> super = model.superModel()) {
        return hasAnim(*super, anim);
    }

    return false;
}

std::string getRangedAttackAnim(Creature &attacker, int kind) {
    CreatureWieldType wield = attacker.getWieldType();
    assert(isRangedWieldType(wield) && "invalid wield");

    auto attackerModel = std::static_pointer_cast<scene::ModelSceneNode>(attacker.sceneNode());
    const graphics::Model &model = attackerModel->model();

    std::string animByWield = str(boost::format("b%da%d") % static_cast<int>(wield) % kind);
    if (hasAnim(model, animByWield)) {
        return animByWield;
    }

    std::string animBasic = str(boost::format("b0a%d") % kind);
    if (hasAnim(model, animBasic)) {
        return animBasic;
    }

    return "";
}

} // namespace game

} // namespace reone
