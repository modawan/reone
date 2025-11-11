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
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/scene/graph.h"
#include "reone/system/arrayref.h"
#include "reone/system/randomutil.h"

#include <algorithm>

namespace reone {

namespace game {

static constexpr char kModelEventDetonate[] = "detonate";
static constexpr float kProjectileSpeed = 16.0f;

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

static const char *attackResultDesc(AttackResultType type) {
    switch (type) {
    case AttackResultType::Miss:
        return "missed";
    case AttackResultType::AttackResisted:
        return "resisted";
    case AttackResultType::AttackFailed:
        return "failed";
    case AttackResultType::Parried:
        return "parried";
    case AttackResultType::Deflected:
        return "deflected";
    case AttackResultType::HitSuccessful:
        return "hit";
    case AttackResultType::AutomaticHit:
        return "automatic hit";
    case AttackResultType::CriticalHit:
        return "critical hit";
    case AttackResultType::Invalid:
        break;
    }
    return "invalid";
}

static int getWeaponAttackBonus(const Creature &attacker, const Item &weapon) {
    auto rightWeapon(attacker.getEquippedItem(InventorySlots::rightWeapon));
    auto leftWeapon(attacker.getEquippedItem(InventorySlots::leftWeapon));

    Ability ability = weapon.isRanged() ? Ability::Dexterity : Ability::Strength;
    int modifier = attacker.attributes().getAbilityModifier(ability);

    int penalty = 0;
    if (rightWeapon && leftWeapon) {
        // TODO: support Dueling and Two-Weapon Fighting feats
        bool offHand = (&weapon != rightWeapon.get());
        penalty = offHand ? 10 : 6;
    }

    int effects = attacker.attributes().getAggregateAttackBonus();

    debug(str(boost::format("getWeaponAttackBonus: modifier(%d) + effects(%d) - penalty(%d)") % modifier % effects % penalty),
          LogChannel::Combat);

    return modifier + effects - penalty;
}

AttackResultType computeWeaponAttack(
    const Creature &attacker, const Object &target, const Item &weapon,
    int rollBonus, int threatBonus) {

    // Determine defense of a target
    int defense;
    if (target.type() == ObjectType::Creature) {
        defense = static_cast<const Creature &>(target).getDefense();
    } else {
        defense = 10;
    }

    // Attack roll
    int roll = randomInt(1, 20);

    if (roll == 1) {
        debug(str(boost::format("computeWeaponAttack: miss: roll(1)")), LogChannel::Combat);
        return AttackResultType::Miss;
    }

    int bonus = getWeaponAttackBonus(attacker, weapon) + rollBonus;

    AttackResultType result;
    if (roll == 20) {
        result = AttackResultType::AutomaticHit;
    } else if ((roll + bonus) >= defense) {
        result = AttackResultType::HitSuccessful;
    } else {
        debug(str(boost::format("computeWeaponAttack: miss: roll(%d), bonus(%d), defense(%d)") % roll % bonus % defense), LogChannel::Combat);
        return AttackResultType::Miss;
    }

    // Critical threat
    int criticalThreat = weapon.criticalThreat() + threatBonus;
    if (roll > (20 - criticalThreat)) {
        // Critical hit roll
        int criticalRoll = randomInt(1, 20);
        if ((criticalRoll + bonus) >= defense) {
            debug(str(boost::format("computeWeaponAttack: critical hit: roll(%d), critical roll(%d),"
                                    " bonus(%d), defense(%d), critical threat(%d)") %
                      roll % criticalRoll % bonus % defense % criticalThreat),
                  LogChannel::Combat);
            return AttackResultType::CriticalHit;
        }
    }

    debug(str(boost::format("computeWeaponAttack: %s: roll(%d), bonus(%d), defense(%d), critical threat(%d)") % attackResultDesc(result) % roll % bonus % defense % criticalThreat));

    return result;
}

void computeWeaponDamage(
    const Creature &attacker, const Object &target, const Item &weapon,
    AttackResultType result, int damageBonus, ISmallVector<Damage> &damage) {

    int criticalHitMultiplier = weapon.criticalHitMultiplier();
    int multiplier = result == AttackResultType::CriticalHit
                         ? criticalHitMultiplier
                         : 1;

    int amount = damageBonus;
    for (int i = 0; i < weapon.numDice(); ++i) {
        amount += randomInt(1, weapon.dieToRoll());
    }

    DamageType type = static_cast<DamageType>(weapon.damageFlags());

    // FIXME: weapon damage may have multiple damage effects (1d8 energy + 1d4
    // sonic, for example).
    damage.push_back({multiplier * amount, type, DamagePower::Normal});

    debug(str(boost::format("computeWeaponDamage: %s -> %s (%d)") % attacker.tag() % target.tag() % amount),
          LogChannel::Combat);
}

void AttackBuffer::addWeaponAttack(
    const Creature &attacker, const Object &target, const Item &weapon,
    int attackRollBonus, int attackThreatBonus, int damageBonus) {

    AttackResultType result = computeWeaponAttack(
        attacker, target, weapon, attackRollBonus, attackThreatBonus);

    _attacks.emplace_back(result);

    if (!isAttackSuccessful(result)) {
        return;
    }

    computeWeaponDamage(attacker, target, weapon, result,
                        damageBonus, _attacks.back().damage);
}

void AttackBuffer::applyEffects(Creature &attacker, Object &target, Game &game) {
    for (Attack &attack : _attacks) {
        for (Damage &damage : attack.damage) {
            auto effect = game.newEffect<DamageEffect>(
                damage.amount, damage.type, damage.power, attacker.id());

            target.applyEffect(std::move(effect), DurationType::Instant);
        }
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

void Projectile::fire(Creature &attacker, Object &target, scene::ISceneGraph &sceneGraph) {
    auto attackerModel = std::static_pointer_cast<scene::ModelSceneNode>(attacker.sceneNode());
    auto targetModel = std::static_pointer_cast<scene::ModelSceneNode>(target.sceneNode());
    if (!attackerModel || !targetModel)
        return;

    int slot = 0;
    const char *attachment = nullptr;

    switch (_source) {
    case Projectile::Main: {
        slot = InventorySlots::rightWeapon;
        attachment = "rhand";
        break;
    }
    case Projectile::Offhand:
        slot = InventorySlots::leftWeapon;
        attachment = "lhand";
        break;
    }
    assert(slot && attachment && "unhandled projectile source");

    std::shared_ptr<Item> weapon(attacker.getEquippedItem(slot));
    if (!weapon)
        return;

    std::shared_ptr<Item::AmmunitionType> ammunitionType(weapon->ammunitionType());
    if (!ammunitionType)
        return;

    auto weaponModel = static_cast<scene::ModelSceneNode *>(attackerModel->getAttachment(attachment));
    if (!weaponModel)
        return;

    // Determine projectile position
    glm::vec3 projectilePos;
    auto bulletHook = weaponModel->getNodeByName("bullethook");
    if (bulletHook) {
        projectilePos = bulletHook->origin();
    } else {
        projectilePos = weaponModel->origin();
    }

    // Determine projectile direction
    auto impact = targetModel->getNodeByName("impact");
    if (impact) {
        _target = impact->origin();
    } else {
        _target = targetModel->origin();
    }

    // Create and add a projectile to the scene graph
    _model = sceneGraph.newModel(*ammunitionType->model, scene::ModelUsage::Projectile);
    _model->signalEvent(kModelEventDetonate);
    _model->setLocalTransform(glm::translate(projectilePos));
    sceneGraph.addRoot(_model);

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
}

void ProjectileSequence::push_back(float time, Projectile::Source source) {
    _projectiles.emplace_back(source);
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

} // namespace game

} // namespace reone
