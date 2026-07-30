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

#include "reone/game/di/services.h"
#include "reone/game/d20/feats.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/stunned.h"
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
static constexpr int kAllDamageTypes = 8199;
static constexpr float kSpecialAttackDefensePenaltyDuration = 3.0f;
static constexpr float kCriticalStrikeStunDuration = 6.0f;

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
        if (property.upgradeType != 0 ||
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

static bool hasStunImmunity(const Creature &target) {
    return hasEffectImmunity(target, ImmunityType::Stun) ||
           target.attributes().hasFeat(FeatType::ForceImmunityStun) ||
           target.attributes().hasFeat(FeatType::ForceImmunityParalysis);
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
    int attackBonus {0};
    int defense {0};
    int criticalThreat {0};
    int confirmationRoll {0};
    bool assuredHit {false};
    bool criticalHitImmune {false};
};

static AttackResolution computeAttack(
    const Creature &attacker,
    const Object &target,
    int attackBonus,
    int criticalThreat,
    int damageFlags) {

    AttackResolution resolution;
    resolution.attackBonus = attackBonus;
    resolution.criticalThreat = criticalThreat;

    // Determine defense of a target
    const auto *targetCreature = dyn_cast<Creature>(&target);
    resolution.defense = targetCreature
                             ? targetCreature->getDefense(&attacker, damageFlags)
                             : 0;

    // Attack roll
    resolution.roll = randomInt(1, 20);

    if (attacker.hasAssuredHit()) {
        resolution.result = AttackResultType::HitSuccessful;
        resolution.assuredHit = true;
        debug(str(boost::format("computeAttack: assured hit: roll(%d)") % resolution.roll),
              LogChannel::Combat);
        return resolution;
    }

    if (resolution.roll == 1) {
        resolution.result = AttackResultType::Miss;
        debug(str(boost::format("computeAttack: miss: roll(1)")), LogChannel::Combat);
        return resolution;
    }

    if (resolution.roll != 20 &&
        (resolution.roll + resolution.attackBonus) < resolution.defense) {
        resolution.result = AttackResultType::Miss;
        debug(str(boost::format("computeAttack: miss: roll(%d), bonus(%d), defense(%d)") %
                  resolution.roll % resolution.attackBonus % resolution.defense),
              LogChannel::Combat);
        return resolution;
    }

    // Critical threat
    if (resolution.roll >= (21 - resolution.criticalThreat)) {
        // Critical confirmation
        resolution.confirmationRoll = randomInt(1, 20);
        if ((resolution.confirmationRoll + resolution.attackBonus) >= resolution.defense) {
            resolution.criticalHitImmune =
                targetCreature && hasCriticalHitImmunity(*targetCreature);
            if (!resolution.criticalHitImmune) {
                resolution.result = AttackResultType::CriticalHit;
                debug(str(boost::format("computeAttack: critical hit: roll(%d), confirmation(%d),"
                                        " bonus(%d), defense(%d), critical threat(%d)") %
                          resolution.roll % resolution.confirmationRoll % resolution.attackBonus %
                          resolution.defense % resolution.criticalThreat),
                      LogChannel::Combat);
                return resolution;
            }
        }
    }

    resolution.result = AttackResultType::HitSuccessful;
    debug(str(boost::format("computeAttack: hit: roll(%d), bonus(%d), defense(%d),"
                            " critical threat(%d)") %
              resolution.roll % resolution.attackBonus % resolution.defense % resolution.criticalThreat),
          LogChannel::Combat);

    return resolution;
}

static DamageType getBaseDamageType(int damageFlags) {
    if (damageFlags <= 0) {
        return DamageType::Universal;
    }

    int type = 1;
    while (damageFlags > 1) {
        damageFlags >>= 1;
        type <<= 1;
    }
    return static_cast<DamageType>(type);
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
    int damageBonus, DamagePacket &damage) {

    int multiplier = result == AttackResultType::CriticalHit
                         ? std::max(weapon.criticalHitMultiplier(), 1)
                         : 1;
    bool offHand = source == AttackBuffer::Source::Offhand;

    int amount = multiplier * (
        damageBonus + attacker.getPhysicalDamageBonus(&weapon, offHand));
    if (!hasActiveItemProperty(weapon, ItemProperty::NoDamage)) {
        for (int multiple = 0; multiple < multiplier; ++multiple) {
            amount += rollDamageDice(weapon.numDice(), weapon.dieToRoll());
        }
    }

    amount += attacker.getMassiveCriticalDamage(
        &weapon, result == AttackResultType::CriticalHit);

    DamageType type = getBaseDamageType(weapon.damageFlags());
    damage.addBaseDamage(std::max(amount, 1), type);
    attacker.addPhysicalDamageModifiers(
        damage,
        dyn_cast<Creature>(&target),
        &weapon,
        offHand,
        multiplier);

    debug(str(boost::format("computeWeaponDamage: %s -> %s (%d)") % attacker.tag() % target.tag() % damage.total()),
          LogChannel::Combat);
}

static int getUnarmedDamageDie(const Creature &attacker) {
    return attacker.size() <= CreatureSize::Small ? 2 : 1;
}

static void computeUnarmedDamage(
    const Creature &attacker, const Object &target,
    AttackResultType result, int damageBonus, DamagePacket &damage) {

    int multiplier = (result == AttackResultType::CriticalHit) ? 2 : 1;

    int amount = multiplier * (
        damageBonus + attacker.getPhysicalDamageBonus(nullptr, false));
    for (int multiple = 0; multiple < multiplier; ++multiple) {
        amount += randomInt(1, getUnarmedDamageDie(attacker));
    }

    amount += attacker.getMassiveCriticalDamage(
        nullptr, result == AttackResultType::CriticalHit);

    damage.addBaseDamage(std::max(amount, 1), DamageType::Bludgeoning);
    attacker.addPhysicalDamageModifiers(
        damage,
        dyn_cast<Creature>(&target),
        nullptr,
        false,
        multiplier);

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

static bool isCriticalStrikeFeat(FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
        return true;
    default:
        return false;
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
            addUnarmedAttack(
                attacker, target,
                attackRollBonus, attackThreatBonus, damageBonus);
        }
        return;
    }

    int mainThreatBonus = getSpecialAttackThreatBonus(feat, main.get());
    for (int i = 0; i < mainHandAttacks; ++i) {
        addWeaponAttack(
            attacker, target, *main, AttackBuffer::Source::Main,
            attackRollBonus, mainThreatBonus, damageBonus);
    }

    auto offhand = attacker.getEquippedItem(InventorySlots::leftWeapon);
    if (main->weaponWield() == WeaponWield::DoubleBladedSword) {
        offhand = main;
    }
    if (offhand) {
        int offhandThreatBonus = getSpecialAttackThreatBonus(feat, offhand.get());
        addWeaponAttack(
            attacker, target, *offhand, AttackBuffer::Source::Offhand,
            attackRollBonus, offhandThreatBonus, damageBonus);
    }
}

void AttackBuffer::resolveMeleeSpecialAttack(
    FeatType feat,
    Creature &attacker,
    Object &target,
    Game &game) {

    if (_attacks.empty() || _attacks.front().ranged) {
        return;
    }

    int defensePenalty = getMeleeSpecialAttackDefensePenalty(feat);
    if (defensePenalty != 0) {
        auto effect = game.newEffect<ACDecreaseEffect>(
            defensePenalty,
            ACBonus::Dodge,
            kAllDamageTypes);
        attacker.applyEffect(
            std::move(effect),
            DurationType::Temporary,
            kSpecialAttackDefensePenaltyDuration);
    }

    if (!isCriticalStrikeFeat(feat) ||
        !isAttackSuccessful(_attacks.front().result)) {
        return;
    }

    auto *targetCreature = dyn_cast<Creature>(&target);
    if (!targetCreature || hasStunImmunity(*targetCreature)) {
        return;
    }

    int difficultyClass =
        attacker.attributes().getAggregateLevel() +
        attacker.attributes().getAbilityModifier(Ability::Strength);
    if (!targetCreature->rollFortitudeSave(difficultyClass)) {
        _attacks.front().stunTarget = true;
    }
}

void AttackBuffer::addWeaponAttack(
    const Creature &attacker, const Object &target, const Item &weapon,
    Source source, int attackRollBonus, int attackThreatBonus, int damageBonus) {

    const auto *targetCreature = dyn_cast<Creature>(&target);
    attackRollBonus += attacker.getAttackBonus(
        targetCreature,
        &weapon,
        source == AttackBuffer::Source::Offhand);
    int criticalThreat = getCriticalThreat(&weapon, attackThreatBonus);

    AttackResolution resolution = computeAttack(
        attacker,
        target,
        attackRollBonus,
        criticalThreat,
        weapon.damageFlags());

    _attacks.emplace_back(
        source,
        weapon.isRanged(),
        resolution.result,
        resolution.roll,
        resolution.attackBonus,
        resolution.defense,
        resolution.confirmationRoll,
        resolution.assuredHit,
        resolution.criticalHitImmune);

    if (!isAttackSuccessful(resolution.result)) {
        return;
    }

    computeWeaponDamage(attacker, target, weapon, source, resolution.result,
                        damageBonus, _attacks.back().damage);
}

void AttackBuffer::addUnarmedAttack(const Creature &attacker, const Object &target,
                                    int attackRollBonus, int attackThreatBonus, int damageBonus) {
    const auto *targetCreature = dyn_cast<Creature>(&target);
    attackRollBonus += attacker.getAttackBonus(
        targetCreature,
        nullptr,
        false);
    auto gloves = attacker.getEquippedItem(InventorySlots::hands);
    int criticalThreat = getCriticalThreat(gloves.get(), attackThreatBonus);

    AttackResolution resolution = computeAttack(
        attacker,
        target,
        attackRollBonus,
        criticalThreat,
        static_cast<int>(DamageType::Bludgeoning));

    _attacks.emplace_back(
        AttackBuffer::Source::Main,
        false,
        resolution.result,
        resolution.roll,
        resolution.attackBonus,
        resolution.defense,
        resolution.confirmationRoll,
        resolution.assuredHit,
        resolution.criticalHitImmune);

    if (!isAttackSuccessful(resolution.result)) {
        return;
    }

    computeUnarmedDamage(
        attacker, target, resolution.result, damageBonus, _attacks.back().damage);
}

void AttackBuffer::applyEffects(Creature &attacker, Object &target, Game &game) {
    for (Attack &attack : _attacks) {
        if (!attack.ranged && !isAttackSuccessful(attack.result)) {
            game.floatingText().addMiss(attacker, target);
        }
        if (!attack.damage.empty()) {
            auto effect = game.newEffect<DamageEffect>(
                std::move(attack.damage), attacker.id());
            target.applyEffect(std::move(effect), DurationType::Instant);
        }
        if (attack.stunTarget) {
            auto effect = game.newEffect<StunnedEffect>();
            target.applyEffect(
                std::move(effect),
                DurationType::Temporary,
                kCriticalStrikeStunDuration);
        }
    }
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

static void substituteFeedbackToken(
    std::string &text,
    int token,
    const std::string &value) {

    const std::string marker = "<CUSTOM" + std::to_string(token) + ">";
    size_t pos = 0;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
        text.replace(pos, marker.size(), value);
        pos += value.size();
    }
}

static std::string getFeedbackString(
    ServicesView &services,
    int strRef,
    std::initializer_list<std::pair<int, std::string>> tokens) {

    std::string text = services.resource.strings.getText(strRef);
    for (const auto &[token, value] : tokens) {
        substituteFeedbackToken(text, token, value);
    }
    return text;
}

void AttackBuffer::addCombatFeedback(
    Game &game,
    ServicesView &services,
    const Creature &attacker,
    const Object &target) const {

    if (game.isTSL()) {
        return;
    }

    auto player = game.party().player();
    if (!player) {
        return;
    }

    bool visible = canReceiveCombatFeedback(*player, attacker);
    if (const auto *targetCreature = dyn_cast<Creature>(&target)) {
        visible = visible || canReceiveCombatFeedback(*player, *targetCreature);
    }
    if (!visible) {
        return;
    }

    const std::string &attackerName = attacker.name();
    const std::string &targetName = target.name();

    for (const Attack &attack : _attacks) {
        bool successful = isAttackSuccessful(attack.result);
        std::string feedback = getFeedbackString(
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
            if (feat) {
                feedback += getFeedbackString(
                    services,
                    kStrRefAttackFeat,
                    {{0, feat->name}});
                feedback += ". ";
            }
        }

        feedback += getFeedbackString(
            services,
            kStrRefAttackRoll,
            {
                {0, services.resource.strings.getText(
                        successful ? kStrRefAttackRollSuccess : kStrRefAttackRollFailure)},
                {1, std::to_string(attack.roll + attack.attackBonus)},
                {2, std::to_string(attack.defense)},
                {3, std::to_string(std::max(attack.damage.baseDamage(), 0))},
            });

        game.messageLog().addCombat(std::move(feedback));
    }
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

AttackResultType AttackBuffer::lastResult() const {
    return _attacks.empty()
               ? AttackResultType::Invalid
               : _attacks.back().result;
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

AttackSchedule::State AttackSchedule::update(
    const CombatRound &round, Action &action, float dt) {

    _time += dt;

    switch (_state) {
    case AttackSchedule::WaitAttack: {
        if (round.canExecute(action)) {
            _state = AttackSchedule::Attack;
        }
        break;
    }
    case AttackSchedule::Attack: {
        _state = AttackSchedule::WaitDamage;
        break;
    }
    case AttackSchedule::WaitDamage: {
        if (_time >= kAttackDamageDelay) {
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
