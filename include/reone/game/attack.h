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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "reone/game/combatfeedback.h"
#include "reone/game/effect/damage.h"
#include "reone/game/runtimeref.h"
#include "reone/game/types.h"
#include "reone/script/types.h"
#include "reone/system/smallvector.h"

namespace reone {

namespace scene {
class ModelSceneNode;
class ISceneGraph;
} // namespace scene

namespace game {

class Action;
struct CombatRound;
class Creature;
class Effect;
class Game;
class IAnimations;
class Item;
class Object;
struct ServicesView;
struct SavedPhysicalAction;

/**
 * The attack type and mode of an attack record, which the attacked event
 * carries to its target and scripts read as the target's last attack type
 * and mode.
 */
struct AttackHistory {
    /** The last attack type scripts read for a Flurry. */
    static constexpr int kScriptAttackTypeFlurry = 9;
    /** The last attack type scripts read for a Rapid Shot. */
    static constexpr int kScriptAttackTypeRapidShot = 10;
    /** The last attack mode scripts read for each combat mode from 1 to 6. */
    static constexpr std::array<int, 6> kScriptAttackModes {1, 2, 3, 0, 4, 5};

    /** The feat the attack uses, or zero for an ordinary attack. */
    int type {0};
    /** The attacker's combat mode. */
    int mode {0};

    /**
     * The last attack type as scripts read it: Flurry and Rapid Shot have a
     * value of their own, and any other attack, or a target out of combat,
     * reads zero.
     */
    int scriptType(bool inCombat) const {
        if (!inCombat) {
            return 0;
        }
        if (type == static_cast<int>(FeatType::Flurry)) {
            return kScriptAttackTypeFlurry;
        }
        if (type == static_cast<int>(FeatType::RapidShot)) {
            return kScriptAttackTypeRapidShot;
        }
        return 0;
    }

    /**
     * The last attack mode as scripts read it. A target out of combat reads
     * the invalid object id; a mode the table names reads as its entry, and
     * any other mode as zero.
     */
    int scriptMode(bool inCombat) const {
        if (!inCombat) {
            return static_cast<int>(kSavedEffectInvalidObjectId);
        }
        if (mode < 1 || mode > static_cast<int>(kScriptAttackModes.size())) {
            return 0;
        }
        return kScriptAttackModes[mode - 1];
    }
};

/**
 * The values of an attack record besides its type and mode (AttackHistory),
 * its target and its ammunition, which together make up a saved attack. The
 * attacked events of a round share the record and read its values when they
 * are delivered. The record reports the attack; it neither applies damage nor
 * shows reactions.
 */
struct AttackEventFields {
    /** The attack group, if the attack has one. */
    std::optional<int> group;
    /** The length of the round's animation in milliseconds. */
    int animationLength {0};
    /** How long a bolt bounced off the target flies to its miss point, in milliseconds. */
    int missedBy {0};
    /** The result of the attack. */
    AttackResultType result {AttackResultType::Invalid};
    /** How long a ranged attack's bolt flies to its endpoint, in milliseconds. */
    int reactionDelay {0};
    /** The target's reaction, if it has one. */
    std::optional<AttackReaction> reactionAnimation;
    /** How long the target's reaction takes, in milliseconds. */
    int reactionAnimationLength {0};
    /** The target's concealment. */
    int concealment {0};
    /** The attack is a ranged one. */
    bool ranged {false};
    /** The attack is a sneak attack. */
    bool sneakAttack {false};
    /** The attack's PhysicalAttackKind, as a number. */
    int weaponAttackType {0};
    /** Where a ranged attack's bolt ends. */
    std::array<float, 3> rangedTarget {};
    /** The damage the hit deals per damage slot; a slot the hit does not deal holds -1. */
    std::array<int, 15> damage;
    /** The hit kills its target. */
    bool killingBlow {false};
    /** The attack is a coup de grace. */
    bool coupDeGrace {false};
    /** The attack's critical threat. */
    int criticalThreat {0};
    /** The attack was deflected. */
    bool deflected {false};
    std::string attackDebugText;
    std::string damageDebugText;

    AttackEventFields() { damage.fill(DamageEffect::ApplicationContext::kAbsentDamageAmount); }
};

/**
 * The attackers of a creature as a script walks them. The first call takes the
 * creatures in the area that attack it, and each next call returns the
 * following one of them, whatever has become of it since.
 */
class AttackerList {
public:
    /**
     * Take the objects of \p objects that \p matches accepts, by the id \p id
     * gives each, and return the first.
     *
     * @return the first attacker's id, or script::kObjectInvalid when there is none
     */
    template <class Objects, class Matches, class Id>
    uint32_t first(const Objects &objects, Matches matches, Id id) {
        _ids.clear();
        for (const auto &object : objects) {
            if (matches(object)) {
                _ids.push_back(id(object));
            }
        }
        if (_ids.empty()) {
            return script::kObjectInvalid;
        }
        _next = 1;
        return _ids.front();
    }

    /** @return the next attacker's id, or script::kObjectInvalid past the last */
    uint32_t next() {
        return _next < _ids.size() ? _ids[_next++] : script::kObjectInvalid;
    }

    void clear() {
        _ids.clear();
        _next = 0;
    }

private:
    std::vector<uint32_t> _ids;
    size_t _next {0};
};

/** Identifies an item-on-hit effect subtype. */
enum class ItemOnHitSubtype {
    Sleep = 0,
    Stun = 1,
    Paralyze = 2,
    Confusion = 3,
    Fear = 4,
    Slow = 5,
    AbilityDrain = 6,
    ItemPoison = 7,
    SlayRG = 8,
    SlayAG = 9,
    InstantDeath = 10,
    Knockdown = 11,
};

/** Stores one active item property before attack resolution. */
struct ItemOnHitProperty {
    /** The item-on-hit effect subtype. */
    ItemOnHitSubtype subtype {ItemOnHitSubtype::Sleep};
    /** The activation chance as a percentage. */
    int chance {0};
    /** The effect duration in seconds. */
    float duration {0.0f};
    /** The saving throw difficulty class. */
    int difficultyClass {20};
    /** The required saving throw category. */
    SavingThrow savingThrow {SavingThrow::None};
    /** The saving throw subtype. */
    SavingThrowType savingThrowType {SavingThrowType::All};
    /** A subtype-specific value. */
    int parameter {0};
    /**
     * True when the subtype applies a creature state for a duration, which
     * then needs a chance and a duration; set from the subtype alone,
     * whatever tables its values came from.
     */
    bool appliesStateForDuration {false};
};

/**
 * The chance and duration that carry from one on-hit property of an item to
 * the next while its properties are read. A slay property keeps the chance
 * of the property before it, zero for the first. Which table a property's
 * values come from and how its effect applies are decided separately.
 */
struct ItemOnHitSelectionState {
    /** The activation chance as a percentage. */
    int chance {0};
    /** The duration in rounds. */
    int rounds {0};

    /** Ability drain, poison, instant death and knockdown always activate. */
    void select(ItemOnHitSubtype subtype) {
        if (subtype == ItemOnHitSubtype::AbilityDrain ||
            subtype == ItemOnHitSubtype::ItemPoison ||
            subtype == ItemOnHitSubtype::InstantDeath ||
            subtype == ItemOnHitSubtype::Knockdown) {
            chance = 100;
        }
    }
};

/** Sleep, stun, paralysis, confusion, fear and slow apply a creature state for a duration. */
constexpr bool appliesItemOnHitStateForDuration(ItemOnHitSubtype subtype) {
    return subtype >= ItemOnHitSubtype::Sleep && subtype <= ItemOnHitSubtype::Slow;
}

/** A slay racial group property names exactly one racial type; it has no wildcard. */
constexpr bool matchesSlayRacialGroup(int race, int parameter) {
    return race == parameter;
}

/** Stores one admitted item-on-hit effect until its impact resolves. */
struct ItemOnHitApplication {
    /** The item-on-hit effect subtype. */
    ItemOnHitSubtype subtype {ItemOnHitSubtype::Sleep};
    /** The effect duration in seconds. */
    float duration {0.0f};
    /** A subtype-specific value. */
    int parameter {0};
    /** Bound attacker retained across resolution and impact. */
    RuntimeObjectRef<Object> creator;
    /** The retained feedback values for the effect outcome. */
    EffectOutcomeBreakdown effectOutcome;
    /** True if the impact emits effect-outcome feedback; false otherwise. */
    bool emitEffectOutcome {false};
};

/**
 * Apply admitted item-on-hit effects to \p target in order. A target that is
 * not a creature, or is dead, takes none.
 */
void applyItemOnHitApplications(std::vector<ItemOnHitApplication> applications,
                                Object &target, Game &game, ServicesView &services);

/** Seconds from the start of an unclocked attack until its damage. */
static constexpr float kAttackDamageDelay = 1.0f;

/**
 * A scripted attack's forced values: the swing (an animations row; for a
 * ranged attacker the weapondischarge row), the result of every attack of the
 * round and the damage of each hit.
 */
struct CutsceneAttack {
    int animation {0};
    int result {static_cast<int>(AttackResultType::Miss)};
    int damage {0};
};

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
 * A running physical attack is over once its target is dead, or is a party
 * member down at zero vitality.
 */
bool isAttackTargetDown(const Object &target);

/** The terms of an attack's bonus, each reported on its own in the combat feedback. */
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
    int targetingBonus {0};
    int superiorWeaponFocusBonus {0};
    int formBonus {0};
    int dualStrikeBonus {0};
    int effectBonus {0};
    int inspireFollowersBonus {0};
    int leaderCharismaBonus {0};
    int crushOppositionPenalty {0};

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
               targetingBonus +
               superiorWeaponFocusBonus +
               formBonus +
               dualStrikeBonus +
               effectBonus +
               inspireFollowersBonus +
               leaderCharismaBonus +
               crushOppositionPenalty;
    }
};

/** A target's defense and the terms reported for it in the combat feedback. */
struct DefenseBreakdown {
    int total {0};
    int armor {0};
    int dexterity {0};
    int classDefense {0};
    int natural {0};
    int dodgeAndDeflection {0};
    int feat {0};
    int stance {0};
    int form {0};
    int debilitationPenalty {0};
};

/** The bonuses a physical attack adds to its damage. */
struct PhysicalDamageBonus {
    int damageAbilityModifier {0};
    int strengthModifier {0};
    int weaponSpecialization {0};
    int combatFeatDamage {0};
    int preciseShotDamage {0};
    int formDamage {0};
    int furyDamage {0};
    int combatModeDamage {0};
    /**
     * The d4 dice of the two unarmed feat families, feats 209-211 and
     * 212-219. They are rolled once per attack, and a critical hit multiplies
     * that roll.
     */
    int unarmedDice209 {0};
    int unarmedDice212 {0};

    /** The bonuses a critical hit multiplies, without the unarmed feat dice. */
    int total() const {
        return damageAbilityModifier + weaponSpecialization +
               combatFeatDamage + preciseShotDamage + formDamage + furyDamage +
               combatModeDamage;
    }
};

/** The terms of a hit's damage, reported in the combat feedback. */
struct DamageBreakdown {
    /** The base damage rolled per damage slot; a slot not rolled holds -1. */
    std::array<int, 14> rawDamageSlots;
    int strengthModifier {0};
    int otherSpecialBonus {0};
    int sneakAttack {0};
    int weaponSpecialization {0};
    int combatFeatDamage {0};
    int preciseShotDamage {0};
    int formDamage {0};
    int unarmedFeatDamage209 {0};
    int unarmedFeatDamage212 {0};
    int criticalMultiplier {0};

    DamageBreakdown();

    /**
     * Add \p amount to the slot of \p type. A slot that already holds damage
     * keeps at least 1; an empty one takes no less than zero.
     */
    void addRawDamage(int amount, DamageType type);
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
     *
     * @return false when an effect applied while the round is built retires or
     *         rebuilds this buffer; the caller must then neither animate the
     *         round nor publish its result
     */
    bool addPhysicalAttacks(Creature &attacker, Object &target,
                            FeatType feat = FeatType::Invalid);

    /**
     * Publish the round's summary on its attack records. Every hit is already
     * resolved, so nothing is mitigated or rolled again; the round's resolved
     * damage reaching the target's vitality adds to the attacker's fury bonus.
     */
    void resolve(Creature &attacker, Object &target);

    /**
     * Settle \p target's reaction to this round on its attack records.
     *
     * @return the reaction, or none
     */
    std::optional<AttackReaction> resolveReaction(const Creature &target, bool engaged, bool targetEngaged,
                                                  bool targetHasRoom, bool forced);

    /** Save the round's unreleased attacks, discharges and records into \p state. */
    void saveContinuation(SavedPhysicalAction &state, const Game &game) const;

    /** Restore the round saveContinuation() saved. */
    void restoreContinuation(const SavedPhysicalAction &state);

    /**
     * Time each melee attack's hit by the impact time of its animation in
     * \p attackAnimations, one per attack, and mark every hit pending.
     */
    void prepareMeleeSequence(
        const IAnimations &animations,
        const std::vector<std::string> &attackAnimations);

    /**
     * Release the pending melee hits whose impact time precedes
     * \p elapsedMilliseconds of the attack animation, in attack order and at
     * most one per call. At the end of the physical attack pause, release all
     * remaining ones.
     *
     * @return the number of hits released
     */
    size_t signalReadyMelee(
        int elapsedMilliseconds,
        Game &game,
        ServicesView &services,
        Creature &attacker,
        Object &target);
    /**
     * Release the ranged discharges of this round whose time precedes
     * \p elapsedMilliseconds of the attack animation, at most one per call.
     * At the end of the physical attack pause, release all remaining ones.
     *
     * @return the number of discharges released
     */
    size_t signalReadyRanged(
        int elapsedMilliseconds,
        Game &game,
        ServicesView &services,
        Creature &attacker,
        Object &target);

    /**
     * Drop the hits not yet released and the discharges not yet fired.
     * Released bolts and the work already queued for them remain.
     */
    void discardPending();

    /**
     * Retire the round: drop its pending work and clear its attack records in
     * place, so that undelivered events sharing them read cleared values.
     */
    void clearHistory();

    /** Clear the attack type of the round's records; released shots keep theirs. */
    void clearSpecialAttacks();

    bool hasPendingMelee() const;

    /**
     * Get the best result for a series of attacks collected in AttackBuffer.
     */
    AttackResultType result() const;

    size_t attackCount() const { return _attacks.size(); }

    /** The forced values of the buffer's cutscene rounds, if any. */
    const std::optional<CutsceneAttack> &cutscene() const { return _cutscene; }

    /**
     * The attack type of the record being resolved, else of the round's
     * record, else of the first attack.
     */
    int currentCombatAttackType() const {
        if (_resolvingHistory) {
            return _resolvingHistory->type;
        }
        if (_roundHistory) {
            return _roundHistory->type;
        }
        return _attacks.empty() ? 0 : _attacks.front().history->type;
    }

    bool hasPendingDischarges() const { return _nextDischarge < _discharges.size(); }

    /** The result the round's record holds: that of the last attack resolved. */
    AttackResultType recordResult() const {
        return _attacks.empty() ? AttackResultType::Invalid : _attacks.back().result;
    }

    /**
     * Result held by the round's record: the last released discharge, or
     * before any release the last one resolved, unless a touch attack's
     * deflection has overwritten it since.
     */
    AttackResultType currentRangedResult() const {
        if (_roundFields) {
            return _roundFields->result;
        }
        if (_discharges.empty()) {
            return AttackResultType::Invalid;
        }
        return _nextDischarge > 0 ? _discharges[_nextDischarge - 1].result : _discharges.back().result;
    }

    /**
     * The ranged record's attack score, the roll plus the modifier it last
     * took; none while the record is not a ranged one.
     */
    std::optional<int> rangedRecordScore() const {
        if (!_roundFields || !_roundFields->ranged) {
            return std::nullopt;
        }
        return _recordRoll + _recordModifier;
    }

    /** The record's weapon attack type: 1 for the right hand, 2 for the left. */
    int recordWeaponAttackType() const { return _roundFields ? _roundFields->weaponAttackType : 0; }

    /** The record's attack kills its target. */
    bool recordKillingBlow() const { return _roundFields && _roundFields->killingBlow; }

    /** Overwrite the result the round's record holds. */
    void setRecordResult(AttackResultType result) {
        if (_roundFields) {
            _roundFields->result = result;
        }
    }

    /** Every round of this buffer is a cutscene attack with these values. */
    void forceCutscene(const CutsceneAttack &cutscene) { _cutscene = cutscene; }

private:
    /** How an attack's critical threat was rolled and confirmed. */
    struct CriticalThreatBreakdown {
        int threshold {0};
        bool threatened {false};
        int confirmationRoll {0};
        int confirmationBonus {0};
        bool confirmed {false};
        int multiplier {0};
    };

    /**
     * One weapon discharge of a ranged round. A hit shot carries a logical
     * attack; the other discharges are presentation-only misses, which a
     * target may still parry or catch on a force shield.
     */
    struct Discharge {
        /** When the discharge fires, in milliseconds into the attack animation. */
        int timeMilliseconds {0};
        /** The firing hand: 0 for the right, 1 for the left. */
        int hand {0};
        /** The index of the attack the discharge carries, or -1 for a miss without one. */
        int attack {-1};
        AttackResultType result {AttackResultType::Miss};
        glm::vec3 endpoint {0.0f};
        /** The bolt's flight time in milliseconds. */
        int delayMilliseconds {0};
        /** The record's values when the discharge was resolved. */
        int attackType {0};
        int weaponAttackType {0};
        int recordRoll {0};
        int recordModifier {0};
    };

    /** One logical attack of the round, from its roll to the release of its hit. */
    struct Attack {
        Attack(
            Source source,
            bool ranged,
            AttackBonusBreakdown attackBonusBreakdown,
            DamagePacket damage = DamagePacket()) :
            source(source),
            ranged(ranged),
            attackBonusBreakdown(std::move(attackBonusBreakdown)),
            damage(std::move(damage)) {}

        std::shared_ptr<AttackHistory> history {std::make_shared<AttackHistory>()};
        std::shared_ptr<AttackEventFields> eventFields {std::make_shared<AttackEventFields>()};
        Source source;
        bool ranged;
        PhysicalAttackKind kind {PhysicalAttackKind::MainHand};
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
        /**
         * The roll hit, or was parried, deflected or shielded, before any
         * interception of a miss.
         */
        bool rolledHitLike {false};
        bool signaled {false};
        RuntimeObjectRef<Item> sourceItem;
        RuntimeObjectRef<Object> sourceActor;
        std::vector<DeferredCombatFeedback> deferredFeedback;
        std::vector<ItemOnHitApplication> onHitApplications;
        bool onHitResolved {false};
        DamagePacket damage;
        /**
         * Ordered target work: passive, selected post-roll, then post-damage
         * effects. Item on-hit applications retain their separate owner and
         * take effect before the target effects from effectsAfterOnHit on.
         */
        std::vector<EffectInstance> targetEffects;
        size_t effectsAfterOnHit {std::numeric_limits<size_t>::max()};
        /**
         * The last saving throw a feat or passive rolled, the effect code a
         * failed feat save records, and whether the hit reports the outcome.
         */
        EffectOutcomeBreakdown saveRecord;
        int effectCode {0};
        bool reportsEffectOutcome {false};
    };

    /**
     * Roll one attack of the round with \p weapon (none for an unarmed attack)
     * and resolve its hit up to the deferred release. \p dischargeWeapon is
     * the weapon in the hand that fires a ranged discharge.
     *
     * @return false when an effect applied meanwhile retires or rebuilds this buffer
     */
    bool addPhysicalAttack(
        Creature &attacker,
        Object &target,
        const std::shared_ptr<Item> &weapon,
        Source source,
        PhysicalAttackKind kind,
        size_t logicalIndex,
        FeatType feat,
        const std::shared_ptr<Item> &dischargeWeapon);

    /**
     * Build a ranged round: every discharge of the attack animation, of which
     * the round's attacks take random ones and the rest are misses.
     *
     * @return false when an effect applied meanwhile retires or rebuilds this buffer
     */
    bool addRangedAttacks(
        Creature &attacker,
        Object &target,
        const std::shared_ptr<Item> &main,
        const std::shared_ptr<Item> &offhand,
        int mainHandAttacks);

    /**
     * The first attack's work after its roll: the TSL passives, the feat's
     * remaining uses, its defense penalty and its saving throw effects.
     *
     * @return false when an effect applied meanwhile retires or rebuilds this buffer
     */
    bool resolvePostRoll(Attack &attack, size_t logicalIndex,
                         Creature &attacker, Object &target);

    /** Whether the record \p attack reads carries the sneak attack marker: the round's record when there is one. */
    bool recordSneakAttack(const Attack &attack) const;

    /** A TSL unarmed hit by a holder of feats 221-223 deals electrical damage and may slow its target. */
    void resolveUnarmedPassive(Attack &attack, Creature &attacker, Creature &target);

    /** A TSL unarmed hit by a holder of Echani Strike may knock its target down. */
    void resolveEchaniKnockdown(Attack &attack, Creature &attacker, Creature &target, bool special);

    /**
     * Roll \p target's Fortitude save against \p dc for a feat or passive of
     * \p attack, recording it for the hit's feedback.
     */
    SavingThrowResult rollDeferredSave(Attack &attack, const Creature &attacker, Creature &target, int dc);

    /** Append the hit visuals of the damage types the hit dealt, and of a deflection. */
    void appendHitVisuals(const Creature &attacker, Attack &attack);

    /** Report, once, the effect a feat of \p attack applied after a saving throw. */
    void reportEffectOutcome(Game &game, ServicesView &services, const Object &target, Attack &attack);

    /** A TSL ranged critical hit by a holder of feats 228-230 may assassinate its target. */
    void resolveAssassinatePassive(Attack &attack, Creature &attacker, Creature &target);

    /** Queue \p effect on the target of \p attack, to take effect when the hit is released. */
    void appendTargetEffect(Attack &attack, std::shared_ptr<Effect> effect,
                            DurationType durationType, float duration);

    /**
     * Mitigate the hit's damage against \p target and resolve its item
     * on-hit properties, hit visuals and coup de grace.
     *
     * @return false when an effect applied meanwhile retires or rebuilds this buffer
     */
    bool resolveDamage(const Creature &attacker, Object &target, Attack &attack);

    /** Roll the item on-hit properties of the attacking item once for a hit. */
    void resolveItemOnHitProperties(
        const Creature &attacker, Object &target, Attack &attack);

    /** Release a melee hit: its attacked event, feedback, damage and effects. */
    void signalAttack(
        Attack &attack,
        Game &game,
        ServicesView &services,
        Creature &attacker,
        Object &target);

    /** Fire a ranged discharge and release the attack it carries, if any. */
    void signalDischarge(
        const Discharge &discharge,
        Game &game,
        ServicesView &services,
        Creature &attacker,
        Object &target);

    /** Send a deflected bolt's damage back to its shooter as \p deflector's. */
    void returnDeflected(
        Attack &attack,
        Game &game,
        Creature &attacker,
        Creature &deflector,
        uint64_t impactTime);

    /** Queue the round's target effects and item on-hit work for \p impactTime. */
    void releaseTargetWork(
        Game &game,
        Creature &attacker,
        Object &target,
        uint64_t impactTime);

    /**
     * Take the feedback the round has gathered. It leaves with the round's
     * first release: a ranged round's first discharge, a melee round's first hit.
     */
    std::vector<DeferredCombatFeedback> takeDeferredFeedback();

    /** Send each piece of \p feedback from the attacker to the target at \p impactTime. */
    void releaseDeferredFeedback(
        Game &game,
        Creature &attacker,
        Object &target,
        uint64_t impactTime,
        std::vector<DeferredCombatFeedback> feedback);

    /** Queue the attacked event of \p attack, sharing the record \p history and \p fields. */
    void publishAttacked(
        const Attack &attack,
        const std::shared_ptr<AttackHistory> &history,
        const std::shared_ptr<AttackEventFields> &fields,
        Game &game,
        const Creature &attacker,
        const Object &target,
        uint64_t when);

    /** Queue the hit's damage on \p target for \p impactTime. */
    void applyDamage(
        Attack &attack,
        Creature &attacker,
        Object &target,
        Game &game,
        uint64_t impactTime);

    /**
     * Apply the hit's damage, then its target effects, with the item on-hit
     * effects \p onHit between those before and those after them.
     */
    void applyEffects(
        Attack &attack,
        Creature &attacker,
        Object &target,
        Game &game,
        ServicesView &services,
        uint64_t impactTime,
        std::vector<ItemOnHitApplication> onHit);

    /**
     * Report \p attack in the combat feedback of the party leader, once for
     * each of the attacker and the target in its faction and within range.
     */
    void addCombatFeedback(
        Game &game,
        ServicesView &services,
        const Creature &attacker,
        const Object &target,
        const Attack &attack) const;

    /** The feedback line that summarises \p attack: its result, feat, roll and suffixes. */
    std::string getAttackSummaryFeedback(
        Game &game,
        ServicesView &services,
        const Creature &attacker,
        const Object &target,
        const Attack &attack) const;

    /** The feedback line that breaks down the attack roll of \p attack. */
    std::string getAttackRollFeedback(Game &game, ServicesView &services, const Attack &attack) const;

    SmallVector<Attack, 8> _attacks;
    /**
     * Ranged rounds release their discharges in order; undischarged ones are
     * dropped when the round is cancelled.
     */
    SmallVector<Discharge, 12> _discharges;
    size_t _nextDischarge {0};
    /** The record's roll and the modifier added to it. They are not saved. */
    int _recordRoll {0};
    int _recordModifier {0};
    std::optional<CutsceneAttack> _cutscene;
    /** TSL times a cutscene hit by an impact row that does not exist. */
    bool _cutsceneHitsAtRelease {false};
    /**
     * The first hit-like discharge releases the round's target effects and
     * item on-hit work, whichever shot carried them.
     */
    bool _targetWorkReleased {false};
    /** The round's one attack record, shared by its queued events. */
    std::shared_ptr<AttackHistory> _roundHistory;
    std::shared_ptr<AttackEventFields> _roundFields;
    FeatType _feat {FeatType::Invalid};
    /** The record of the attack being resolved, while it is resolved. */
    std::shared_ptr<AttackHistory> _resolvingHistory;
    size_t _pendingMeleeAttacks {0};
    bool _meleeSequencePrepared {false};
    /**
     * Advances whenever the round is cancelled or rebuilt, so that a release
     * in progress stops.
     */
    uint64_t _signalGeneration {0};
};

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

    /**
     * Advance the schedule by \p dt of the round's time. A clocked attack
     * reaches its damage when the attack pause has run; an unclocked one
     * after kAttackDamageDelay.
     */
    State update(const CombatRound &round, Action &action, float dt);

    /** Start the attack pause clock for a melee round. */
    void startMelee();

    /** Start the attack pause clock for a ranged round. */
    void startRanged();

    /** Go straight to waiting for the round to finish, without attacking. */
    void skipAttacks() { _state = WaitFinish; }

    /** Save the schedule's phase and clock into \p state. */
    void saveContinuation(resource::Gff &state) const;

    /**
     * Restore the schedule saveContinuation() saved. A phase whose entry has
     * already run resumes waiting.
     */
    void restoreContinuation(const resource::Gff &state);

    /** The attack holds its round until its pause ends and its hits are out. */
    bool holdsCombatRound() const { return _state < Damage; }
    bool started() const { return _state != WaitAttack; }
    bool isMelee() const { return _melee; }
    bool isRanged() const { return _ranged; }
    int elapsedMilliseconds() const { return _elapsedMilliseconds; }

private:
    void startClock(int completionMilliseconds);
    bool clocked() const { return _melee || _ranged; }

    State _state {WaitAttack};
    float _time {0.0f};
    bool _melee {false};
    bool _ranged {false};
    int _elapsedMilliseconds {0};
    int _completionMilliseconds {0};
    float _elapsedRemainderMilliseconds {0.0f};
};

/** How far an attack has come in closing on its target. */
struct AttackApproach {
    /** The attacker has reached its target, or leapt at it. */
    bool reached {false};
    /** The Force Jump the attack became by leaping at its target. */
    FeatType forceJump {FeatType::Invalid};
    /**
     * Having turned on another enemy for want of sight, the attack no longer
     * closes on its target.
     */
    bool holdsGround {false};
    /** The attack is taken up where the attacker stands, but without a swing. */
    bool withoutSwing {false};
};

/** The outcome of one update of an attack closing on its target. */
enum class AttackApproachStep {
    Approaching,
    Reached,
    Ended
};

/**
 * One update of an attack closing on its target, until it has reached it.
 * A door or placeable is attacked at the attacker's use point for it, and
 * every distance and the sight line below are measured to that point. The
 * target is reached when it is in the attacker's area, within the attacker's
 * maximum attack range (a tenth of a metre more in TSL), in sight, and, for an
 * attacker outside the party, not nearer than the desired distance allows. In
 * TSL a door is attacked from its stand-off point, twice the desired distance
 * out from the door beyond its use point, and is reached only within the
 * desired distance of that point. Sight is a clear line from just above the
 * attacker's eyes to just below the target's, past nothing but the two of
 * them. An attacker out of sight remembers where it stood; the controlled
 * creature, finding itself out of sight again from the same spot, says so and
 * turns on the nearest enemy it can find, or, with none, drops its combat
 * modes and ends the attack. An attack that has turned so ends, with the
 * attacker's combat modes, whenever its target is out of reach or sight, as
 * does an attack on a target in no area that is not a creature, or by a
 * creature an encounter spawned. In TSL an attacker that cannot move takes up
 * its attack on a creature from where it stands, and swings only at one within
 * reach. Otherwise an attacker that \p mayLeap tries its Force Jump at a target in
 * sight; one too near walks back to the desired distance, or runs to a door's
 * stand-off point from farther than the square root of three metres; a TSL
 * party member set to fight at range or to stand its ground, unless
 * controlled, only turns toward the target; one farther than its closing
 * distance (its reach with a ranged weapon, the desired distance in melee,
 * and for a door or placeable the attacker's use range for it, a door
 * counting as the target of the attacker's own path) closes on the target
 * until the target is within the attacker's use range for it lengthened to
 * the maximum attack range, in a clear line; and one already that near turns
 * toward the target and waits for sight. A creature standing on the way back
 * to the desired distance cuts it short as a wall does.
 */
AttackApproachStep approachAttackTarget(Creature &attacker, Object &target, float dt, AttackApproach &approach,
                                        Game &game, const Action &parent, bool mayLeap);

/**
 * An attacker trained in Force Jump and holding a lightsaber leaps at a
 * creature in its sight ten metres away or more, with no one but the target
 * on the straight line to it, instead of closing on it: the Force jump is
 * applied to the attacker, and the attack becomes the highest Force Jump the
 * attacker knows, which is returned. Returns FeatType::Invalid, applying
 * nothing, when the attacker does not leap.
 */
FeatType resolveForceJumpAttack(Creature &attacker, Object &target);

/** Either side of the exchange shows a creature model rather than a character. */
bool isCreatureCombat(
    const Creature &attacker,
    const Object &target);

/**
 * The swing of a physical attack round as it is presented: one clip, however
 * many attacks the round holds.
 */
struct PhysicalAttackSwing {
    /**
     * The animation that times each logical melee attack's hit, or the one
     * ranged animation.
     */
    std::vector<std::string> animations;
    /** The clip the attacker shows and the target's reaction is keyed by. */
    std::string clip;
    /** The target is engaged in the exchange. */
    bool engaged {false};
    /** The exchange uses the engaged placeholder rather than the generic one. */
    bool engagedPlaceholder {false};
    /** The round is a ranged one. */
    bool ranged {false};
};

/**
 * Engage the target (unless \p engages is false), roll a physical attack
 * round and choose the attacker's swing. Returns nothing when an effect
 * applied meanwhile retired the buffer, and no animations when a melee round has no attacks or
 * the attack is taken up without a swing, which rolls nothing.
 */
std::optional<PhysicalAttackSwing> beginPhysicalAttack(
    Creature &attacker, Object &target,
    const IAnimations &animations, AttackBuffer &attacks,
    FeatType feat = FeatType::Invalid, bool engages = true, bool swings = true);

/**
 * Settle the target's reaction to a resolved round, then queue the attacker's
 * swing and the target's reaction.
 */
void presentPhysicalAttack(
    Creature &attacker, Object &target,
    const IAnimations &animations, AttackBuffer &attacks,
    const PhysicalAttackSwing &swing);

/**
 * The target's reaction to a physical attack: melee by the round's result,
 * ranged a dodge only when no roll hit, the target holds no lightsaber and is
 * engaged. A target unable to react has none. \p committed tells whether the
 * reaction takes the target's time; an uncommitted melee reaction is none.
 *
 * @return the reaction, or none
 */
std::optional<AttackReaction> resolveAttackReaction(const Creature &target, bool ranged, AttackResultType result,
                                                    bool anyHit, bool engaged, bool targetEngaged,
                                                    bool targetHasRoom, bool forced, bool &committed);

/**
 * An engaged exchange uses the engaged placeholder when a creature model takes
 * part or the pair is choreographed; everything else is generic.
 */
bool usesEngagedAttackPlaceholder(const Creature &attacker, const Object &target, bool engaged);

/**
 * Show \p reaction, if any, on a creature target, keyed by the clip of the
 * attacker's swing.
 */
void showAttackReaction(Creature &attacker, Object &target, const std::string &swing,
                        std::optional<AttackReaction> reaction, bool engagedPlaceholder, bool ranged,
                        const IAnimations &animations);

} // namespace game

} // namespace reone
