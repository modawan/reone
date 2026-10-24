/*
 * Copyright (c) 2026 The reone project contributors
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
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "reone/game/effect/damage.h"
#include "reone/game/types.h"

namespace reone {

namespace resource {

class Gff;

}

namespace game {

class Game;
class Creature;
class Object;
struct ServicesView;
struct SavingThrowBreakdown;
enum class CreatureState : int;

/**
 * The square of the distance within which combat lines about a creature reach
 * the controlled creature of its faction: 30 metres.
 */
constexpr float kCombatFeedbackRangeSquared = 900.0f;

/** Stores the displayed values for one ability-drain result. */
struct AbilityDrainFeedback {
    /** The affected ability. */
    Ability ability {Ability::Strength};
    /** The ability-score decrease. */
    int amount {0};
    /** The effect duration in seconds. */
    int durationSeconds {0};
};

/** Stores the displayed values for one saving throw. */
struct SavingThrowFeedback {
    /** The saving throw category. */
    SavingThrow savingThrow {SavingThrow::None};
    /** The base saving throw value. */
    int base {0};
    /** The total saving throw modifier. */
    int modifier {0};
    /** The d20 roll. */
    int roll {0};
    /** The difficulty class. */
    int difficultyClass {0};
    /** The subtype the saving throw was made against. */
    SavingThrowType savingThrowType {SavingThrowType::All};
};

/** Reports a target immune to the attacker's sneak attack. */
struct SneakAttackImmunityFeedback {
};

/** Reports a confirmed critical hit that the target's immunity turned into an ordinary hit. */
struct CriticalHitImmunityFeedback {
};

/** Tells the attacker that its ranged weapon cannot hurt the target. */
struct WeaponIneffectiveFeedback {
};

/**
 * One damage mitigation line about the attacker itself: its own defenses
 * against the damage its deflected shot brought back. The line names the
 * attacker.
 */
struct ReturnedMitigationFeedback {
    /** The attacker's mitigation of the returned damage. */
    MitigationFeedback feedback;
};

/**
 * Represents feedback that a round gathers while its attacks resolve and
 * sends with its first release, one queued feedback-log message each: a
 * ranged round's first discharge, or a melee round's first hit. Damage
 * mitigation lines are among them.
 */
using DeferredCombatFeedback = std::variant<
    AbilityDrainFeedback,
    SavingThrowFeedback,
    SneakAttackImmunityFeedback,
    MitigationFeedback,
    CriticalHitImmunityFeedback,
    WeaponIneffectiveFeedback,
    ReturnedMitigationFeedback>;

/**
 * A queued feedback-log message: one piece of a round's feedback, sent from
 * the attacker to the target and shown when the release that carries it
 * lands. The target and the attacker are the queued event's object and
 * caller.
 *
 * A saving throw is shown to the saver (the target) and to the attacker, and
 * an ineffective weapon only to the attacker. Every other line goes to both,
 * unless the message is for the target alone.
 */
struct CombatFeedbackMessage {
    /** The feedback the message carries. */
    DeferredCombatFeedback feedback;
    /**
     * Only the target is told. A door or placeable target passes the line on
     * to the attacker all the same. Rounds never send such a message; a saved
     * game can hold one.
     */
    bool targetOnly {false};

    /**
     * Reads a message as the game saves it in a queued event.
     *
     * @param gff the event's data
     * @param attackerId the event's caller, as saved: a mitigation line whose
     *        object is the caller is about the attacker itself
     * @return the message, or none for a message that shows no line here
     * @throws ValidationException when a value the message needs is missing
     */
    static std::optional<CombatFeedbackMessage> fromGff(const resource::Gff &gff, uint32_t attackerId);

    /**
     * Writes the message as the game saves it in a queued event.
     *
     * @param targetId the event's object, as saved
     * @param attackerId the event's caller, as saved
     */
    std::shared_ptr<resource::Gff> toGff(uint32_t targetId, uint32_t attackerId) const;
};

/** Stores effect-outcome values for combat feedback. */
struct EffectOutcomeBreakdown {
    /** The outcome of a record that holds none. */
    static constexpr int kNoOutcome = -1;

    /** True if the record contains an effect outcome; false otherwise. */
    bool present {false};
    /** The saving throw category used by the effect. */
    int saveType {0};
    /** The effect subtype displayed in feedback. */
    int effectType {0};
    /** The effect-specific saving throw mode. */
    int saveMode {0};
    /** The d20 roll. */
    int saveRoll {0};
    /** The total saving throw modifier. */
    int modifierTotal {0};
    /** The base saving throw value. */
    int baseSave {0};
    /** The final saving throw value. */
    int finalTotal {0};
    /** The difficulty class. */
    int difficultyClass {0};
    /** The SavingThrowResult of the effect's saving throw, or kNoOutcome. */
    int outcome {kNoOutcome};
};

/**
 * The string naming the effect a hit's feat applied, for the effect token of
 * its feedback lines.
 *
 * @param tsl whether the game is TSL, which adds knockdown (14) and four more
 *        effects to KotOR's ten
 * @param effectType the feat's effect type
 * @return the StrRef, or none for type zero, which names no effect: the token
 *         keeps what it last held. Any other unknown type is named by StrRef 0.
 */
constexpr std::optional<int> getEffectOutcomeLabelStrRef(bool tsl, int effectType) {
    if (effectType == 0) {
        return std::nullopt;
    }
    constexpr int labels[] {0, 42031, 42035, 42036, 42032, 42037,
                            42038, 42033, 42040, 42034, 42039};
    if (effectType > 0 && effectType <= 10) {
        return labels[effectType];
    }
    if (tsl) {
        switch (effectType) {
        case 14:
            return 48178;
        case 15:
            return 48541;
        case 16:
            return 42031;
        case 18:
            return 113790;
        case 19:
            return 124290;
        default:
            break;
        }
    }
    return 0;
}

/**
 * Whether combat lines about a creature reach the controlled creature: the
 * subject is of its faction and within kCombatFeedbackRangeSquared.
 *
 * @param controlled the creature the player controls
 * @param subject the creature the line is about
 */
bool canReceiveCombatFeedback(const Creature &controlled, const Creature &subject);

/**
 * The effect-application line of a hit whose feat attempted a saving throw,
 * shown once for each of the attacker and the target that can receive combat
 * feedback.
 *
 * @param game the game
 * @param attacker the creature whose hit applied the effect, if it still exists
 * @param target the object hit
 * @param values the effect's saving throw and outcome
 */
void addAttackEffectOutcomeFeedback(Game &game, const std::shared_ptr<Object> &attacker,
                                    const Object &target, const EffectOutcomeBreakdown &values);

/**
 * Shows a queued feedback-log message when it is delivered to the target.
 *
 * @param game the game
 * @param attacker the message's sender, if it still exists
 * @param target the object the message was sent to
 * @param message the message
 */
void showCombatFeedbackMessage(Game &game, const std::shared_ptr<Object> &attacker,
                               const Object &target, const CombatFeedbackMessage &message);

/**
 * Shows feedback at once rather than queueing it: each piece is shown as its
 * queued message would be.
 *
 * @param game the game
 * @param attacker the attacker, if any
 * @param target the object attacked or damaged
 * @param records the pieces of feedback
 */
void showCombatFeedback(Game &game, const std::shared_ptr<Object> &attacker,
                        const Object &target, const std::vector<DeferredCombatFeedback> &records);

/**
 * A saving-throw line about the saver, shown to the saver and to a different
 * creature the throw was made against, whichever is the controlled creature.
 *
 * @param game the game
 * @param saver the creature that made the saving throw
 * @param source the object the throw was made against, if any
 * @param savingThrow the saving throw category
 * @param breakdown the saver's base save and effect modifier
 * @param roll the d20 roll
 * @param difficultyClass the difficulty class
 */
void addSavingThrowFeedback(Game &game, const Creature &saver,
                            std::optional<std::reference_wrapper<const Object>> source,
                            SavingThrow savingThrow, const SavingThrowBreakdown &breakdown, int roll,
                            int difficultyClass);

/**
 * The target's immunity to poison, told to the target and to the creator,
 * whichever is the controlled creature. A creature that is both is told twice.
 *
 * @param game the game
 * @param creator the creator of the poison effect, if it still exists
 * @param target the immune creature
 */
void addPoisonImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target);

/**
 * The target's immunity to slowing, told to the creator and to the target,
 * whichever is the controlled creature. A creature that is both is told twice.
 *
 * @param game the game
 * @param creator the creator of the slowing effect, if it still exists
 * @param target the immune creature
 */
void addSlowImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target);

/**
 * The target's immunity to entanglement, told to the creator and to the
 * target, whichever is the controlled creature. A creature that is both is
 * told twice.
 *
 * @param game the game
 * @param creator the creator of the entangling effect, if it still exists
 * @param target the immune creature
 */
void addEntangleImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target);

/**
 * The target's immunity to a state, told to the creator and to the target,
 * whichever is the controlled creature. A creature that is both is told
 * twice. Droid confusion has no line.
 *
 * @param game the game
 * @param creator the creator of the state effect, if it still exists
 * @param target the immune creature
 * @param state the state the target is immune to
 */
void addStateImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target,
                              CreatureState state);
/**
 * The target's immunity to an effect, found by a link-immunity check: a line
 * naming the target, chosen by the immunity type, told to a creator that is a
 * creature and to the target, whichever is the controlled creature. A
 * creature that is both is told twice. Blindness, deafness, silence, curses,
 * dazing and death magic have StrRef 0; the other immunity types without a
 * line of their own report nothing.
 *
 * @param game the game
 * @param creator the creator of the effect, if it still exists
 * @param target the immune creature
 * @param immunity the immunity that refused the effect
 */
void addLinkImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target,
                             ImmunityType immunity);

/**
 * The spell-immunity line (1458): the target and the name of the spell it is
 * immune to, told to the target and to the caster, whichever is the
 * controlled creature. A creature that is both is told twice.
 *
 * @param game the game
 * @param target the immune creature
 * @param caster the object that cast the spell
 * @param spellName the spell's name
 */
void addSpellImmunityFeedback(Game &game, const Creature &target, const Object &caster,
                              const std::string &spellName);

/**
 * The hit-point healing line, told to the target and to a different creator,
 * whichever is the controlled creature.
 *
 * @param game the game
 * @param creator the creator of the healing effect, if it still exists
 * @param target the healed creature
 * @param amount the hit points restored
 */
void addHitPointHealingFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target,
                                int amount);

/**
 * The target's immunity to blindness, told to the creator and to the target,
 * whichever is the controlled creature. A creature that is both is told
 * twice. The line is StrRef 0, not one about blindness.
 *
 * @param game the game
 * @param creator the creator of the blinding effect, if it still exists
 * @param target the immune creature
 */
void addBlindnessImmunityFeedback(Game &game, const std::shared_ptr<Object> &creator, const Creature &target);

/**
 * The Force point healing line, shown only to the player controlling the
 * healed creature.
 *
 * @param game the game
 * @param target the healed creature
 * @param amount the Force points restored
 */
void addForceHealingFeedback(Game &game, const Creature &target, int amount);

/**
 * The poisoned line, shown when the target can receive combat feedback. The
 * target also plays its poisoned sound when it is within combat feedback
 * range of the controlled creature.
 *
 * @param game the game
 * @param target the poisoned creature
 * @param poisonNameStrRef the StrRef of the poison's name
 */
void addPoisonedFeedback(Game &game, Creature &target, int poisonNameStrRef);

/**
 * The dispel line: StrRef 0, the target and the names of the powers dispelled
 * from it, told to the target and to a different caster, whichever is the
 * controlled creature.
 *
 * @param game the game
 * @param services the services, for the spells' names
 * @param target the object the powers were dispelled from
 * @param caster the dispelling creature, if any
 * @param spellIds the dispelled powers
 */
void addDispelFeedback(Game &game, ServicesView &services, const Object &target,
                       std::optional<std::reference_wrapper<const Creature>> caster,
                       const std::vector<uint32_t> &spellIds);

/**
 * The damage line (1403): the creator, the damaged object, the total and the
 * amount of each damage type present; 1402, naming only the damaged object,
 * when there is no creator. Told to the controlled creature when it is on the
 * damaged creature's side, or on a creature creator's side of another faction;
 * for a damaged door or container, only on a creature creator's side.
 *
 * @param game the game
 * @param creator the object that created the damage, if any
 * @param damaged the damaged object
 * @param amounts the damage of each slot and the total; a slot that took no
 *        part in the damage is negative
 */
void addDamageFeedback(Game &game, const std::shared_ptr<Object> &creator, const Object &damaged,
                       const std::array<int, DamageEffect::kSlotCount> &amounts);
/**
 * The death-experience line (1407): the recipient, the victim and the award
 * as reported. TSL writes it to the combat list, KotOR to its only list.
 *
 * @param game the game
 * @param recipientName the name of the creature awarded the experience
 * @param victimName the name of the creature that died
 * @param reportedExperience the award as reported
 */
void addDeathExperienceFeedback(Game &game, const std::string &recipientName, const std::string &victimName,
                                int reportedExperience);

} // namespace game

} // namespace reone
