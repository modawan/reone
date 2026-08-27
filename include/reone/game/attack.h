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

#include <array>
#include <cstdint>

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
class IAnimations;
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

struct AttackBonusBreakdown {
    int baseAttackBonus {0};
    int strengthModifier {0};
    int dexterityModifier {0};
    int dualWieldPenalty {0};
    int smallOffhandBonus {0};
    int featBonus {0};
    FeatType duelingFeat {FeatType::Invalid};
    int duelingBonus {0};
    int closeProximityRangedBonus {0};
    int meleeOnRangedBonus {0};
    int weaponFocusBonus {0};
    int effectBonus {0};

    int total() const {
        return baseAttackBonus +
               strengthModifier +
               dexterityModifier +
               dualWieldPenalty +
               smallOffhandBonus +
               featBonus +
               duelingBonus +
               closeProximityRangedBonus +
               meleeOnRangedBonus +
               weaponFocusBonus +
               effectBonus;
    }
};

struct DefenseBreakdown {
    int total {0};
    int armor {0};
    int dexterity {0};
    int classDefense {0};
    int natural {0};
    int dodgeAndDeflection {0};
    int feat {0};
    int debilitationPenalty {0};
};

struct PhysicalDamageBonus {
    int damageAbilityModifier {0};
    int strengthModifier {0};
    int weaponSpecialization {0};

    int total() const {
        return damageAbilityModifier + weaponSpecialization;
    }
};

struct DamageBreakdown {
    DamageBreakdown();

    void addRawDamage(int amount, DamageType type);

    std::array<int, 14> rawDamageSlots;
    int strengthModifier {0};
    int otherSpecialBonus {0};
    int sneakAttack {0};
    int weaponSpecialization {0};
    int criticalMultiplier {0};
};

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
        Game &game);

    void resolve(Creature &attacker, Object &target);
    void signal(
        Game &game,
        ServicesView &services,
        Creature &attacker,
        Object &target);

    size_t attackCount() const { return _attacks.size(); }
    void prepareMeleeSequence(
        const IAnimations &animations,
        const std::vector<std::string> &attackAnimations);
    size_t signalReadyMelee(
        int elapsedMilliseconds,
        Game &game,
        ServicesView &services,
        Creature &attacker,
        Object &target);
    int latestMeleeImpactMilliseconds() const;
    void discardPendingMelee();
    bool hasPendingMelee() const;

    /**
     * Get the best result for a series of attacks collected in AttackBuffer.
     */
    AttackResultType result() const;

private:
    struct CriticalThreatBreakdown {
        int threshold {0};
        bool threatened {false};
        int confirmationRoll {0};
        bool confirmed {false};
    };

    struct Attack {
        Attack(
            Source source,
            bool ranged,
            AttackBonusBreakdown attackBonusBreakdown) :
            source(source),
            ranged(ranged),
            attackBonusBreakdown(std::move(attackBonusBreakdown)) {}

        Source source;
        bool ranged;
        AttackResultType result {AttackResultType::Invalid};
        int roll {0};
        AttackBonusBreakdown attackBonusBreakdown;
        DefenseBreakdown defenseBreakdown;
        bool naturalTwenty {false};
        bool naturalOne {false};
        bool coupDeGrace {false};
        CriticalThreatBreakdown criticalThreat;
        DamageBreakdown damageBreakdown;
        int impactTimeMilliseconds {0};
        bool meleeSignaled {false};
        DamagePacket damage;
    };

    void addPhysicalAttack(
        const Creature &attacker,
        const Object &target,
        const Item *weapon,
        Source source,
        int attackRollBonus,
        int attackThreatBonus,
        int damageBonus);
    void resolveDamage(Object &target);
    void signalAttack(
        Attack &attack,
        Game &game,
        ServicesView &services,
        Creature &attacker,
        Object &target);
    void applyEffects(
        Attack &attack,
        Creature &attacker,
        Object &target,
        Game &game);
    void addCombatFeedback(
        Game &game,
        ServicesView &services,
        const Creature &attacker,
        const Object &target,
        const Attack &attack) const;

    SmallVector<Attack, 8> _attacks;
    FeatType _feat {FeatType::Invalid};
    size_t _pendingMeleeAttacks {0};
    bool _meleeSequencePrepared {false};
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
    void startMelee(int latestImpactMilliseconds);

    bool isMelee() const { return _melee; }
    int meleeElapsedMilliseconds() const { return _meleeElapsedMilliseconds; }

private:
    State _state {WaitAttack};
    float _time {0.0f};
    bool _melee {false};
    int _meleeElapsedMilliseconds {0};
    int _meleeCompletionMilliseconds {0};
    float _meleeElapsedRemainderMilliseconds {0.0f};
};

bool navigateToAttackTarget(Creature &attacker, Object &actor, float dt, bool &reachedOnce);

bool isCreatureCombat(
    const Creature &attacker,
    const Object &target);
std::string selectPhysicalMeleeAttackAnimation(
    Creature &attacker,
    const Object &target,
    CreatureWieldType wield);
std::string getRangedAttackAnim(Creature &attacker, int kind);

} // namespace game

} // namespace reone
