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

#pragma once

#include "reone/game/effect/damage.h"
#include "reone/game/types.h"
#include "reone/system/smallvector.h"
#include "reone/system/timeevents.h"

namespace reone {

namespace scene {
class ModelSceneNode;
class ISceneGraph;
} // namespace scene

namespace game {

class Action;
class CombatRound;
class Creature;
class Game;
class Item;
class Object;
class ProjectileSpec;
class ServicesView;

static constexpr float kAttackDamageDelay = 1.0f;

/**
 * Make an attack roll with a weapon.
 *
 * Attack bonus is calculated from \p attacker ability modifier (STR for melee,
 * DEX for ranged), \p weapon attack modifier, and \p rollBonus.
 *
 * Critical hit is calculated from \p weapon threat and \p threatBonus. When an
 * attack roll is greater than (20 - threat), it is a critical hit.
 */
AttackResultType computeWeaponAttack(
    const Creature &attacker, const Object &target, const Item &weapon,
    int rollBonus = 0, int threatBonus = 0);

/**
 * Predicate for melee weapon.
 *
 * Stun Baton and Hand-to-Hand is NOT a melee weapon. These require special
 * animations that are different from regular melee weapons.
 */
bool isMeleeWieldType(CreatureWieldType type);

/**
 * Predicate for ranged weapon: blasters, rifles, etc.
 */
bool isRangedWieldType(CreatureWieldType type);

/**
 * Returns true if \p result is HitSuccessful, CriticalHit, AutomaticHit.
 * The rest are variouns forms for failed attacks (Parried, Deflected, etc.)
 */
bool isAttackSuccessful(AttackResultType result);

/**
 * Predicate for feats that resolve as a physical weapon or unarmed attack.
 */
bool isPhysicalAttackFeat(FeatType feat);

/**
 * Make and collect multiple attacks, but delay damage effects until later.
 */
class AttackBuffer {
public:
    enum class Source {
        Main,
        Offhand,
    };

    /**
     * Construct an ordered physical attack round from the attacker's equipped
     * weapons, active effects, and optional combat feat.
     */
    void addPhysicalAttacks(const Creature &attacker, const Object &target,
                            FeatType feat = FeatType::Invalid);

    /**
     * Apply the once-per-action effects of a melee combat feat.
     */
    void resolveMeleeSpecialAttack(
        FeatType feat,
        Creature &attacker,
        Object &target,
        Game &game);

    /**
     * Calculate an attack roll and base damage packet for a weapon attack.
     * The packet is retained until applyEffects().
     */
    void addWeaponAttack(const Creature &attacker, const Object &target, const Item &weapon,
                         Source source = Source::Main,
                         int attackRollBonus = 0,
                         int attackThreatBonus = 0,
                         int damageBonus = 0);

    /**
     * Calculate an attack roll and base damage packet for an unarmed attack.
     * The packet is retained until applyEffects().
     */
    void addUnarmedAttack(const Creature &attacker, const Object &target,
                          int attackRollBonus = 0,
                          int attackThreatBonus = 0,
                          int damageBonus = 0);

    /**
     * Apply the delayed damage and secondary effects of each physical attack.
     */
    void applyEffects(Creature &attacker, Object &target, Game &game);

    /**
     * Add one combat-feedback entry for each collected physical attack.
     */
    void addCombatFeedback(
        Game &game,
        ServicesView &services,
        const Creature &attacker,
        const Object &target) const;

    /**
     * Get the best result for a series of attacks collected in AttackBuffer.
     */
    AttackResultType result() const;

    /**
     * Get the result of the final attack in the round.
     */
    AttackResultType lastResult() const;

private:
    struct Attack {
        Attack(
            Source source,
            bool ranged,
            AttackResultType result,
            int roll,
            int attackBonus,
            int defense,
            int confirmationRoll,
            bool assuredHit,
            bool criticalHitImmune) :
            source(source),
            ranged(ranged),
            result(result),
            roll(roll),
            attackBonus(attackBonus),
            defense(defense),
            confirmationRoll(confirmationRoll),
            assuredHit(assuredHit),
            criticalHitImmune(criticalHitImmune) {}

        Source source;
        bool ranged;
        AttackResultType result;
        int roll;
        int attackBonus;
        int defense;
        int confirmationRoll;
        bool assuredHit;
        bool criticalHitImmune;
        bool stunTarget {false};
        DamagePacket damage;
    };

    SmallVector<Attack, 8> _attacks;
    FeatType _feat {FeatType::Invalid};
};

/**
 * Projectile is a visual effect of a blaster shot flying from a weapon towards
 * a target on a straight line trajectory.
 */
class Projectile {
public:
    enum Source {
        Main,
        Offhand,
    };

    /**
     * Create a projectile that fires from either the main hand (from a single
     * blaster or a rifle) or the offhand (dual blasters).
     */
    explicit Projectile(Source source, bool miss) :
        _source(source), _miss(miss) {}

    ~Projectile() { reset(); }

    /**
     * Fires a projectile from \p attacker to \p target. This adds a new model
     * to the \p sceneGraph located at the weapon attachment slot.
     */
    void fire(Creature &attacker, Object &target, scene::ISceneGraph &sceneGraph);

    /**
     * Move the model created by fire() towards the target. When the target is
     * reached, return true. The caller may either continue calling update() to
     * keep the projectile flying in the same direction, or call reset() to
     * remove it.
     */
    bool update(float dt);

    /**
     * Remove the projectile model from the scene graph.
     */
    void reset();

private:
    Source _source;
    bool _miss;
    std::shared_ptr<scene::ModelSceneNode> _model;
    std::shared_ptr<scene::ModelSceneNode> _flash;
    glm::vec3 _target {0.0f};
};

/**
 * ProjectileSequence keeps track of multiple projectiles that are supposed to
 * fire at specific time points that match the animation.
 */
class ProjectileSequence {
public:
    /**
     * Add a projectile to the sequence.
     */
    void push_back(float time, Projectile::Source source, bool miss);

    /**
     * Keep track of time and fire projectiles when necessary. Remove
     * projectiles that reach the target.
     */
    void update(float dt, Creature &attacker, Object &target, scene::ISceneGraph &sceneGraph);

    /**
     * Remove all projectile models.
     */
    void reset();

    bool empty() const { return _projectiles.empty(); }

private:
    TimeEvents _events;
    SmallVector<Projectile, 16> _projectiles;
};

void addProjectilesFromSpec(ProjectileSequence &seq, const ProjectileSpec &spec);

class AttackSchedule {
public:
    enum State {
        WaitAttack,
        Attack,
        WaitDamage,
        Damage,
        WaitFinish,
        Finish,
    };

    State update(const CombatRound &round, Action &action, float dt);

private:
    State _state {WaitAttack};
    float _time {0.0f};
};

bool navigateToAttackTarget(Creature &attacker, Object &actor, float dt, bool &reachedOnce);

std::string getRangedAttackAnim(Creature &attacker, int kind);

} // namespace game

} // namespace reone
