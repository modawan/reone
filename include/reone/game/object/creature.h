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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

#include "reone/audio/clip.h"
#include "reone/audio/source.h"
#include "reone/game/attack.h"
#include "reone/graphics/lipanimation.h"
#include "reone/resource/format/2dareader.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/parser/gff/git.h"
#include "reone/resource/strings.h"
#include "reone/resource/types.h"
#include "reone/scene/animeventlistener.h"
#include "reone/scene/node/model.h"
#include "reone/script/types.h"
#include "reone/system/timer.h"

#include "../d20/attributes.h"
#include "../d20/itemattributes.h"
#include "../forcerules.h"
#include "../menupresentation.h"
#include "../object.h"
#include "../pathfinder.h"
#include "../runtimeref.h"

#include "item.h"

namespace reone {

namespace resource {
class Gff;
class IGffs;
class TwoDA;
}

namespace game {

/**
 * An armour class effect as the armour class rules read it: its category, an
 * ACBonus, its amount, the race and alignment it answers, the damage it
 * covers, and whether it is a decrease.
 */
struct ArmorClassEffectData {
    static constexpr int kDodge = static_cast<int>(ACBonus::Dodge);
    static constexpr int kArmour = static_cast<int>(ACBonus::ArmourEnchantment);
    static constexpr int kShield = static_cast<int>(ACBonus::ShieldEnchantment);
    static constexpr int kDeflection = static_cast<int>(ACBonus::Deflection);
    static constexpr int kCategoryCount = 5;
    static constexpr int kAnyRace = static_cast<int>(RacialType::All);
    /** The selector of an effect against physical damage, as items grant it. */
    static constexpr int kPhysicalSelector = 0x4007;
    /** The selector of an effect against every damage type, as scripts grant it. */
    static constexpr int kAllDamageSelector = 0x2007;

    int category {kDodge};
    int amount {0};
    int race {kAnyRace};
    int lawChaos {0};
    int goodEvil {0};
    int selector {kPhysicalSelector};
    bool decrease {false};

    bool validCategory() const { return category >= kDodge && category < kCategoryCount; }
    bool unqualified() const { return race == kAnyRace && lawChaos == 0 && goodEvil == 0; }

    /**
     * Whether the effect counts toward the creature's general armour class.
     * TSL counts a dodge increase only against physical or every damage type.
     */
    bool cached(bool tsl) const {
        if (!validCategory() || !unqualified()) {
            return false;
        }
        return !tsl || decrease || category != kDodge || selector == kAllDamageSelector || selector == kPhysicalSelector;
    }

    /**
     * Whether the effect counts against an attacker of \p attackerRace and
     * \p attackerAlignment. An effect for every attacker against physical
     * damage is general, not conditional; the law-chaos qualifier is not
     * tested. The physical selector matches exactly, and the all-damage
     * selector matches no single damage type.
     */
    bool conditional(int attackerRace, int attackerAlignment, int damageFlags) const {
        if (race == kAnyRace && goodEvil == 0 && selector == kPhysicalSelector) {
            return false;
        }
        if ((race != kAnyRace && race != attackerRace) ||
            (goodEvil != 0 && goodEvil != attackerAlignment))
            return false;
        return selector == kPhysicalSelector || (selector == 1 && damageFlags == 0) ||
               (selector == 2 && damageFlags == 1) || (selector == 4 && damageFlags == 2);
    }
};

/**
 * Where armour class queries and removals start in the creature's effects:
 * the first armour class increase. It moves only while an increase exists, so
 * it keeps its place once the last increase is gone, and decrease-only scans
 * start there too.
 */
struct ArmorClassCursor {
    int position {0};

    template <class ReadType>
    void update(std::size_t count, ReadType type) {
        for (std::size_t i = 0; i < count; ++i) {
            if (type(i) == SavedEffectTypes::acIncrease) {
                position = static_cast<int>(i);
                return;
            }
        }
    }
};

/**
 * The largest amount of \p removed's category that the effects of its kind
 * left from the cursor on still give, \p removed itself excluded. Removing a
 * decrease tests the race alone; removing an increase also tests the
 * alignment. Neither tests the damage selector.
 */
template <class ReadType, class ReadEffect, class IsRemoved>
int remainingArmorClassMaximum(const ArmorClassCursor &cursor, std::size_t count,
                               const ArmorClassEffectData &removed, ReadType type,
                               ReadEffect effect, IsRemoved isRemoved) {
    const int kind = removed.decrease ? SavedEffectTypes::acDecrease : SavedEffectTypes::acIncrease;
    int maximum = 0;
    for (std::size_t i = cursor.position; i < count; ++i) {
        // A decrease removal does not skip increases before it either.
        if (type(i) != kind) {
            break;
        }
        if (isRemoved(i)) {
            continue;
        }
        const auto candidate = effect(i);
        if (candidate.category == removed.category && candidate.race == ArmorClassEffectData::kAnyRace &&
            (removed.decrease || candidate.unqualified())) {
            maximum = std::max(maximum, candidate.amount);
        }
    }
    return maximum;
}

/**
 * A creature's general armour class from effects, by category, kept as the
 * effects come and go. Dodge amounts add up; any other category keeps its
 * largest amount, and a removal takes what the remaining effects give at that
 * moment. The cache keeps that history, which a maximum taken afresh at query
 * time would not.
 */
struct ArmorClassCache {
    std::array<int, ArmorClassEffectData::kCategoryCount> increases {};
    std::array<int, ArmorClassEffectData::kCategoryCount> decreases {};

    void add(const ArmorClassEffectData &effect, bool tsl) {
        if (!effect.cached(tsl)) {
            return;
        }
        auto &value = (effect.decrease ? decreases : increases)[effect.category];
        if (effect.category == ArmorClassEffectData::kDodge) {
            value += effect.amount;
        } else if (effect.amount > value) {
            value = effect.amount;
        }
    }

    void remove(const ArmorClassEffectData &effect, bool tsl, int remainingMaximum) {
        if (!effect.cached(tsl)) {
            return;
        }
        auto &value = (effect.decrease ? decreases : increases)[effect.category];
        if (effect.category == ArmorClassEffectData::kDodge) {
            value -= effect.amount;
        } else {
            value = remainingMaximum;
        }
    }

    /** The dodge bonus, at most 10. */
    int dodge() const {
        return std::min(10, increases[ArmorClassEffectData::kDodge] - decreases[ArmorClassEffectData::kDodge]);
    }
};

/** The armour class effects that answer one attacker, added to the general ones. */
struct ConditionalArmorClass {
    int dodge {0};
    int deflection {0};
    int shieldDecrease {0};

    explicit ConditionalArmorClass(const ArmorClassCache &cache) :
        deflection(cache.increases[ArmorClassEffectData::kDeflection]) {}

    /**
     * An attacker's armour class leaves out conditional natural, armour and
     * shield increases. Shield decreases add to the remaining sum; deflection
     * decreases do not count.
     */
    void add(const ArmorClassEffectData &effect, int race, int alignment, int damageFlags) {
        if (!effect.conditional(race, alignment, damageFlags)) {
            return;
        }
        if (effect.category == ArmorClassEffectData::kDodge) {
            dodge += effect.decrease ? -effect.amount : effect.amount;
        }
        if (!effect.decrease && effect.category == ArmorClassEffectData::kDeflection) {
            deflection = std::max(deflection, effect.amount);
        }
        if (effect.decrease && effect.category == ArmorClassEffectData::kShield) {
            shieldDecrease = std::max(shieldDecrease, effect.amount);
        }
    }
};

/** The parts of a creature's armour class that effects move. */
struct ArmorClassParts {
    int natural {0};
    int armour {0};
    int shield {0};
    int dodgeAndDeflection {0};
};

/**
 * The armour class parts from the general cache and, \p versus an attacker,
 * the conditional effects. A dodge bonus counts only against a seen attacker.
 * A touch attack ignores natural, worn armour and shield AC and their
 * decreases, but keeps the armour and shield increases.
 */
inline ArmorClassParts armorClassParts(const ArmorClassCache &cache,
                                       const ConditionalArmorClass &conditional,
                                       int natural, int armour, int shield,
                                       bool versus, bool attackerSeen = true, bool touch = false) {
    constexpr int kNatural = static_cast<int>(ACBonus::Natural);
    constexpr int kArmour = ArmorClassEffectData::kArmour;
    constexpr int kShield = ArmorClassEffectData::kShield;
    constexpr int kDeflection = ArmorClassEffectData::kDeflection;
    ArmorClassParts result;
    if (touch) {
        result.armour = cache.increases[kArmour];
        result.shield = cache.increases[kShield];
    } else {
        result.natural = natural + cache.increases[kNatural] - cache.decreases[kNatural];
        result.armour = armour + cache.increases[kArmour] - cache.decreases[kArmour];
        result.shield = shield + cache.increases[kShield] - cache.decreases[kShield];
    }
    if (versus) {
        result.dodgeAndDeflection = (attackerSeen ? cache.dodge() : 0) + conditional.dodge +
                                    conditional.deflection + (touch ? 0 : conditional.shieldDecrease);
    } else {
        // The general armour class counts both deflection increases and decreases.
        result.dodgeAndDeflection = cache.dodge() + cache.increases[kDeflection] - cache.decreases[kDeflection];
    }
    return result;
}

struct PowerMenuStatus;
struct ContextAction;

class CreatureClass;
class Talent;

class DamagePacket;
class Trigger;
class Encounter;
struct Spell;
class ModuleSnapshotBuilder;
struct Race;
struct AttackBonusBreakdown;
struct DefenseBreakdown;
struct DeflectionBreakdown;
struct DamageBreakdown;
struct PhysicalDamageBonus;
struct DamageResolution;

/** A saving throw: the creature's base bonus and the modifier of the moment. */
struct SavingThrowBreakdown {
    int base {0};
    int modifier {0};
    int total() const { return base + modifier; }
};

/**
 * Where the clip a creature shows came from: its animation ID, -1 when
 * unknown, and whether it is a dialog animation.
 */
struct AnimationSource {
    int id {-1};
    bool dialog {false};
};

/**
 * The attack a swing shows: its target, the result its round's record holds,
 * whether it is ranged, the kind of attack the record holds (it picks the
 * weapon heard at the hit) and whether that attack is the killing blow.
 */
struct SwingAttack {
    uint32_t targetId {script::kObjectInvalid};
    AttackResultType result {AttackResultType::Invalid};
    bool ranged {false};
    PhysicalAttackKind weaponKind {PhysicalAttackKind::None};
    bool killingBlow {false};
};

class Creature : public Object, public scene::IAnimationEventListener {
public:
    /** The Fury damage bonus of a creature that is not in a Fury. */
    static constexpr int kNoFuryDamageBonus = -1;
    /** The most experience a creature holds; an addition past it is refused. */
    static constexpr int kMaxExperience = std::numeric_limits<int>::max();

    // AI state

    /**
     * The actions a creature's AI may take, as bits of its AI state. Applying
     * an AI-state effect keeps only the bits of its mask.
     */
    static constexpr int kAIStateMove = 0x02;
    /** Acting with the body: an attack needs it, and a dead creature keeps its talents only with it and moving. */
    static constexpr int kAIStateAct = 0x04;
    static constexpr int kAIStateCastPowers = 0x08;
    static constexpr int kAIStateAttack = 0x80;
    /** The further actions a held creature loses, which no creature rule consults. */
    static constexpr int kAIStateHeldActions = 0x10 | 0x40;
    /** An action every state and a knockdown take away, which no creature rule consults. */
    static constexpr int kAIStateFree = 0x100;

    /** A creature no effect restricts. */
    static constexpr int kAIStateUnrestricted = 0xffff;
    /** A dead creature, which takes no action. */
    static constexpr int kAIStateNone = 0;

    /** The mask every state and a knockdown lay. */
    static constexpr int kAIStateMaskRestrained = ~kAIStateFree;
    /** The mask of a state that roots the creature: stuns, choking, horror and the like. */
    static constexpr int kAIStateMaskRooted = ~kAIStateMove;
    /** The mask of paralysis. */
    static constexpr int kAIStateMaskParalyzed = ~(kAIStateMove | kAIStateAct | kAIStateCastPowers);
    /** The mask of sleep and of a Force push. */
    static constexpr int kAIStateMaskHeld =
        ~(kAIStateFree | kAIStateHeldActions | kAIStateCastPowers | kAIStateAct | kAIStateMove);

    // END AI state

    enum class ModelType {
        Creature,
        Droid,
        Character
    };

    enum class MovementType {
        None,
        Walk,
        Run
    };

    struct Perception {
        float sightRange {0.0f};
        float hearingRange {0.0f};
        std::map<uint32_t, RuntimeObjectRef<Object>> seen;
        std::map<uint32_t, RuntimeObjectRef<Object>> heard;
        /** Perceived creatures that are invisible to this one; kept while so. */
        std::map<uint32_t, RuntimeObjectRef<Object>> invisible;

        bool sees(uint32_t id) const {
            auto found = seen.find(id);
            return found != seen.end() && found->second.resolve() != nullptr;
        }
        bool hears(uint32_t id) const {
            auto found = heard.find(id);
            return found != heard.end() && found->second.resolve() != nullptr;
        }
        bool isInvisible(uint32_t id) const {
            auto found = invisible.find(id);
            return found != invisible.end() && found->second.resolve() != nullptr;
        }
        bool has(uint32_t id) const { return sees(id) || hears(id) || isInvisible(id); }
        void clear() {
            seen.clear();
            heard.clear();
            invisible.clear();
        }
    };

    /**
     * Detection rolls, kept between checks: the hider's stealth rolls and the
     * observer's awareness rolls, refreshed at most every 20 s of play.
     */
    struct PerceptionRolls {
        int hide {0};
        int moveSilently {0};
        int spot {0};
        int listen {0};
        float age {0.0f};
    };

    /**
     * How TSL balances a creature against the player: the autobalance row it
     * uses, 0 for none, and the player's level when it spawned, 0 when the
     * player's current level counts.
     */
    struct AutoBalanceContext {
        int multiplierSet {0};
        int playerLevelAtSpawn {0};
    };

    /**
     * The appearance's sound set (appearancesndset.2da): the weaponsounds.2da
     * row of its own weapons, 0 for none, its armour type and its fall sounds,
     * in lower case.
     */
    struct AppearanceSoundSet {
        int weapon {0};
        std::string armorType;
        std::string fallDirt;
        std::string fallHard;
        std::string fallMetal;
        std::string fallWater;
    };

    /** The creature's part in combat: whether it fights, how it was drawn in, and its targets. */
    struct CombatState {
        bool active {false};
        CombatActivation activationType {CombatActivation::None};
        bool expiryHeld {false};
        RuntimeObjectRef<Object> attackTarget;
        /** The target of the round most recently started, other than the creature itself. */
        RuntimeObjectRef<Object> roundTarget;
        RuntimeObjectRef<Object> attemptedAttackTarget;
        RuntimeObjectRef<Object> attemptedSpellTarget;
        /**
         * The target the player ordered the creature to attack while it was
         * attempting another, until its next round is taken on that target.
         * It is not saved, and leaving combat keeps it.
         */
        RuntimeObjectRef<Object> orderedAttackTarget;
        ActionType attackAction {ActionType::QueueEmpty};
        FeatType combatFeat {FeatType::Invalid};
        /** The power a cast took the current round with, 0 for an item use. */
        std::optional<int> roundSpell;
        /**
         * The current round has told the creature that its ranged weapon
         * cannot hurt its target. It is saved with the round.
         */
        bool weaponIneffectiveReported {false};
        Timer expiryTimer;
        float decisionScriptTime {3.0f};
    };

    Creature(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services);

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Creature;
    }

    /** \p object as a creature; none when it is not one. */
    static std::optional<std::reference_wrapper<const Creature>> asCreature(const Object &object);
    /** \p object as a creature; none when it is null or not a creature. */
    static std::optional<std::reference_wrapper<const Creature>> asCreature(const std::shared_ptr<Object> &object);

    /**
     * The creature template for resRef. A template that does not exist is
     * replaced by the substitute creature template, and resRef then names it;
     * null when neither exists.
     */
    static std::shared_ptr<resource::Gff> findTemplate(resource::IGffs &gffs, std::string &resRef);

    /** False, leaving the creature unloaded, when findTemplate finds nothing. */
    bool loadFromBlueprint(const std::string &resRef);
    void loadAppearance();
    /**
     * Set the creaturespeed.2da row and take its walk and run speeds. The
     * Default row takes the row the appearance's MOVERATE names. The Immobile
     * row takes the creature's legs away.
     */
    void setMovementRate(int row);
    int movementRateRow() const { return _walkRate; }

    /**
     * Put on a disguise's appearance, remembering the creature's own. A
     * disguise put on as the creature is loaded leaves it where it stands;
     * otherwise the creature moves to where its new size fits. Taking the
     * disguise off always moves it so.
     */
    void applyDisguiseAppearance(int appearance, bool place);
    /** Returns to the creature's own appearance after a disguise. */
    void removeDisguiseAppearance();
    /**
     * A lasting change of appearance, as armour and scripts make; none while
     * disguised. The creature then moves to where its new size fits.
     */
    void changeAppearance(int appearance);
    /**
     * TSL: body armour just put on, or none when armour was just taken off,
     * may change the creature's appearance for good.
     */
    void updateArmourAppearance(const std::shared_ptr<Item> &armour);

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    /**
     * Invalidate state that only exists while this retained creature inhabits
     * one Area. The owning Area supplies its still-live Pathfinder so path
     * handles are released before that owner is destroyed.
     *
     * @param retainedObjectIds ids of the session objects that outlive the Area
     */
    void retireAreaRuntime(
        Pathfinder &pathfinder,
        const std::set<uint32_t> &retainedObjectIds);

    void update(float dt) override;
    /** Runs the action queue; with the clock stopped the frame takes no time, so nothing turns. */
    void runActions() override;
    /**
     * The room, by its place in the area's layout, in which the creature's
     * ground was last found; -1 before any.
     */
    int groundRoomHint() const { return _groundRoomHint; }
    void setGroundRoomHint(int room) { _groundRoomHint = room; }

    void clearAllActions(bool force = false, bool evenUncommandable = false) override;
    /**
     * A player's order to the creature under control: its actions are
     * cleared, as forced, unless its queue is busy with a cast, an item use,
     * a stance or another task, which the order then follows.
     */
    void clearOrdersUnlessBusy();
    void teardownActions() override;
    void damage(
        int amount,
        const std::shared_ptr<Object> &damager) override;

    /**
     * Applies damage from a damage effect: a hit of no damage still draws the reaction and the
     * damaged script.
     */
    void applyDamageEffect(
        int amount,
        const std::shared_ptr<Object> &damager,
        std::optional<DamageReaction> reaction) override;

    /**
     * Adds experience. With two or more classes, every class other than the
     * favoured one that is two or more levels below the highest of those
     * classes takes a fifth of the amount, rounded down. A race without a
     * favoured class favours the highest-level class, the earliest on a tie;
     * five or more such classes take the whole amount. Experience only
     * grows: a negative amount, or a positive one that would take it past
     * kMaxExperience, changes nothing.
     */
    void giveXP(int amount);
    void setXP(int xp);

    /**
     * Plays a sound-set sound, replacing the creature's last one. A dead
     * creature, or a party member at zero vitality, only plays its death
     * sound.
     */
    void playSound(resource::SoundSetEntry entry, bool positional = true);
    /** The leader is in the creature's area within 30 m, where it hears the creature's voice. */
    bool isHeardByLeader() const;

    /**
     * The head talks \p animation once over whatever pose the body shows;
     * without one it moves no lips. A new line starts the talk over.
     */
    void startTalking(const std::shared_ptr<graphics::LipAnimation> &animation);
    /** The talking head blends out. */
    void stopTalking();

    bool isSelectable() const override;
    int effectState() const { return _effectState; }
    int effectAIStateMask() const { return _effectAIStateMask; }
    EffectId activeStateRootId() const { return _activeStateRootId; }
    /** A state effect was admitted: a state stronger than the one laid takes its place. */
    void onStateRootApplied(const EffectInstance &pending);
    /**
     * Lays the strongest of the creature's state effects, with \p pending applied or the
     * effect applied as \p removingOrder going; none restores movement.
     */
    void rebuildStateEffects(std::optional<std::reference_wrapper<const EffectInstance>> pending = std::nullopt, uint64_t removingOrder = 0);
    /** A holding state stops the creature, remembering how it moved. */
    void beginStateImmobilization();
    /**
     * A holding state ended: the creature moves again as it did, unless dead or held by
     * another.
     */
    void restoreMovementAfterState();
    /**
     * Lays \p state with its ambient animation state, through the record
     * \p id; the creature's heartbeat becomes due at once.
     */
    void setInternalStateEffect(int state, int ambientState, EffectId id);
    /** The record \p id that laid the state ended: the creature is free of it. */
    void clearInternalStateEffect(EffectId id);
    /** The record \p id that laid the state is gone; \p setsPose refreshes the pose. */
    void onInternalStateRemoved(EffectId id, bool setsPose);
    /**
     * Takes away the AI capabilities \p mask lacks, as an AI-state effect does
     * when it is applied. A creature that cannot move stops.
     */
    void restrictAIState(int mask);
    /**
     * Takes up the AI state the creature's AI-state effects leave, starting
     * from \p pendingMask and leaving out the effect applied as
     * \p removingOrder. A creature that cannot move stops.
     */
    void recomputeAIStateEffects(int pendingMask = kAIStateUnrestricted, uint64_t removingOrder = 0);
    struct EffectStackCounts {
        int positive {0};
        int negative {0};
    };
    /** The good and bad effect icons the creature shows. */
    EffectStackCounts effectStackCounts() const;
    /** Counts one more effect showing \p icon. */
    void addEffectIcon(int icon);
    /** Counts one effect showing \p icon fewer. */
    void removeEffectIcon(int icon);
    /** Each damage shield of the creature strikes back at \p attacker. */
    void resolveDamageShields(Creature &attacker);

    /**
     * Takes \p amount from the whole Force pool, temporary points included, out of the
     * ordinary points.
     */
    void damageForcePoints(int amount);
    /** Heals \p amount Force points against the whole pool, up to the maximum. */
    void healForcePoints(int amount);
    /** Adds to the ordinary Force pool without capping it at the maximum. */
    void addCurrentForcePoints(int amount);
    /** The Force point maximum; none for a droid or a creature without a Force-using class. */
    int maxForcePoints() const;
    void setBodyFuel(bool active) { _bodyFuel = active; }
    void setThrowParryBlocked(bool blocked) { _throwParryBlocked = blocked; }
    bool throwParryBlocked() const { return _throwParryBlocked; }
    /** Under Body Fuel the Force pool stays full. */
    void refreshBodyFuel();
    /**
     * The Force point cost of \p spell after alignment, Charisma, room and form; \p
     * partyModifiers counts the party terms.
     */
    int adjustedSpellForcePointCost(const Spell &spell, bool partyModifiers) const;
    /** What the Force point cost of \p spell is worked out from; \p partyModifiers counts the party terms. */
    ForcePointCostTerms spellForcePointCostTerms(const Spell &spell, bool partyModifiers) const;
    /** Whether the power menu offers \p spell as ready against \p target, or why not. */
    PowerMenuStatus powerMenuStatus(const Spell &spell, std::optional<std::reference_wrapper<const Object>> target = std::nullopt) const;
    /**
     * The entries of the creature's power menu against \p target, or of its
     * own menu without one: the powers offered, each with its status.
     */
    std::vector<ContextAction> powerMenuActions(std::optional<std::reference_wrapper<const Object>> target, bool hostileMenu) const;
    /** The entries of the creature's form menu: every form it knows, all ready. */
    std::vector<ContextAction> formMenuActions() const;
    /**
     * The entries of the creature's behaviour menu, all ready. A combat style
     * outside the party behaviours first becomes the aggressive one.
     */
    std::vector<ContextAction> behaviorMenuActions();
    /** The implant mode the implant-switching feat has selected (1-4). */
    int implantMode() const;
    /** Switch to implant mode \p mode, remembering the previous one, and run the switch script. */
    void switchImplantMode(int mode);
    /** Whether the creature can pay the cost of \p spell now. */
    bool canPaySpellForcePointCost(const Spell &spell) const;
    /**
     * A class of the creature knows the power, and its Force point cost is
     * within the creature's reach as a query of the power counts it.
     */
    bool hasAffordableSpell(const Spell &spell) const;
    /**
     * Pays the cost of \p spell, temporary points first, whatever the
     * creature has left: each pool stops at zero.
     */
    void paySpellForcePointCost(const Spell &spell);
    /**
     * The Force points a cast of \p spell is recorded as costing: its
     * adjusted cost, or under Force Body the share of it paid in vitality.
     */
    int recordedSpellForcePointCost(const Spell &spell) const;
    /** The equipment classes the creature wears, as the Force power equipment masks test them. */
    uint32_t forceItemMask() const;
    /** The level of the Force Body effect on the creature, -1 without one. */
    int forceBodyLevel() const;
    /**
     * The caster level of \p spell: the level of the first Force class that
     * knows it, less its negative levels, -1 without one; an item or cheat
     * cast uses the spell's innate level.
     */
    int spellCasterLevel(const Spell &spell, bool itemOrCheat = false) const;
    /**
     * The class position a power cast from the menu uses: the first of the
     * first two positions that holds a Jedi class, or none.
     */
    int spellCastingClass() const;
    /** The caster level of the spell-like ability \p spellId. */
    int spellLikeAbilityCasterLevel(int spellId) const;
    /** The level of the class at \p classIndex less its negative levels. */
    int adjustedClassLevel(int classIndex) const;
    /** Whether \p spell is a ready spell-like ability, giving its caster level. */
    bool readySpellLikeAbility(SpellType spell, int &casterLevel) const;
    /** Uses up the spell-like ability \p spell of \p casterLevel. */
    void consumeSpellLikeAbility(SpellType spell, int casterLevel);
    /** Grants temporary vitality; a restored grant does not add to a saved pool. */
    void addTemporaryHitPoints(int amount, bool restoring = false);
    /** Takes back temporary vitality, which can kill. */
    void removeTemporaryHitPoints(int amount);
    /** Grants temporary Force points; a restored grant does not add to a saved pool. */
    void addTemporaryForcePoints(int amount, bool restoring = false);
    /** Takes back the whole of a temporary Force point grant, spent or not. */
    void removeTemporaryForcePoints(int amount);
    int bonusForcePoints() const { return _bonusForcePoints; }
    void setBonusForcePoints(int amount);
    int currentHitPoints() const override {
        return _currentHitPoints + _temporaryHitPoints;
    }

    /** Scales the movement rate factor. */
    void multiplyMovementRate(float multiplier);
    /**
     * Takes up the movement rate the creature's speed effects give, the package \p removingId
     * left out.
     */
    void recomputeMovementRate(uint64_t removingId = 0);
    /** The movement rate factor; \p applyMobility counts TSL's Mobility. */
    float movementRate(bool applyMobility = false) const;
    bool isRunLimited() const;
    void setRunLimited(bool limited) { _runLimited = limited; }
    MovementType movementType() const { return _movementType; }

    bool canCastSpells() const { return canExecuteActions(); }
    bool canMove() const { return canExecuteActions() && (_effectAIStateMask & kAIStateMove) != 0; }
    bool canAttack() const {
        constexpr int kAttackBits = kAIStateAttack | kAIStateAct;
        return canExecuteActions() && (_effectAIStateMask & kAttackBits) == kAttackBits;
    }
    bool isMovementRestricted() const { return _movementRestricted || !canMove(); }
    bool movementLockedByAction() const { return _movementRestricted; }
    /** Whether the AI state lets the creature take \p action. */
    bool permitsAction(const Action &action) const;
    /**
     * A walk heading for \p target ends where the creature stands, as a click
     * on it, whether or not the creature can move: \p target is a trigger
     * that is not a mine, within use range, and the creature is neither dead
     * nor a party member at zero vitality.
     */
    bool clicksTriggerInReach(const Object &target) const;
    /**
     * Enough experience for the next level by the experience table, below the
     * title's maximum, neither dead nor downed.
     */
    bool isLevelUpPending() const;
    /** The level-up chime may still sound for the creature: from its creation, and again after each level-up. */
    bool isLevelUpChimeArmed() const { return _levelUpChimeArmed; }
    void setLevelUpChimeArmed(bool armed) { _levelUpChimeArmed = armed; }
    /**
     * The level the creature's experience reaches by the experience table, up
     * to the title's maximum, for a creature whose IsPC is exactly 1; any
     * other creature's level, as getSpellLevel(false) gives it.
     */
    int potentialLevel() const;

    glm::vec3 getSelectablePosition() const override;
    /**
     * The experience table's amount at the creature's level, up to and
     * including the title's maximum level; none past it. The maximum level's
     * row normally holds 0xFFFFFFFF, the amount no experience reaches.
     */
    std::optional<uint32_t> getNeededXP() const;
    /**
     * The experience the creature lacks for its next level, from the
     * experience table: KotOR up to the first row its experience falls short
     * of, counted from the first row; TSL from the levels it has taken,
     * scaled as an autobalanced creature's, a level past the table reading
     * the maximum level's row. The difference wraps as 32 bits, so it is
     * negative when the creature has more, as at the maximum level.
     */
    int experienceToNextLevel() const;

    Gender gender() const { return _gender; }
    const std::string &firstName() const { return _firstName.str(); }
    const std::string &lastName() const { return _lastName.str(); }
    ModelType modelType() const { return _modelType; }
    int appearance() const { return _appearance; }
    CreaturePresentation presentation() const;
    void setPresentation(const CreaturePresentation &presentation);
    uint16_t portraitId() const { return _portraitId; }
    std::shared_ptr<graphics::Texture> portrait() const { return _portrait; }
    float walkSpeed() const;
    /** The speed the player drives this creature at, and its appearance's limit. */
    float driveSpeed() const { return _driveSpeed; }
    float driveMaxSpeed() const { return _driveMaxSpeed; }
    void setDriveSpeed(float speed) { _driveSpeed = speed; }
    /**
     * Whether the creature last moved at a run under the player's direct
     * control. Running under an action or its AI does not count, and the
     * creature stays running when it stops for an action, a conversation or a
     * change of leader.
     */
    bool isRunning() const { return _running; }
    void setRunning(bool running) { _running = running; }
    float runSpeed() const;
    float personalSpace() const { return _personalSpace; }
    float creaturePersonalSpace() const { return _creaturePersonalSpace; }
    /** How far above its feet the creature's walking space reaches. */
    float collisionHeight() const { return _collisionHeight; }
    CreatureSize size() const { return _size; }
    /** Whether the appearance gives a size category; without one, size() is Invalid. */
    bool hasSizeCategory() const { return _hasSizeCategory; }
    CreatureAttributes &attributes() { return _attributes; }
    const CreatureAttributes &attributes() const { return _attributes; }
    ItemAttributes &itemAttributes() { return _itemAttributes; }
    const ItemAttributes &itemAttributes() const { return _itemAttributes; }
    Faction faction() const { return _faction; }
    int xp() const { return _xp; }
    /** Experience a roster companion brought with it beyond its share of the party pool. */
    int joiningXP() const { return _joiningXP; }
    void setJoiningXP(int value) { _joiningXP = value; }
    float challengeRating() const { return _challengeRating; }
    int getReputationToward(const Creature &target) const;
    /** How an object of a faction outside the party regards this creature. */
    int getReputationFrom(Faction sourceFaction) const;

    bool isEncounterCreature() const { return _encounterId != script::kObjectInvalid; }
    /** The creature was spawned by \p encounter. */
    void joinEncounter(const Encounter &encounter);
    /** The creature no longer counts against its encounter's wave. */
    void leaveEncounter();
    /**
     * The areas of effect the creature carries join the area it enters,
     * centred on it.
     */
    void placeCarriedAreasOfEffect(Area &area);
    bool hasLeftEncounter() const { return _leftEncounter; }

    /**
     * Keep turning toward an object while standing (kObjectInvalid releases
     * the lock). A dead or downed creature keeps its lock unless forced.
     * Releasing a lock turns the creature once toward what it was locked on.
     */
    void setOrientationLock(uint32_t objectId, bool force = false);
    /**
     * Give up the object the creature keeps turning toward, without turning
     * toward it. A dead or downed creature keeps its lock.
     */
    void dropOrientationLock();
    /**
     * The creature's last swing was in an engaged exchange. While it is, the
     * controlled leader at rest is not driven to its rest pose.
     */
    void setEngagedExchange(bool engaged) { _engagedExchange = engaged; }

    // Facing

    /**
     * Faces at once, the model with it: placement, loading, conversations and
     * player input.
     */
    void setFacing(float facing) override;
    /**
     * Turns the facing at once and lets the model follow: the facing wanted
     * and the movement facing become it, and the model takes one step.
     */
    void turnTo(float facing);
    void turnToward(const glm::vec3 &point);
    void turnToward(const Object &object);
    void turnAwayFrom(const Object &object);
    /**
     * A step taken turns the facing along it; walking or running, the model
     * first steps toward its previous way.
     */
    void turnAlongStep(const glm::vec2 &step);
    /** The facing the model turns toward standing still; the facing stays. */
    void setDesiredFacing(float facing);
    /** Wants to face a point, unless it stands on it. */
    void setDesiredFacingToward(const glm::vec3 &point);

    // END Facing

    void setIsInConversation(bool isInConversation) override;
    void onDestroyabilityChanged();
    /**
     * Kills the creature; false when it cannot die. \p operation is the death effect that
     * kills it, if any.
     */
    bool applyDeathEffect(const std::shared_ptr<Object> &damager, bool noFadeAway,
                          std::optional<std::reference_wrapper<const EffectInstance>> operation = std::nullopt);
    /**
     * Raises the creature with \p hpPercent of its vitality in TSL; false when it cannot be
     * raised.
     */
    bool applyResurrectionEffect(int hpPercent);
    /**
     * Heals \p amount up to the maximum and ends temporary wounding; \p quiet keeps TSL from
     * reporting it.
     */
    void applyHealingEffect(int amount, const std::shared_ptr<Object> &creator, bool quiet);
    /** Takes off the effects a death ends. */
    void removeEffectsOnDeath();

    /** An armour class effect was admitted. */
    void addArmorClassEffect(const EffectInstance &effect);
    /** An armour class effect is being removed. */
    void removeArmorClassEffect(const EffectInstance &effect);
    /** Moves the armour class cursor to the first armour class increase. */
    void updateArmorClassEffectCursor();
    void clearArmorClassEffectCache() {
        _armorClassCache = {};
        _armorClassCursor = {};
    }

    /**
     * The pool maximum an ability effect moves: vitality for Constitution,
     * Force points for Wisdom and Charisma. Read before the effect is
     * admitted or removed.
     */
    std::optional<int> abilityPoolMaximum(const EffectInstance &effect) const;

    /**
     * An ability effect was admitted or removed. The current pool moves with
     * its maximum; a Constitution change can kill.
     */
    void followAbilityPool(const EffectInstance &effect, int previousMaximum);

    Alignment alignment() const;
    RacialType racialType() const { return _race; }
    /** The creature's race: its names, ability adjustments and favoured class. */
    const Race &race() const;
    /** Set the race; a race without a row of the race table is invalid. */
    void setRacialType(RacialType race);
    Subrace subrace() const { return _subrace; }
    NPCAIStyle aiStyle() const { return _aiStyle; }
    /** The creature scripts name for this one to heal. It is not saved. */
    uint32_t healTarget() const { return _healTarget; }
    void setHealTarget(uint32_t target) { _healTarget = target; }
    /**
     * Whether the creature's record marks it a player character: its IsPC is
     * not 0. The record's value is saved with the creature.
     */
    bool isPC() const { return _isPC != 0; }
    /** The record's IsPC as stored, 0 to 255; a player character's is 1. */
    int isPCValue() const { return _isPC; }
    /**
     * Whether the creature acts as a player character: its own copy of the
     * record's IsPC, taken whenever the record is read, cleared when an
     * encounter spawns the creature, and set with the record's when control
     * of the party changes hands. The copy is not 0.
     */
    bool actsAsPC() const { return _actsAsPC != 0; }
    /** The creature's own copy of IsPC as it stands, 0 to 255. */
    int actsAsPCValue() const { return _actsAsPC; }
    /** Whether a click to talk leaves the creature facing the way it was. */
    bool isNotReorienting() const { return _notReorienting; }
    /** The character was made by the player (TSL item limitation). */
    bool isPlayerCreated() const { return _playerCreated; }
    bool isPartyInteract() const { return _partyInteract; }
    /** Runs the heartbeat script now and restarts the heartbeat interval. */
    void forceHeartbeat();
    /** Puts the party's clothing on a member whose body armour was refused on load. */
    void forceEquipClothing();
    void setPlayerCreated(bool value) { _playerCreated = value; }
    /**
     * Sets the player-character flag, both the record's IsPC and the
     * creature's own copy, to 1 or 0. A creature whose record does not carry
     * IsPC is a player character.
     */
    void setPC(bool value) {
        _isPC = value ? 1 : 0;
        _actsAsPC = _isPC;
    }
    /** The appearance's race label, which picks droid discharges. */
    const std::string &appearanceRace() const { return _appearanceRace; }
    /** The appearance's blood colour: R, G, Y or none. */
    const std::string &bloodColour() const { return _bloodColour; }
    /** The equipment slots the appearance locks, as a mask. */
    uint32_t equipSlotsLocked() const { return _equipSlotsLocked; }
    int goodEvil() const { return _goodEvil; }
    void setGoodEvil(int value) { _goodEvil = value; }
    /**
     * Move the good/evil value by a shift, kept within 0..100. In TSL a party
     * NPC's own alignment moves instead and its influence gives the result;
     * the player character's change, unless NPCs are left alone, is recorded
     * for scripts and moves every companion with a creature through its
     * influence. Pure Good or Evil powers follow.
     */
    void modifyAlignment(int shift, bool dontModifyNPCs);
    /**
     * TSL: a party NPC's good/evil follows from its own alignment, its
     * influence with the player character and the player character's
     * alignment, Charisma and mastery of a side of the Force.
     */
    void recomputeInfluenceAlignment();
    int assignedPuppet() const { return _assignedPuppet; }
    bool isPuppet() const { return _puppet; }
    CombatForm currentForm() const { return _currentForm; }
    CombatStance combatStance() const { return _combatStance; }
    void setCombatStance(CombatStance stance);
    /** Whether the creature is in the Total Defense stance or its round takes it. */
    bool isInTotalDefense() const;
    /**
     * The calendar day and time at which Total Defense last started the
     * creature's round, 0/0 when unset. Set, it also marks the stance as
     * already begun. It is not saved.
     */
    bool hasTotalDefenseStart() const { return _totalDefenseDay != 0 || _totalDefenseTime != 0; }
    void markTotalDefenseStart(uint32_t day, uint32_t time) {
        _totalDefenseDay = day;
        _totalDefenseTime = time;
    }
    void clearTotalDefenseStart() { markTotalDefenseStart(0, 0); }
    uint32_t totalDefenseDay() const { return _totalDefenseDay; }
    uint32_t totalDefenseTime() const { return _totalDefenseTime; }
    /**
     * Releases the orientation lock, returns to the ready pose and strips
     * special attacks from the round.
     */
    void cancelAllCombatModes();
    /** Record the kind of the round action most recently dispatched for this creature. */
    void setRoundActionKind(int kind) { _roundActionKind = kind; }
    int roundActionKind() const { return _roundActionKind; }
    /** Whether the current round has told this creature that its ranged weapon is ineffective. */
    bool weaponIneffectiveReported() const { return _combatState.weaponIneffectiveReported; }
    void setWeaponIneffectiveReported(bool reported) { _combatState.weaponIneffectiveReported = reported; }
    /** Queues the actions of \p stance; false for a creature that takes no command. */
    bool addStanceActions(CombatStance stance, const std::shared_ptr<Object> &target,
                          bool clearActions, bool toFront, bool fromRound);
    /**
     * Queues a weapon swap, after the round's work unless \p immediate; false for a creature
     * that takes no command.
     */
    bool addSwitchWeaponsAction(bool immediate);
    /**
     * A player's weapon-swap request: refused in restrict mode and within a
     * second of the last. The second is real time, so it also passes while the
     * game is paused or a menu is open.
     */
    bool requestSwitchWeapons(bool immediate);
    /** An item used in combat holds back the next item use for three seconds of game time. */
    bool isItemUseCoolingDown() const { return _itemUseCooldown > 0.0f; }
    void startItemUseCooldown();
    const AutoBalanceContext &autoBalanceContext() const {
        return _autoBalanceContext;
    }

    void setGender(Gender gender) { _gender = gender; }
    /** The first and last name; the name is the two joined by a space. */
    void setFirstAndLastName(resource::LocString firstName, resource::LocString lastName);
    void setAppearance(int appearance) { _appearance = appearance; }
    void setMovementType(MovementType type);
    /** Join a faction; a creature joining a faction other than its own goes to the end of its members. */
    void setFaction(Faction faction);
    /** Where the creature stands in the order in which its faction's members joined. */
    uint32_t factionJoinOrder() const { return _factionJoinOrder; }
    /** Set the serialized base maximum and preserve the existing damage. */
    void setMaxHitPoints(int baseHitPoints) override;
    int maxHitPoints() const override;
    /** The Constitution modifier the maximum vitality counts for every level. */
    int vitalityConstitutionModifier() const { return _vitalityConstitutionModifier; }
    void setCurrentHitPoints(int hitPoints) override;

    /**
     * Take up a changed base Constitution: current vitality moves with the
     * maximum.
     */
    void recalculatePermanentVitality();

    /**
     * Add to a base ability score. A Constitution change moves current
     * vitality with the maximum.
     */
    void adjustBaseAbilityScore(Ability ability, int amount);

    /** Initialize a newly generated creature at full derived vitality. */
    void initializeGeneratedVitality();

    /** Current vitality translated back to the serialized base-HP axis. */
    int serializedCurrentHitPoints() const;

    /** Current Force points translated back to the serialized base axis. */
    int serializedCurrentForce() const;
    void setMovementRestricted(bool restricted) { _movementRestricted = restricted; }
    void setImmortal(bool immortal) { _immortal = immortal; }
    void setAIStyle(NPCAIStyle style) { _aiStyle = style; }
    void setAssignedPuppet(int puppet) { _assignedPuppet = puppet; }
    void setPuppet(bool puppet) { _puppet = puppet; }
    void setCurrentForm(CombatForm form) { _currentForm = form; }
    bool isStealthed() const { return _stealthMode; }
    /** Enters or leaves stealth, with its presentation. */
    void setStealthMode(bool enabled);
    /**
     * The latest of the creature's own shells, the stealth exit's flash or
     * else the stealth field, with the effect application order current when
     * it began; null without one.
     */
    std::shared_ptr<graphics::Texture> latestOwnShell(uint64_t &order) const;
    /** Whether the creature may use its Stealth skill at all. */
    bool canUseStealthSkill() const;
    /** Whether the player can put this creature into stealth. */
    bool isStealthCapable() const;
    /** The stealth skill: leave stealth, or enter it out of combat and conversation. */
    void toggleStealth();
    /** Using the Stealth skill: a creature without it is told so, others toggle stealth. */
    void useStealthSkill();
    /** Stealth holds the creature to a sneaking walk, unless TSL's Stealth Run frees it. */
    bool movesStealthily() const;
    /**
     * Whether the creature has done its Treat Injury work and not yet healed.
     * A heal that finds nothing to treat leaves it set, so the creature's
     * next heal skips the work.
     */
    bool treatInjuryWorkDone() const { return _treatInjuryWorkDone; }
    void setTreatInjuryWorkDone(bool done) { _treatInjuryWorkDone = done; }
    /**
     * Break off the activities an engaging creature cannot keep: stealth ends
     * and the creature leaves any conversation it takes part in. A creature
     * holding a paused conversation stays in it; keepStealth preserves stealth.
     */
    void interruptActivities(bool keepStealth = false);
    /** A creature that paused its conversation keeps its place in it until it resumes. */
    void setConversationPaused(bool paused) { _conversationPaused = paused; }
    /**
     * A cast or item use takes the round; unless \p interrupts is false it breaks off stealth
     * and conversation as its kind does.
     */
    void beginSpellActivity(int spellId, bool itemCast, bool hostile, bool interrupts = true);
    /**
     * In TSL a creature under a mind trick shakes it off on noticing a hostile creature close
     * by.
     */
    void updateMindTrickPerception(const Creature &target, bool heard, bool seen);
    /**
     * Excite the creature for the duration of an excitedduration row, unless
     * it is already excited for longer. The creature the player controls
     * starts battle music when an enemy is near.
     */
    void setExcitedState(int row);

    // Animation

    /**
     * Turn the head toward an object no farther than maxDistance, for
     * creatures whose appearance tracks with the head; none ends the look.
     * Nothing changes while in combat state, while suspended, or for the
     * object already looked at. True when a new look begins.
     */
    bool lookAt(const std::shared_ptr<Object> &target, float maxDistance);
    /** The appearance's horizontal head-turn arc in degrees. */
    float headTurnHorizontal() const;
    /**
     * Height of the head above the creature's position: the posed head node of
     * the body model, or the appearance height when that node is missing, at
     * zero height or below the creature.
     */
    float headHeight() const;
    /** Where free-look sees from: the free-look hook, the camera hook, or 2 m above the creature. */
    glm::vec3 freeLookPoint() const;
    /** The video effect shown while looking through the creature's eyes in free-look, -1 for none. */
    int freeLookVideoEffect() const;
    /**
     * Height the camera is hooked at: the camera hook's posed height relative
     * to its parent node, or the head height without a hook.
     */
    float cameraHookHeight() const;
    /**
     * A creature that played a world-space cut clip cannot turn its head
     * until a later dialog animation, or the release of the list before the
     * last one at the conversation's close, lifts it.
     */
    void setHeadLookSuspended(bool suspended) { _headLookSuspended = suspended; }

    /** An animation ID as this creature plays it for a script. */
    struct ScriptAnimation {
        std::string clip; /**< empty without one */
        bool loop {false};
        float length {0.0f}; /**< seconds at natural speed, 0 when the model lacks the clip */
        AnimationSource source;
    };
    /**
     * The animation that animation ID \p id names for this creature: its row's
     * clip, else its cut clip, else the pause row, except 10098, which shows
     * nothing.
     */
    ScriptAnimation getScriptAnimation(int id) const;
    /** Plays a script animation: a loop at once, a one-shot through the queue. */
    void playScriptAnimation(const ScriptAnimation &animation, float speed);
    int currentAnimationId() const { return _animationSource.id; }
    bool isPlayingDialogAnimation() const { return _animationSource.dialog; }

    void playAnimation(AnimationType type, scene::AnimationProperties properties = scene::AnimationProperties()) override;
    void playAnimation(AnimationType type, scene::AnimationProperties properties, AnimationSource source);

    void playAnimation(const std::string &name, scene::AnimationProperties properties = scene::AnimationProperties(),
                       AnimationSource source = AnimationSource());
    bool playAnimation(const std::shared_ptr<graphics::Animation> &anim, scene::AnimationProperties properties = scene::AnimationProperties());
    /**
     * Plays a conversation clip: a loop until replaced, a one-shot until it ends in the pause.
     *
     * @param source the clip's animation ID, when the clip is known by one
     */
    bool playExternalAnimation(const std::shared_ptr<graphics::Animation> &anim,
                               scene::AnimationProperties properties = scene::AnimationProperties(),
                               AnimationSource source = AnimationSource {-1, true});
    void resumeStateDrivenAnimation();
    /**
     * Shows the pause or combat-ready loop. At the end of a round pause an
     * injured creature out of combat state is given the injured pause.
     */
    void showPauseReadyAnimation(bool pauseEnd);
    /** Shows the combat-ready loop, in combat state or not. */
    void showReadyAnimation();
    /** Chooses the idle loop again, as when its pose is sent anew. */
    void refreshPauseAnimation();
    /**
     * The flinch of damage other than a weapon hit: once until another
     * animation is chosen, queued as a layer that plays at its natural speed
     * once the creature stands still in a loop. Hands with no flinch hold the
     * pause clip as a pose instead.
     */
    void playDamageFlinch();
    /**
     * A cast or an item use stands in for the pose until a pose is shown
     * again. Its loop, if any, is the idle meanwhile; without one the pose
     * stays underneath. A running one-shot finishes first.
     */
    void holdCastAnimation(std::string loop = std::string());
    /** A module transition refills each Force shield's pool. */
    void refillForceShieldPools();
    /** Ends the hold of a cast or item use. */
    void releaseCastAnimation();
    /** The conjure and cast visuals the creature's spells show on it. */
    SpellCastVisuals &spellCastVisuals() { return _spellCastVisuals; }
    /** The visuals the creature's special attacks show on its weapons. */
    SpecialAttackVisuals &specialAttackVisuals() { return _specialAttackVisuals; }
    /**
     * Another animation is chosen for the creature: the flinch it was held to
     * and a cast's or an item use's hold end.
     */
    void markAnimationChosen();
    /**
     * A one-shot the creature is given: queued to last its length at the
     * speed given, unless it is the one showing.
     */
    void playFireForgetAnimation(const std::string &clip, AnimationSource source, float speed = 1.0f);
    /**
     * A dialog animation by its type's clip: a loop plays at once; a one-shot
     * makes the pause the loop first and is queued.
     *
     * @param looping the clip loops rather than playing once
     */
    void playDialogAnimation(AnimationType type, bool looping, AnimationSource source);
    /** A dialog animation by its clip, as playDialogAnimation(AnimationType, bool, AnimationSource) plays it. */
    void playDialogAnimation(const std::string &clip, bool looping, AnimationSource source);
    /** A weapon newly in hand is drawn; while play is paused the draw waits. */
    void playWeaponDraw();
    /** The flourish animation: a layer while the player drives, else queued. */
    void playWeaponFlourish();
    /**
     * Flourish the weapons in hand, as the player's flourish command
     * (\p playerCommand) or a script does.
     */
    void flourishWeapons(bool playerCommand);
    /** Below a fifth of its maximum vitality, unless its appearance disables the injured animations. */
    bool isInjured() const;
    /**
     * Stands in its plain pause or ready loop: not moving, not in a one-shot,
     * a script loop, a talk, sleep or death pose, and not given the injured
     * pause at a round pause end.
     */
    bool showsPauseReadyAnimation() const;
    /**
     * A one-shot is running: a clip started from the fire-and-forget queue
     * whose time has not run out, or a directly played clip still playing.
     */
    bool isPlayingOneShotAnimation() const;

    /**
     * Queues a one-shot lasting lengthMilliseconds at rate behind the ones
     * already queued. Each frame, while the
     * creature stands still and its current clip loops, the first queued clip
     * starts. A clip that waited is sped up to end when it would have ended
     * had it started at once, and runs for at least 50 ms. A clip the
     * animations table marks as an overlay, or any clip queued as layered,
     * plays over the loop at its natural speed instead; the time then only
     * decides when the next clip may start and how long a new loop waits, and
     * an overlay row is taken off when it runs out. When the time runs out the
     * creature returns to its stored loop. A different loop arriving empties
     * the queue; a walk or run leaves the queue waiting until it stands again.
     * A swing carries its attack, which the hit of the swing shows.
     */
    void addFireForgetAnimation(const std::string &clip, int lengthMilliseconds, float rate, bool layered,
                                AnimationSource source = AnimationSource(),
                                std::optional<SwingAttack> attack = std::nullopt);
    /**
     * Queues a clip of the creature's model at the speed given, lasting its
     * length at that speed; a clip the model lacks runs the shortest time and
     * shows nothing. False without a model.
     */
    bool addFireForgetAnimation(const std::string &clip, bool layered = false, AnimationSource source = AnimationSource(),
                                float speed = 1.0f);
    /**
     * Ends the running one-shot and returns to the stored loop; the queue is
     * kept. clearLayers also takes every layer off the body model.
     */
    void abortFireForgetAnimation(bool clearLayers = false);
    /**
     * Cuts the running queued one-shot short: it ends at the next animation
     * update, as if its time had run out.
     */
    void cutOneShot();
    /** Empties the fire-and-forget queue. The running one-shot goes on. */
    void flushFireForgetQueue();
    /** The pause becomes the loop, unless it shows already. */
    void showDialogPause();
    /**
     * The creature stands still and shows a loop: no one-shot runs and the
     * clip under any layers loops.
     */
    bool currentClipLoops() const;

    /**
     * Play an animation as a layer over whatever the creature is already doing,
     * including while it is walking or running. Unlike the other playAnimation
     * overloads this neither waits for the creature to stand still nor takes
     * over its state-driven animation, so locomotion carries on underneath and
     * the layer disappears on its own once it has run.
     */
    void playOverlayAnimation(AnimationType type);
    void playOverlayAnimation(const std::string &clip);
    /**
     * The variant of a melee attack animation, 1 to 5 in a cinematic exchange, never the last
     * one again, else 1 or 2.
     */
    int selectMeleeAttackVariant(bool cinematic);

    void updateModelAnimation();
    /**
     * Each frame a standing creature's loop follows its state: the controlled
     * leader acting freely and following party members are held to their
     * pose, everyone else folds between pause, ready and injured pause.
     */
    void updateIdleLoop(const std::string &active);
    /**
     * Once per frame: counts the running one-shot down and returns to the
     * stored loop when its time runs out, updates the model animation, then
     * starts the first queued clip if the current clip loops.
     */
    void animateFireAndForget(float dt);

    // END Animation

    // Equipment

    bool equip(const std::string &resRef);
    /** Validate an equipment commit without changing ownership. */
    bool canEquip(int slot, const std::shared_ptr<Item> &item) const;
    bool equip(int slot, const std::shared_ptr<Item> &item);
    /** Validate an equipment replacement without changing ownership. */
    bool canReplaceEquipment(
        int slot,
        const std::shared_ptr<Item> &item,
        const Object &displacedReceiver) const;
    bool replaceEquipment(
        int slot,
        const std::shared_ptr<Item> &item,
        Object &displacedReceiver);
    /**
     * Remove the equipment ownership edge for immediate transfer or retirement.
     * Without the presentation update the model keeps showing the item.
     */
    std::shared_ptr<Item> takeEquippedItem(const std::shared_ptr<Item> &item, bool updatePresentation = true);
    /** Unequip an Item directly into an explicit owning inventory. */
    bool moveEquippedItemTo(
        const std::shared_ptr<Item> &item,
        Object &receiver);

    std::shared_ptr<Item> getEquippedItem(int slot) const;
    CreatureWieldType getWieldType() const;
    /**
     * The weapon class of the hands, which picks the ready and combat clips:
     * empty hands are 8 and a left item alone is 0. With both hands filled
     * only the left item's wield counts; otherwise the right item's wield
     * does. A wield with no class gives 0.
     */
    int getReadyWeaponClass() const;
    /** Whether the weapons in hand show the blaster stance an assassination needs. */
    bool hasAssassinateWeaponPresentation() const;

    const std::map<int, std::shared_ptr<Item>> &equipment() const { return _equipment; }
    /**
     * Runs \p evaluate while the creature answers as though it wore
     * \p equipment, then restores its own. Nothing is equipped or unequipped:
     * effects, ownership and presentation stay as they are.
     */
    void evaluateWithEquipment(
        std::map<int, std::shared_ptr<Item>> equipment,
        const std::function<void()> &evaluate);
    std::vector<std::shared_ptr<Object>> ownedRuntimeObjects() const override;

    // END Equipment

    // Body bag

    int bodyBagRow() const { return _bodyBagId; }
    /** The appearance's body bag row; blank has none. */
    std::optional<int> appearanceBodyBagRow() const { return _appearanceBodyBag; }
    /** The appearance's delay in milliseconds before a destroyed body fades; blank keeps the corpse. */
    std::optional<int> fadeDelayOnDeath() const { return _fadeDelayOnDeath; }
    /** The items a body leaves in its bag: droppable equipment of the first sixteen slots, then droppable inventory. */
    std::vector<std::shared_ptr<Item>> dropableItems() const;
    /** The body shows no weapons in its hands; its equipment is unchanged. */
    void stripHandWeapons();
    /** The bag made when this creature was destroyed, which its kept corpse passes picks to. */
    uint32_t spawnedBodyBag() const { return _spawnedBodyBag; }
    void setSpawnedBodyBag(uint32_t id) { _spawnedBodyBag = id; }

    // END Body bag

    // Pathfinding
    /**
     * Walk toward \p dest until within \p distance of it. A creature already
     * within that distance of it, but without a clear line to it, walks all
     * the way onto it instead, until that walk ends.
     * @param pathTarget the object a planned path is recorded as heading for
     * @param straight a new path is planned along the straight line to the
     *        destination, rather than round what lies between
     */
    bool navigateTo(const glm::vec3 &dest, bool run, float distance, float dt,
                    std::optional<std::reference_wrapper<const Object>> pathTarget = std::nullopt,
                    bool straight = false);
    /** The last navigateTo found no way to its destination. */
    bool navigationFailed() const { return _navigationFailed; }

    /** Where the creature goes to use an object, and how near that point it must come. */
    struct UseRange {
        glm::vec3 point {0.0f};
        float range {0.0f};
    };

    /**
     * The point and range a walk toward a door or placeable heads for. Until
     * it is fixed, the walk takes the creature's current use point and range
     * once when either differs from what it holds (the point by more than a
     * millimetre), and from then on keeps them.
     */
    struct UseApproach {
        uint32_t target {script::kObjectInvalid};
        UseRange use;
        bool fixed {false};

        void follow(const UseRange &current);
    };

    /**
     * Where this creature uses \p target from. The distance starts at the
     * creature's personal space, at the target's position. A creature is used
     * from 1.4 metres in TSL, and in KotOR from the two creature personal
     * spaces and 0.3 metres. A trigger that is not an area transition is used
     * from its point nearest the creature, half a metre beyond the personal
     * space. A placeable is used from its nearer use point, three quarters of
     * a metre beyond the personal space, or from a tenth of a metre when it
     * wants precise use, that is not ignored, and the creature fits at the
     * point; five metres more for a corpse. A door is used from its nearest
     * action point, dropped onto the ground, three quarters of a metre beyond
     * the personal space, or from a tenth of a metre when it is locked and
     * wants precise use, that is not ignored, and the creature fits there. In
     * TSL, while a door saber is anywhere in the creature's queue, every door
     * is used from exactly a metre.
     */
    UseRange useRange(const Object &target, bool ignorePreciseUse = false) const;
    /**
     * Whether \p target is within use range, lengthened by \p extra and a
     * tenth of a metre, of this creature, both in the same area, with a clear
     * line a metre and a half above the ground from the creature to the use
     * point. An object in no area is never within use range.
     * An area transition trigger is within use range while the creature
     * stands inside it.
     */
    bool isInUseRange(const Object &target, float extra) const;
    /**
     * Run, or walk unless \p run, toward the use point of \p target until
     * within its use range lengthened by \p extra. True once \p target is
     * within use range, so lengthened. A walk to a door or placeable sets out
     * for the use point and range of that moment and keeps them until it
     * arrives, unless \p followsPoint lets it take them anew once.
     */
    /**
     * @param targetsPath whether a planned path is recorded as heading for the target
     */
    bool navigateToUse(const Object &target, float extra, float dt, bool run = true, bool followsPoint = false, bool targetsPath = true);
    void clearPath();
    void advanceOnPath(const glm::vec3 &dest, const glm::vec3 &dir, bool run, float distance, float dt);
    glm::vec3 computeSteeringForce(const Uniwalk &uni, const glm::vec3 &next, float dt);
    // END Pathfinding

    // Blocking doors

    /**
     * The door the last blocked event was raised for. Each blocked event
     * replaces it and nothing else clears it, so later runs report it too.
     */
    uint32_t lastBlockingDoorId() const { return _lastBlockingDoorId; }

    // END Blocking doors
    // Perception

    void onObjectSeen(const std::shared_ptr<Object> &object);
    void onObjectVanished(const std::shared_ptr<Object> &object);
    void onObjectHeard(const std::shared_ptr<Object> &object);
    void onObjectInaudible(const std::shared_ptr<Object> &object);

    void setObjectSeen(const std::shared_ptr<Object> &object, bool seen);
    void setObjectHeard(const std::shared_ptr<Object> &object, bool heard);
    /** Records whether \p object is invisible to the creature. */
    void setObjectInvisible(const std::shared_ptr<Object> &object, bool invisible);
    /** Forgets having seen, heard or noticed the invisible \p id. */
    void forgetPerceived(uint32_t id);
    /** Record a perception change as the last one and run the notice script. */
    void runOnNotice(const std::shared_ptr<Object> &object, PerceptionEvent event);
    void refreshVisibilityPerception();
    /**
     * Forget everything perceived, without notices, and look around at once;
     * a dead creature other than a player character does not look.
     */
    void perceiveAfresh();
    /** Take over another creature's perception, as a new party leader does. */
    void takePerception(Creature &other);
    /** Restore saved perception bits once saved references are bound. */
    void restoreSavedPerception(const std::vector<uint8_t> &data);

    /** The object of the last perception change. */
    uint32_t lastPerceivedId() const;
    bool lastPerception(PerceptionEvent event) const { return _lastPerception == event; }

    /** Spot and listen ranges; creatures other than the controlled one widen them in combat. */
    float spotRange() const;
    float listenRange() const;
    /** Take the perception ranges of a ranges row when the creature next looks around. */
    void setPerceptionRangeRow(int row);
    /** Takes up the pending ranges row, if any. */
    void resolvePerceptionRanges();

    /** Refresh the detection rolls when 20 s of play have passed since the last refresh. */
    void refreshPerceptionRolls();
    /** Whether this creature spots a creature now, reporting a spotted hider. */
    bool detectsBySight(const Creature &target, bool invisible) const;
    /** Whether this creature hears a creature now, through what lies between them. */
    bool detectsBySound(const Creature &target, bool invisible) const;
    /** Whether a creature this one attacks is one it has detected. */
    bool hasDetectedTarget(const Object &target) const;
    /** Blinded, or in darkness without Ultravision. */
    bool isBlind() const;

    /**
     * Update priority 0-4. Only priority 0 changes behaviour: it slows the
     * creature's full perception pass.
     */
    int aiLevel() const { return _aiLevel; }
    /** Script priority: remembered for resetting, out-of-range values give 3. */
    void setScriptAILevel(int level);
    /** Returns to the priority the script changed. */
    void resetScriptAILevel();
    /** Whether a priority-0 creature is still skipping its full perception pass. */
    bool throttlesPerceptionPass();

    float perceptionPassTime() const { return _perceptionPassTime; }
    void setPerceptionPassTime(float time) { _perceptionPassTime = time; }
    bool perceptionStarted() const { return _perceptionStarted; }
    void setPerceptionStarted() { _perceptionStarted = true; }

    static constexpr int kSeeInvisibleCounter = 0x01;
    static constexpr int kUltravisionCounter = 0x02;
    static constexpr int kTrueSeeingCounter = 0x04;
    /** The blindness of darkness, which Ultravision and True Seeing see through. */
    static constexpr int kDarknessCounter = 0x08;
    /** Blindness, which nothing sees through. */
    static constexpr int kBlindnessCounter = 0x10;
    /** The sight that sees through darkness. */
    static constexpr int kDarkSightCounters = kUltravisionCounter | kTrueSeeingCounter;

    int visibilityCounterBits() const { return _visibilityCounterBits; }
    void setVisibilityCounter(int bit);
    /** Takes every visibility counter off, as the creature's effects are dropped for those of a save. */
    void clearVisibilityCounters() { _visibilityCounterBits = 0; }
    /**
     * Takes the blindness counters of \p mask off, then sets those the
     * remaining blindness effects give, the one applied as
     * \p removedApplication left out.
     */
    void restoreBlindnessCounter(int mask, uint64_t removedApplication);
    /**
     * Takes \p bit off once the effect applied as \p removedApplication goes,
     * and sets it again while another effect of \p type remains. With
     * \p trueSeeingGivesUltravision, a remaining True Seeing gives
     * Ultravision instead.
     */
    void restoreVisibilityCounter(
        EffectType type,
        int bit,
        uint64_t removedApplication,
        bool trueSeeingGivesUltravision = false);
    /**
     * Whether one of \p bits is set. A counter that only equipped effects
     * whose item is gone give is off.
     */
    bool hasVisibilityCounter(int bits) const;

    const Perception &perception() const { return _perception; }

    // END Perception

    // Combat

    void setCombatState(bool active, CombatActivation activationType = CombatActivation::Direct, bool holdExpiry = false);
    /**
     * Powers the hand items on or off, with the power-up or power-down clip
     * and sound when \p animate; nothing changes while a script holds them.
     */
    void setLightsabersPowered(bool powered, bool animate);
    /** A script powers both hand items on or off, and in KotOR holds them so. */
    void overrideLightsabers(int override, bool powered, bool animate);
    /** The item the equipment screen takes out of a slot goes out at once. */
    void putOutItemLeavingSlot(int slot);
    /** A creature entering another module brings no hand power with it. */
    void resetHandPower();
    CombatActivation combatActivationType() const { return _combatState.activationType; }
    bool clientCombatMode() const { return _clientCombatMode; }
    /** Enters or leaves client combat mode, which the game takes up at once. */
    void setClientCombatMode(bool active);
    /** Draws the creature, and its faction within sight, into combat with \p opponent. */
    void broadcastCombatState(uint32_t opponent);
    /** Ends the ordinary invisibility and sanctuary effects an attack breaks. */
    void removeCombatInvisibilityEffects();
    /**
     * In TSL ends the mind trick and droid scramble states, walking the
     * effect list by position: a record a removal moves into the position it
     * freed is passed over.
     */
    void removeMindTrickEffects();
    uint32_t lastWeaponUsed() const { return _lastWeaponUsed; }

    bool isInCombat() const { return _combatState.active; }
    /**
     * Held helpless: by a state (in TSL any but the confusions), by a push, or
     * as a party member at zero vitality. TSL also counts meditation when
     * \p meditating asks for it.
     */
    bool isDebilitated(bool meditating = false) const;
    bool isTemporarilyDead() const;
    /**
     * Whether the creature is dead follows its vitality anew, as it must
     * once it has joined or left the party: a member never is, anyone else
     * is at the death threshold.
     */
    void refreshDeath();
    /**
     * The creature lies in a dead pose: from its death, a downed party
     * member's included, until it is raised.
     */
    bool liesDead() const { return _deathPose.has_value(); }
    /** The first force shield effect on the creature names a shield. */
    bool hasForceShield() const;
    /**
     * Shows no reaction to an attack: unable to act, a party member at zero
     * vitality, or meditating.
     */
    bool isUnableToReact() const;
    EffectId activePoisonEffectId() const { return _activePoisonEffectId; }
    void setActivePoisonEffectId(EffectId id) { _activePoisonEffectId = id; }
    void clearActivePoisonEffectId(EffectId id) {
        if (_activePoisonEffectId == id) {
            _activePoisonEffectId = kUnassignedEffectId;
        }
    }
    /** Whether the creature is in the party. */
    bool isPartyMember() const;
    bool isInvisibleTo(const Creature &observer) const;
    /** Drops the queued action groups whose attack, cast or item cast is aimed at \p object. */
    void clearHostileActionsAgainst(const Object &object);
    /**
     * Whether an immunity of the creature refuses \p effect or any effect it
     * links. The first immunity found is reported to the effect's creator
     * and to the creature.
     */
    bool isEffectLinkImmune(const Effect &effect) const;
    bool isTwoWeaponFighting() const;
    std::shared_ptr<Item> getOffhandAttackWeapon() const;
    /** The weapon of the off hand: a double-bladed right-hand weapon, else the left-hand weapon. */
    std::shared_ptr<Item> getEquippedOffhandWeapon() const;

    /** The creature lies under a knockdown effect. */
    bool isKnockedDown() const { return hasEffect(EffectType::Knockdown); }
    /** Show the fall of a knockdown under the given animation ID. */
    void showKnockdownFall(int animationId);

    int forcePoints() const { return _forcePoints; }
    int currentForce() const { return _currentForce + _temporaryForcePoints; }
    int currentForceWithoutTemporary() const { return _currentForce; }
    /** Regenerates \p amount Force points against the whole pool, up to the maximum. */
    void regenerateForcePoints(int amount);
    /**
     * Takes a level of \p clazz with \p attributes, filling vitality and adding the Force
     * points it grants.
     */
    void applyLevelUp(CreatureAttributes attributes, CreatureClass &clazz);
    /**
     * Gains a class at level 0, after any number already held; a class already
     * held is unchanged. Its Pure Good or Evil powers are applied afresh.
     */
    void addClass(CreatureClass &clazz);

    /** The object the last planned path heads for, until the action queue empties or is cleared. */
    uint32_t attemptedMovementTarget() const {
        auto target = _attemptedMovementTarget.resolve();
        return target ? target->id() : script::kObjectInvalid;
    }
    void resetAttemptedMovementTarget() { _attemptedMovementTarget.reset(); }

    /**
     * The creature last walked into and looked for a way round, until the
     * next path is planned, the action queue empties or is cleared, or no way
     * round it was found.
     */
    uint32_t blockingCreature() const {
        auto blocker = _avoidance.blocker.resolve();
        return blocker ? blocker->id() : script::kObjectInvalid;
    }
    void resetBlockingCreature() {
        _avoidance.blocker.reset();
        _avoidance.blockerPosition = glm::vec3(0.0f);
    }
    /** The last hostile creature found blocking a walk or standing on the way round. */
    uint32_t foundEnemyCreature() const {
        auto enemy = _avoidance.foundEnemy.resolve();
        return enemy ? enemy->id() : script::kObjectInvalid;
    }
    /**
     * Push a creature in the way of a step aside, when it can be pushed. While
     * a spot is chosen, this creature stands on the line through a and b
     * where it passes nearest the other. The other drops what it was doing
     * and runs to a spot just off that line on its own side, unless it is
     * already set on a move. Failing a spot it can walk straight to, it is
     * placed at once behind this creature, against moverDir from b. True
     * when the other has been dealt with.
     */
    bool pushAside(Creature &other, glm::vec3 a, glm::vec3 b, const glm::vec2 &moverDir);

    uint32_t getAttemptedAttackTarget() const {
        auto target = _combatState.attemptedAttackTarget.resolve();
        return target ? target->id() : script::kObjectInvalid;
    }
    /**
     * The target the player ordered the creature to attack while it was
     * attempting another, until its next round is taken on that target.
     */
    uint32_t getOrderedAttackTarget() const {
        auto target = _combatState.orderedAttackTarget.resolve();
        return target ? target->id() : script::kObjectInvalid;
    }
    void setOrderedAttackTarget(uint32_t target);
    std::shared_ptr<Object> getAttackTarget() const {
        return _combatState.attackTarget.resolve();
    }
    std::shared_ptr<Object> getRoundTarget() const {
        return _combatState.roundTarget.resolve();
    }
    void setRoundTarget(const std::shared_ptr<Object> &target) { _combatState.roundTarget = target; }
    void clearRoundTarget() { _combatState.roundTarget.reset(); }
    uint32_t getLastHostileTarget() const {
        auto target = _lastHostileTarget.resolve();
        return target ? target->id() : script::kObjectInvalid;
    }
    /** The action of the last combat round, or -1 before any and after leaving combat. */
    int getLastAttackAction() const { return _lastAttackAction; }
    /** The power the last spell round ended with: 0 after an item use, -1 when none since leaving combat. */
    int lastForcePowerUsed() const { return _lastForcePowerUsed; }
    FeatType getLastCombatFeat() const { return _lastCombatFeat; }
    AttackResultType getLastAttackResult() const { return _lastAttackResult; }
    int modifiedAttacks() const { return _modifiedAttacks; }
    bool hasAssuredHit() const { return _assuredHit; }
    /** Adds TSL's party leadership terms of an attack on \p target. */
    void addPartyLeadershipTerms(AttackBonusBreakdown &result, std::optional<std::reference_wrapper<const Creature>> target) const;
    /**
     * The attack bonus of an attack with \p weapon on \p target.
     *
     * @param handType the kind of attack whose hand-specific modifiers count;
     *        by default that of an attack with \p weapon in the given hand
     */
    AttackBonusBreakdown getAttackBonusBreakdown(
        std::optional<std::reference_wrapper<const Creature>> target,
        const std::shared_ptr<Item> &weapon,
        bool offHand,
        std::optional<AttackBonus> handType = std::nullopt) const;
    int getAttackBonus(bool offHand = false) const;
    /** The touch-attack bonus: none when the right hand holds the other kind of weapon. */
    int getTouchAttackBonus(bool ranged) const;
    /**
     * The untargeted attack bonus of the equipped hand for a melee or ranged
     * attack. A melee attack uses the left-hand weapon for the off hand unless
     * \p doubleBladed, and has none while that weapon is ranged; a ranged attack
     * has none while the right hand holds a melee weapon. \p meleeTwoWeapon
     * keeps the two-weapon terms of a melee attack. The hand-specific
     * modifiers counted are the queried hand's: the off hand's for a melee
     * off hand, else the on hand's, except that TSL counts the off hand's for
     * any off hand and the unarmed ones without a right-hand weapon.
     */
    AttackBonusBreakdown getHandAttackBonusBreakdown(
        bool ranged,
        bool offHand,
        bool doubleBladed,
        bool meleeTwoWeapon) const;
    bool hasEffectImmunity(
        ImmunityType immunityType,
        std::optional<std::reference_wrapper<const Creature>> creator = std::nullopt) const;
    int getAbilityEffectModifier(Ability ability) const;
    int getEffectiveAbilityScore(Ability ability) const;
    int getEffectiveAbilityModifier(Ability ability) const;
    /** The race's adjustment to \p ability. */
    int racialAbilityAdjustment(Ability ability) const;
    bool hasEffectiveFeat(FeatType feat) const;
    /** The feats the creature's live bonus feat effects give it. */
    std::set<FeatType> effectFeats() const;
    /** The d6 a sneak attack adds: the rank of the highest Sneak Attack feat held, 0 without one. */
    int sneakAttackDice() const;
    /**
     * The Defense against \p attacker and damage of \p damageFlags, or the general Defense
     * without an attacker; \p touch for a touch attack.
     */
    DefenseBreakdown getDefenseBreakdown(std::optional<std::reference_wrapper<const Creature>> attacker, int damageFlags, bool touch = false) const;
    /**
     * The Dexterity modifier the defense counts: the effective modifier,
     * limited by worn body armour, and at most 0 while debilitated.
     */
    int defenseDexterityModifier() const;
    int getDefense(std::optional<std::reference_wrapper<const Creature>> attacker, int damageFlags) const;
    int getDefense() const;
    /** Ordinary ranged defense consumes the same permission as active saber throws. */
    bool canParryRangedWeapon(const Creature &shooter, int damageFlags, bool &canReturn) const;
    /**
     * Defends against a shot of \p shooter totalling \p attackTotal: Invalid when the defense
     * fails, else a parry or deflection. A deflection roll writes its parts to
     * \p record, the round's record, whether or not it succeeds.
     */
    AttackResultType resolveRangedDefense(const Creature &shooter, int damageFlags, int attackTotal,
                                          DeflectionBreakdown &record) const;
    /**
     * Let \p target deflect this creature's touch attack. Only a ranged round
     * record allows it, opposed by that record's last roll and modifier; a
     * deflection overwrites the record's result.
     */
    AttackResultType resolveTouchDeflection(const Creature &target);
    /** \p weapon is the shooter's weapon in the discharging hand, if any. */
    AttackResultType resolveRangedMiss(const Creature &shooter, const std::shared_ptr<Item> &weapon) const;
    /** The saving throw \p save of \p type against \p versus, if any. */
    SavingThrowBreakdown getSavingThrowBreakdown(
        SavingThrow save, SavingThrowType type = SavingThrowType::All,
        std::optional<std::reference_wrapper<const Object>> versus = std::nullopt) const;
    /** The saving throw \p save as the character sheet shows it. */
    int getSavingThrow(SavingThrow save) const;
    /** Add \p delta to the creature's own bonus to \p save. */
    void modifySavingThrowBonus(SavingThrow save, int delta);
    /**
     * The outcome of a saving throw totalling \p total against \p difficultyClass; an immunity
     * answers it first.
     */
    SavingThrowResult getSavingThrowResult(
        int total, int difficultyClass, SavingThrowType type,
        std::optional<std::reference_wrapper<const Object>> versus = std::nullopt) const;
    PhysicalDamageBonus getPhysicalDamageBonus(
        const std::shared_ptr<Item> &weapon,
        bool offHand) const;
    /** The factor TSL's autobalance applies to the creature's physical damage, 1 without it. */
    int getPhysicalDamageAutoBalanceFactor() const;
    /**
     * The caster level spells use: the class levels, less negative levels,
     * balanced in TSL for a creature neither flagged as a player character
     * nor in the party. The level is a byte.
     */
    int getSpellLevel(bool applyNegativeLevels = true) const;
    ForceResistanceState &forceResistance() { return _forceResistance; }
    const ForceResistanceState &forceResistance() const { return _forceResistance; }
    int getMassiveCriticalDamage(const std::shared_ptr<Item> &weapon, bool criticalHit) const;
    /** Sets the damage resistance the creature's feats give against \p damage. */
    void getDamageResistanceFeatBonuses(
        int damage,
        DamageResolution &resolution) const;
    /**
     * The damage power of an attack: the low byte of its effect attack bonus.
     *
     * @param handType the kind of attack whose hand-specific modifiers count;
     *        by default that of an attack with \p weapon in the given hand
     */
    DamagePower calculateDamagePower(
        std::optional<std::reference_wrapper<const Creature>> target,
        const std::shared_ptr<Item> &weapon,
        bool offHand,
        std::optional<AttackBonus> handType = std::nullopt) const;
    /**
     * Adds the item and effect damage modifiers of one hit and returns the
     * miscellaneous amount added to its base damage, which KotOR never adds.
     * The hand-specific modifiers are added by damage type, uncapped, their
     * dice multiplied by \p criticalMultiplier: on a critical hit the on-hand
     * weapon's multiplier, whichever hand attacks, else 1.
     * \p dice chooses whether the modifier dice are rolled or taken at a
     * fixed face; the lowest face applies to hand-specific dice only.
     * \p handType is the kind of attack whose hand-specific modifiers count;
     * by default that of an attack with \p weapon in the given hand.
     */
    int addPhysicalDamageModifiers(
        DamagePacket &damage,
        DamageBreakdown &breakdown,
        std::optional<std::reference_wrapper<const Creature>> target,
        const std::shared_ptr<Item> &weapon,
        bool offHand,
        int criticalMultiplier,
        DamageDiceRoll dice = DamageDiceRoll::Rolled,
        std::optional<AttackBonus> handType = std::nullopt) const;
    /**
     * Whether the weapon in the given hand would damage \p versus: its highest
     * damage after the target's immunity, resistance and reduction, plus its
     * highest elemental damage bonus, is positive. No pool is consumed.
     */
    bool isWeaponEffective(std::optional<std::reference_wrapper<Object>> versus, bool offHand) const;
    /**
     * The highest damage one hit of the given hand's weapon deals to
     * \p versus: its highest dice, Strength and feat bonuses and the
     * miscellaneous modifiers, at least one point, then the target's immunity,
     * resistance and reduction without consuming a pool. Without a target
     * nothing is mitigated.
     */
    int getMaximumWeaponDamage(std::optional<std::reference_wrapper<Object>> versus, bool offHand) const;
    /**
     * The highest elemental damage bonus of one hit against \p versus, after
     * its immunity and resistance.
     */
    int getMaximumElementalDamageBonus(Object &versus) const;
    /**
     * The weapon's size relative to the creature's, from two smaller to one
     * larger; none outside that span or when either size is invalid.
     */
    std::optional<int> getRelativeWeaponSize(const Item &weapon) const;
    /**
     * The damage range of one hit with \p weapon (none: unarmed) in the given
     * hand, as shown to the player: the base dice, the Strength term, weapon
     * specialization and the damage feats, then, with \p includeModifiers, the
     * untargeted item and effect damage modifiers of the equipped hand. Each
     * bound is at least one.
     */
    void getDamageRange(const std::shared_ptr<Item> &weapon, bool offHand, bool includeModifiers, int &minimum, int &maximum) const;

    /** The object the creature last tried to cast at. */
    void setAttemptedSpellTarget(uint32_t id);
    std::shared_ptr<Object> attemptedSpellTarget() const { return _combatState.attemptedSpellTarget.resolve(); }
    /** How far a cleave reaches from the creature to \p target. */
    float maxCleaveRange(std::optional<std::reference_wrapper<const Creature>> target) const;
    std::string getWeaponModelName(int slot) const;
    void setAttemptedAttackTarget(uint32_t target);
    void beginCombatAttack(Object &target, FeatType feat);
    /** Attack records are stored in action-owned buffers, not in the creature. */
    void setCurrentCombatAction(const std::shared_ptr<Action> &action) { _currentCombatAction = action; }
    std::shared_ptr<Action> currentCombatAction() const { return _currentCombatAction.lock(); }
    /** The attack type of the round under way, 0 without one. */
    int currentCombatAttackType() const;
    /** The result of the round's current attack. */
    AttackResultType currentCombatAttackResult() const;
    void finishCombatRound();
    /**
     * Leaves combat: actions, round and targets end; \p runEndRound tells whether the
     * end-of-round script runs.
     */
    void cancelCombat(int runEndRound = 0);
    /**
     * Gives up the fight: the creature and every creature within 250 m it
     * regards as hostile leave combat and lose their effects (with
     * retainOwnEffects, the effects each cast on itself stay); the creature
     * then joins the neutral faction.
     */
    void surrenderToEnemies(bool retainOwnEffects);
    /**
     * Every creature of the area that targets this one, or was last attacked
     * by it, leaves combat and drops its hostile actions against it.
     */
    void pacify();
    /**
     * An attack on \p target was queued: a targeted player character or
     * leader learns who comes for it, and combat spreads.
     */
    void recordQueuedAttack(Object &target);
    uint32_t getGoingToBeAttackedBy() const { return _incomingAttacker.id; }
    /** The first creature of the area attacking this one, starting the attacker walk. */
    uint32_t getFirstAttacker();
    uint32_t getNextAttacker() { return _attackerList.next(); }
    int getLastAttackType() const { return _receivedAttack.scriptType(isInCombat()); }
    int getLastAttackMode() const { return _receivedAttack.scriptMode(isInCombat()); }
    /** An attack by \p attackerId arrived, with its round's record and fields when known. */
    void receiveAttackEvent(std::optional<std::reference_wrapper<const AttackHistory>> history, uint32_t attackerId,
                            std::optional<std::reference_wrapper<const AttackEventFields>> fields = std::nullopt);
    /** A spell was cast at the creature: its script runs and a harmful one engages both sides. */
    void receiveSpellCastAt(uint32_t casterId, int spell, int harmful);
    /** Record the weapon of a delivered attack as this attacker's last weapon. */
    void recordAttackWeapon(int weaponAttackType);
    /**
     * A party member enters client combat mode from its own live attempted
     * target and leaves it once nothing keeps it there. The stored target is
     * the member's party-slot target; the leader also holds while a dead target
     * is held. Returns true when the member left combat mode.
     */
    bool updateClientCombatMode(std::optional<std::reference_wrapper<const Object>> storedTarget, bool holdDeadTarget);
    void clearCurrentAttackTarget() { _combatState.attackTarget.reset(); }
    /**
     * An attacker that cannot see its target remembers where it stood. True
     * when it had already failed to see from this very spot.
     */
    bool noteBlockedAttackSight() {
        const bool again = _blockedAttackSight == _position;
        _blockedAttackSight = _position;
        return again;
    }
    void clearBlockedAttackSight() { _blockedAttackSight.reset(); }
    void setLastAttackResult(AttackResultType result) { _lastAttackResult = result; }
    void adjustModifiedAttacks(int amount);
    bool applyAssuredHit();
    void removeAssuredHit() { _assuredHit = false; }
    /** The next shot is deflected, sent back when \p returnDamage; false while one is assured. */
    bool applyAssuredDeflection(int returnDamage);
    void removeAssuredDeflection() { _assuredDeflection = _assuredReturn = false; }

    // END Combat

    // Gold

    void giveGold(int amount);
    void takeGold(int amount);

    int gold() const { return _gold; }

    // END Gold

    /** Script-driven talent queries. Selection does not spend uses or resources. */
    bool hasTalent(TalentType type, int id) const;
    /** Whether the class at \p classIndex can still pay \p spell. */
    bool hasSpellUsesLeft(const Spell &spell, size_t classIndex) const;
    /**
     * Whether the creature can learn \p power in its class \p owner: the power
     * has a row in the spell table, and a player character learns powers only
     * in a Jedi class.
     */
    bool canLearnPower(SpellType power, ClassType owner) const;
    /**
     * A random talent the AI may use, of \p category, ranked by challenge rating unless \p
     * crMax is -1, and not the excluded one; an invalid talent when there is none.
     */
    std::shared_ptr<Talent> selectTalent(int category, int crMax, int inclusion,
                                         int excludeType, int excludeId) const;
    /** The skill rank against no one; \p baseOnly gives the trained rank alone. */
    int getUnopposedSkillRank(SkillType skill, bool baseOnly = false) const;
    /** Skill rank against a creature: skill effects limited to its race or alignment count. */
    int getSkillRankVersus(SkillType skill, const Creature &versus) const;
    /**
     * The farthest this creature attacks \p target from. A ranged weapon
     * reaches its range, 30 metres for none, lengthened by Awareness in TSL,
     * or 20 metres when looking for another enemy to attack. In melee it is
     * half a metre beyond the desired distance; in a choreographed exchange in
     * TSL, 1.5 metres.
     */
    float maxAttackRange(const Object &target, bool choreographed = false, bool enemySearch = false) const;
    /**
     * The distance this creature closes to in melee with \p target: the two
     * hit radii and 1.6 metres between them for a creature; in a
     * choreographed exchange 1.4 metres in TSL, and in KotOR the two creature
     * personal spaces and 0.7 metres; twice the creature's personal space for a door,
     * which an attack closes on as the target of its own path; the use range
     * for a placeable; 1.5 metres for anything else.
     */
    float desiredAttackRange(const Object &target, bool choreographed = false) const;
    /** Whether the creature's appearance cannot run at all. */
    bool isImmobile() const;
    /** The uses of \p feat left today: 0 without the feat, 100 for unlimited. */
    int featRemainingUses(FeatType feat) const;
    /** Spends a use of \p feat, when it has uses per day. */
    void spendFeatUse(FeatType feat);
    /**
     * Uses \p feat: nothing for a feat the creature lacks, otherwise the
     * highest rank of its chain the creature holds. A guard stance rank turns
     * the guard stance on, or off when one is held.
     *
     * @return the rank used when it is an attack feat; Invalid means no attack
     */
    FeatType useFeat(FeatType feat);
    /** Whether the creature holds a guard stance, in which its attacks are made without their feat. */
    bool inGuardStance() const { return _guardStanceFeat != FeatType::Invalid; }
    /** The item and property through which the creature can use the power \p spellId, if any. */
    std::optional<std::pair<std::shared_ptr<Item>, size_t>> itemForPower(int spellId) const;
    /**
     * The object whose inventory holds the creature's items: the player
     * character for a party member when \p useParty, and while the inventory
     * menu shows a character; the creature itself otherwise.
     */
    const Object &itemRepositoryOwner(bool useParty = true) const;

    // Scripts

    /**
     * Runs the creation script of a creature that has not run it yet, with its
     * first heartbeat. It is due on the creature's first update, or earlier
     * when an event reaches the creature first.
     */
    void runSpawnScript();
    void runBlockedScript(uint32_t blockingDoorId);
    void runEndRoundScript();
    /** In TSL restarts the three seconds to the next end-of-round decision. */
    void refreshCombatDecisionTimer();
    void runAttackedScript(uint32_t attackerId);

    bool spawnScriptFired() const { return _spawnScriptFired; }

    void setOnHeartbeat(std::string onHeartbeat) { _onHeartbeat = onHeartbeat; }
    void setOnSpawn(std::string onSpawn) { _onSpawn = onSpawn; }
    void setOnDeath(std::string onDeath) { _onDeath = onDeath; }
    void setOnNotice(std::string onNotice) { _onNotice = onNotice; }
    void setOnEndRound(std::string onEndRound) { _onEndRound = onEndRound; }
    std::string getOnSpellCastAt() const override { return _onSpellAt; }
    void setOnSpellAt(std::string onSpellAt) { _onSpellAt = onSpellAt; }
    void setOnAttacked(std::string onAttacked) { _onAttacked = onAttacked; }
    void setOnDamaged(std::string onDamaged) { _onDamaged = onDamaged; }
    void setOnDisturbed(std::string onDisturbed) { _onDisturbed = onDisturbed; }
    void setOnEndDialogue(std::string onEndDialogue) { _onEndDialogue = onEndDialogue; }
    /** Runs the end-of-conversation script. */
    void runEndDialogScript();
    void setOnBlocked(std::string onBlocked) { _onBlocked = onBlocked; }
    const std::string &onDialogue() const { return _onDialogue; }
    void setOnDialogue(std::string onDialogue) { _onDialogue = onDialogue; }

    // END Scripts

    // IAnimationEventListener

    void onEventSignalled(const std::string &name) override;

    // END IAnimationEventListener

    int getSpellSaveDC(int spellId) const;
    /** Plays the Force resisted block. */
    void playForceResistedAnimation();
    /** A Force push carries the creature to \p destination at 25 metres a second. */
    void beginForcePush(const glm::vec3 &destination);
    bool isForcePushed() const { return _forcedMove && _forcedMove->pushed; }
    /**
     * Carries the creature straight over the ground to destination in a third
     * of a second, as a Force jump does, whatever it is doing meanwhile.
     */
    void beginLeap(const glm::vec3 &destination);
    /** Ends a push or leap carrying the creature; it stays where it has got to. */
    void endForcedMove();

    int furyDamageBonus() const { return _furyDamageBonus; }
    int furySpellState() const { return _furySpellState; }
    /** Fury or Wookiee Rage of \p spellId starts, with no damage bonus yet. */
    void applyFuryState(int spellId);
    /** Fury ends. */
    void clearFuryState();
    /** In TSL a running Fury adds a point of damage, up to six. */
    void incrementFuryDamageBonus();

protected:
    bool canExecuteActions() const override;
    /** A party member at zero vitality takes up its actions too, which mostly fail. */
    bool takesUpActions() const override;
    glm::quat presentedOrientation() const override;
    /** Runs the conversation script, the fallback one for a creature with none or "default". */
    void runConversationScript() override;

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;

    /** The bone a head turns by when the appearance names none. */
    static constexpr char kDefaultHeadBone[] = "hturn_g";
    /** How far, in degrees, a head turns across when the appearance leaves it blank. */
    static constexpr float kDefaultHeadArcH = 40.0f;
    /** How far, in degrees, a head turns up and down when the appearance leaves it blank. */
    static constexpr float kDefaultHeadArcV = 30.0f;
    /** The seconds a dead creature's body waits before it is destroyed when the appearance leaves the delay blank. */
    static constexpr float kDefaultDestroyObjectDelay = 3.0f;
    /** How far above its feet the walking space reaches when the appearance leaves its height blank. */
    static constexpr float kBlankCollisionHeight = 1.0f;

    /**
     * Which per-frame animation rules a creature follows: the controlled
     * leader acting freely is driven, a party member following the leader
     * follows, everyone else takes the default rules.
     */
    enum class AnimationUpdater {
        Default,
        Drive,
        Follow
    };

    /**
     * The pose a creature lies in once dead, chosen when it first dies: a
     * dead pose reached through its die clip, the second dead pose reached
     * through the second die clip, or the third dead pose reached through its
     * own lead-in.
     */
    enum class DeathPose {
        Dead,
        Dead1,
        Dead3
    };

    /** What walking into another creature does to a walk. */
    enum class Bump {
        /** The walk is over, as if arrived. */
        WalkEnded,
        /** A way round is spliced into the path. */
        Detoured
    };

    /** How planning a way round a creature in the path went. */
    enum class Avoidance {
        Failed,
        /** The walk ends inside the hexagon round the blocker, so it stops. */
        EndInHex,
        Planned
    };

    /** The clip a walking or running creature shows, its pose and its speed. */
    struct MovementClip {
        std::shared_ptr<graphics::Animation> anim;
        int id {0};
        float speed {1.0f};
    };

    /**
     * Which hands are powered, as the walk and run clips see it: the right
     * hand, the left hand, and a last item powered on or off that is not a
     * lightsaber.
     */
    struct PoweredHands {
        bool right {false};
        bool left {false};
        bool notLightsaber {false};

        bool any() const { return right || left || notLightsaber; }
    };

    /** Walking into other creatures on the current path. */
    struct PathAvoidance {
        /** The creature last walked into and planned round, and where it stood then. */
        RuntimeObjectRef<Creature> blocker;
        glm::vec3 blockerPosition {0.0f};
        /** The last hostile creature found in the way. */
        RuntimeObjectRef<Creature> foundEnemy;
        /** Blocked steps since the path was planned. */
        int bumps {0};
        /** The next way round is tried counter-clockwise first. */
        bool side {false};
    };

    /** An item property through which the creature can use a power. */
    struct ItemPowerSource {
        std::shared_ptr<Item> item;
        size_t property;
        bool equipped;
    };

    /**
     * A move that carries the creature bodily, a Force push or a leap: one at
     * a time, the latest replacing any other. Only a push holds the creature.
     */
    struct ForcedMove {
        glm::vec3 destination;
        float speed;
        bool pushed;
    };

    /** The hit die and base Force point grant of one level of the player character. */
    struct LevelStats {
        int hitDie {0};
        int forcePoints {0};
    };

    /** A spell-like ability: its spell, readiness and caster level. */
    struct SpellLikeAbility {
        int spell {0};
        /**
         * Keep the full readiness value: some rules require exactly one,
         * while others accept any nonzero value.
         */
        int flags {0};
        int casterLevel {0};
    };

    /** A one-shot waiting in the fire-and-forget queue. */
    struct FireForgetEntry {
        std::string clip;
        uint32_t startDay {0};  /**< calendar day it was queued at */
        uint32_t startTime {0}; /**< and the time of that day */
        int lengthMilliseconds {0};
        float rate {1.0f};
        bool layered {false};
        AnimationSource source;
        std::optional<SwingAttack> attack;
    };

    /** The one-shot running from the fire-and-forget queue. */
    struct RunningOneShot {
        std::string clip;
        int id {-1}; /**< its animation ID */
        bool layered {false};
        bool overlayRow {false}; /**< taken off the model when its time runs out */
        float remainingMilliseconds {0.0f};
    };

    /**
     * The loop a one-shot returns to: a state-driven pose goes back through
     * the pose path, any other loop plays again with its properties.
     */
    struct StoredLoop {
        std::string clip;
        scene::AnimationProperties properties;
        bool stateDriven {true};
        AnimationSource source;
    };

    /** The attack penalty of two-weapon fighting, and the part a light off-hand weapon gives back. */
    struct TwoWeaponPenalty {
        int penalty {0};
        int smallOffhandBonus {0};
    };

    // Serializable
    RacialType _race {RacialType::Unknown};
    Subrace _subrace {Subrace::None};
    int _appearance {0};
    int _appearanceBeforeDisguise {0};
    bool _disguised {false};
    Gender _gender {Gender::Male};
    uint16_t _portraitId {0};
    /** The record's IsPC byte; any value but 0 marks a player character. */
    uint8_t _isPC {1};
    /** The creature's own copy of IsPC (see actsAsPC()); not saved. */
    uint8_t _actsAsPC {1};
    bool _playerCreated {false};
    /** Body armour this creature could not wear when it was loaded. */
    bool _bodyRefusedOnLoad {false};
    int32_t _assignedPuppet {-1};
    bool _puppet {false};
    Faction _faction {Faction::Invalid};
    uint32_t _factionJoinOrder {0};
    bool _disarmable {false};
    bool _noPermDeath {false};
    std::optional<std::string> _livingName;
    EffectId _activePoisonEffectId {kUnassignedEffectId};
    bool _notReorienting {false};
    uint8_t _bodyVariation {0};
    std::optional<CreaturePresentation> _presentation;
    uint8_t _textureVar {0};
    bool _partyInteract {false};
    int _walkRate {0};
    uint8_t _naturalAC {0};
    ArmorClassCache _armorClassCache;
    ArmorClassCursor _armorClassCursor;
    int _forcePoints {0};
    /**
     * LvlStatList: each level of the player character. Its maxima are
     * derived from it.
     */
    std::vector<LevelStats> _levelStats;
    int _currentForce {0};
    int _temporaryHitPoints {0};
    int _temporaryForcePoints {0};
    int _bonusForcePoints {0};
    /** The Fury damage bonus, -1 while Fury is off. */
    int _furyDamageBonus {kNoFuryDamageBonus};
    int _furySpellState {0};
    bool _temporaryHitPointsRestored {false};
    bool _temporaryForcePointsRestored {false};
    /**
     * The Constitution modifier vitality reads. It is taken up on load, on
     * base Constitution and level changes and when a Constitution effect is
     * admitted or removed.
     */
    int _vitalityConstitutionModifier {0};
    int _refBonus {0};
    int _willBonus {0};
    int _fortBonus {0};
    int _goodEvil {0};
    /**
     * TSL: a companion's own alignment, which its influence modulates into
     * the good/evil value. A record without one takes its good/evil value.
     */
    int _baseCNPCAlignment {0};
    float _challengeRating {0};
    uint32_t _encounterId {script::kObjectInvalid};
    bool _leftEncounter {false};
    uint32_t _orientationLock {script::kObjectInvalid};
    bool _engagedExchange {false};
    int _xp {0};
    bool _levelUpChimeArmed {true};
    int _joiningXP {0};
    CombatForm _currentForm {CombatForm::None};
    AutoBalanceContext _autoBalanceContext;
    ForceResistanceState _forceResistance;
    bool _autoBalancePlayerLevelAtSpawnSet {false};

    std::string _onNotice;
    std::string _onSpellAt;
    std::string _onAttacked;
    std::string _onDamaged;
    std::string _onDisturbed;
    std::string _onEndRound;
    std::string _onEndDialogue;
    std::string _onDialogue;
    std::string _onSpawn;
    std::string _onDeath;
    std::string _onBlocked;

    /**
     * CreatnScrptFird: whether this creature has run its creation script.
     * It persists across area attachments and saves.
     */
    bool _spawnScriptFired {false};

    // The door the last blocked event was raised for, as an object id, so a
    // door destroyed since resolves to no object.
    uint32_t _lastBlockingDoorId {script::kObjectInvalid};
    resource::LocString _firstName;
    resource::LocString _lastName;

    uint16_t _soundSetId {0xFFFF};
    /** A row of 0 takes the appearance's bag row. */
    uint8_t _bodyBagId {0};
    /**
     * The perception range row of a creature's record: by default the row its
     * appearance names.
     */
    uint8_t _perceptionId {11};

    CreatureAttributes _attributes;
    std::vector<SpellLikeAbility> _spellLikeAbilities;
    std::map<int, std::shared_ptr<Item>> _equipment;
    // END Serializable

    ModelType _modelType {ModelType::Creature};
    std::shared_ptr<graphics::Texture> _portrait;

    /**
     * The facing the model shows turns toward the facing wanted standing
     * still and toward the movement facing while moving.
     */
    float _presentationFacing {0.0f};
    float _desiredFacing {0.0f};
    float _movementFacing {0.0f};
    int _frameMilliseconds {0};

    // Current path that the creature is following, its velocity and position at
    // the previous frame.
    std::optional<Path> _path;
    bool _navigationFailed {false};
    /** The path under way walks all the way onto its destination. */
    bool _walksOntoPoint {false};
    /** The walk of navigateToUse under way. */
    std::optional<UseApproach> _useApproach;
    RuntimeObjectRef<Object> _attemptedMovementTarget;
    glm::vec3 _pathVelocity;
    /**
     * How much of its full speed a path walk goes at, which it carries from
     * one walk to the next; and the same for the pace of its walk and run
     * clips, which starts from nothing with each new model.
     */
    float _walkPace {1.0f};
    float _clipPace {0.0f};
    /** Unset for a frame that has no step to compare with. */
    std::optional<glm::vec3> _previousPosition;
    PathAvoidance _avoidance;
    // When there is no progress on the path, apply _stuckForce to steer the
    // creature in a random direction until the timer runs out.
    Timer _stuckTimer;
    glm::vec3 _stuckForce;
    std::optional<ForcedMove> _forcedMove;

    /** The movement-rate row's walk and run speeds, metres a second. */
    float _walkSpeed {0.0f};
    float _runSpeed {0.0f};
    /** How far one loop of the walk and run animations carries the appearance. */
    float _walkDistance {1.0f};
    float _runDistance {1.0f};
    /** The appearance's own sneaking pace. */
    float _stealthWalkSpeed {0.0f};
    float _driveSpeed {0.0f};
    float _driveMaxSpeed {0.0f};
    bool _running {false};
    float _personalSpace {0.6f};
    float _creaturePersonalSpace {0.6f};
    /** The appearance's height, which the head falls back on; zero when blank. */
    float _height {0.0f};
    /** How far above its feet the creature's walking space reaches. */
    float _collisionHeight {kBlankCollisionHeight};
    CreatureSize _size {CreatureSize::Invalid};
    bool _hasSizeCategory {false};
    MovementType _movementType {MovementType::None};
    bool _talking {false};

    ItemAttributes _itemAttributes;

    /** Fractional regeneration, carried from one update to the next. */
    float _hitPointAccumulator {0.0f};
    float _forcePointAccumulator {0.0f};
    Timer _stateSupportTimer;

    bool _movementRestricted {false};
    float _movementRate {1.0f};
    bool _runLimited {false};
    std::optional<MovementType> _movementTypeBeforeStateImmobilization;
    int _effectState {0};
    int _effectAmbientState {0};
    /**
     * The AI capabilities the creature has. Applying an AI-state effect takes
     * away what its mask lacks, and removing one restores what the remaining
     * effects allow unless the creature has none left; death takes them all,
     * raising gives them all back, and an immobile movement rate takes
     * movement. So it can differ from the effects.
     */
    int _effectAIStateMask {kAIStateUnrestricted};
    bool _bodyFuel {false};
    bool _throwParryBlocked {false};
    EffectId _internalStateEffectId {0};
    EffectId _activeStateRootId {0};
    std::map<int, int> _effectIconCounts;
    CombatState _combatState;
    std::weak_ptr<Action> _currentCombatAction;
    bool _clientCombatMode {false};
    float _mineCheckTime {0.0f};
    uint32_t _lastWeaponUsed {script::kObjectInvalid};
    SavedObjectReference _incomingAttacker;
    /** Where the creature last stood when it could not see its attack target. */
    std::optional<glm::vec3> _blockedAttackSight;
    AttackerList _attackerList;
    AttackHistory _receivedAttack;
    CombatStance _combatStance {CombatStance::None};
    uint32_t _totalDefenseDay {0};
    uint32_t _totalDefenseTime {0};
    /** The kind of the round action most recently dispatched. */
    int _roundActionKind {0};
    bool _stealthMode {false};
    bool _treatInjuryWorkDone {false};
    bool _conversationPaused {false};
    /** Milliseconds of excitement left, counted down by the frame time; 0 when not excited. */
    uint32_t _excitedMilliseconds {0};
    /** System clock time, in microseconds, at which the weapon swap is ready again. */
    uint64_t _switchWeaponsReadyAt {0};
    float _itemUseCooldown {0.0f};
    int _lastMeleeAttackVariant {-1};
    RuntimeObjectRef<Object> _lastHostileTarget;
    int _lastAttackAction {-1};
    FeatType _lastCombatFeat {FeatType::Invalid};
    /** The guard stance feat the creature holds; Invalid when it holds none. */
    FeatType _guardStanceFeat {FeatType::Invalid};
    int _lastForcePowerUsed {-1};
    AttackResultType _lastAttackResult {AttackResultType::Invalid};
    int _modifiedAttacks {0};
    bool _assuredHit {false};
    bool _assuredDeflection {false};
    bool _assuredReturn {false};
    bool _immortal {false};
    std::shared_ptr<resource::SoundSet> _soundSet;
    uint32_t _spawnedBodyBag {script::kObjectInvalid};
    Perception _perception;
    PerceptionRolls _perceptionRolls;
    float _perceptionPassTime {0.0f};
    bool _perceptionStarted {false};
    std::optional<PerceptionEvent> _lastPerception;
    float _blindSpot {120.0f};
    /**
     * The stealth field's shell, and the stealth exit's one-second shell,
     * each with the effect application order current when it began.
     */
    std::shared_ptr<graphics::Texture> _stealthShell;
    uint64_t _stealthShellOrder {0};
    std::shared_ptr<graphics::Texture> _cloakRemovalShell;
    uint64_t _cloakRemovalShellOrder {0};
    float _cloakRemovalTime {0.0f};
    int _aiLevel {1};
    int _savedAILevel {-1};
    int _perceptionThrottle {0};
    /** Heartbeat interval (ms) and the due updates a level-0 creature waits. */
    int _heartbeatInterval {3000};
    int _heartbeatThrottle {0};
    /** The ranges row the creature takes up when it next looks around. */
    std::optional<int> _pendingPerceptionRangeRow;
    bool _pendingPerceptionRangeByAppearance {false};
    /**
     * What the creature sees through and what blinds it. Effects set their
     * counter as they are applied; removing one takes it off and sets it again
     * from the effects of its type that remain. So it can differ from the
     * effects: removing one of several True Seeing effects gives Ultravision
     * in place of True Seeing, and darkness laid on a creature that sees
     * through it sets no counter until another blindness goes.
     */
    int _visibilityCounterBits {0};
    NPCAIStyle _aiStyle {NPCAIStyle::DefaultAttack};
    uint32_t _healTarget {script::kObjectInvalid};

    /** Head look-at: the object looked at, and whether the look still runs. */
    uint32_t _lookAtId {script::kObjectInvalid};
    RuntimeObjectRef<Object> _lookAtTarget;
    float _lookAtDistance {0.0f};
    bool _lookAtRunning {false};
    /** The buffer lookAtModels() fills. */
    mutable std::vector<std::reference_wrapper<scene::ModelSceneNode>> _lookAtModels;
    bool _headLookSuspended {false};
    /**
     * The appearance's head look: whether it turns its head, the bone it
     * turns, and how far across and up.
     */
    bool _headTrack {false};
    std::string _headBone {kDefaultHeadBone};
    float _headArcH {kDefaultHeadArcH};
    float _headArcV {kDefaultHeadArcV};

    uint32_t _footstepType {0};
    int _gold {0}; /**< aka credits */
    std::string _envmap;
    /** The appearance's death visual and the body node it goes off at. */
    std::optional<int> _deathVisual;
    std::string _deathVisualNode;
    std::optional<int> _appearanceBodyBag;
    std::optional<int> _fadeDelayOnDeath;
    /** Read from the appearance with the rest of its properties. */
    float _hitRadius {0.0f};
    AppearanceSoundSet _appearanceSoundSet;
    /** How long the body stays after death, in seconds. */
    float _destroyObjectDelay {kDefaultDestroyObjectDelay};
    std::string _appearanceRace;
    /** The creaturespeed.2da name of the appearance's movement rate. */
    std::string _appearanceMoveRate;
    std::string _bloodColour;
    /** The appearance's free-look video effect, -1 for none. */
    int _freeLookEffect {-1};
    /** The equipment slots the appearance locks, as a mask. */
    uint32_t _equipSlotsLocked {0};
    /** The appearance each armour body variation from A to L hands its model to, 0 for none. */
    std::array<int, 12> _armourAppearances {};
    /** The appearance's perception range row. */
    int _appearancePerceptionRange {0};

    std::shared_ptr<audio::AudioSource> _audioSourceVoice;
    std::shared_ptr<audio::AudioSource> _audioSourceFootstep;
    bool _lightsaberIdlePowerDownPending {false};
    Timer _lightsaberIdlePowerDownTimer;
    PoweredHands _poweredHands;
    /**
     * In KotOR a script can hold the hand items as it set them: nothing else
     * powers them on or off until a script lets go.
     */
    bool _handPowerHeld {false};

    // Animation

    bool _animDirty {true};
    bool _animFireForget {false};
    bool _injuredPause {false};                /**< given the injured pause at a round pause end */
    bool _injuredIdle {false};                 /**< the idle pause turned into the injured pause */
    bool _damageFlinchHeld {false};            /**< flinched since another animation was chosen */
    std::optional<std::string> _castAnimation; /**< held by a cast or an item use */
    bool _equipmentHidden {false};             /**< an animation that hides equipped items put the hand weapons away */
    SpellCastVisuals _spellCastVisuals;
    SpecialAttackVisuals _specialAttackVisuals;
    bool _disableInjuredAnim {false};
    /** The appearance breaks off a swing that is parried. */
    bool _abortOnParry {false};
    std::deque<FireForgetEntry> _fireForgetQueue;
    std::optional<RunningOneShot> _oneShot;
    std::optional<StoredLoop> _storedLoop;
    AnimationSource _animationSource;        /**< the clip shown */
    std::optional<SwingAttack> _swingAttack; /**< the attack of the last swing started */
    bool _weaponDrawPending {false};         /**< a draw waiting for play to resume */
    /**
     * Idle fidgets: milliseconds spent in the plain pause, and when the next
     * fidget is due.
     */
    int _fidgetTime {0};
    int _fidgetDelay {0};
    std::shared_ptr<graphics::LipAnimation> _lipAnimation;
    std::optional<DeathPose> _deathPose;

    // END Animation

    // Ground tilt

    /** The appearance follows the ground while alive. */
    bool _groundTiltAppearance {false};
    /** The tilt last aimed for and the tilt shown, applied beneath the facing. */
    glm::quat _groundTiltTarget {1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat _groundTilt {1.0f, 0.0f, 0.0f, 0.0f};
    /** See groundRoomHint. */
    int _groundRoomHint {-1};

    // END Ground tilt

    // Scripts

    // END Scripts

    void loadTransformFromGIT(const resource::generated::GIT_Creature_List &git);

    void onEffectsRestored() override;
    void updateModel();

    // Refresh appearance-derived state (model type, size, speeds, footstep, envmap,
    // portrait) for the current _appearance, without building a scene node.
    void loadAppearanceProperties();
    /** Read the appearance's sound set, a row of appearancesndset.2da. */
    void loadAppearanceSoundSet(int row);
    /** Read the appearance's movement rate, the one the default rate stands for. */
    void loadAppearanceMoveRate(const resource::TwoDA &appearances);
    /**
     * In an area, move to the nearest spot within reach where the creature's
     * current size fits; stay put when there is none.
     */
    void standWhereAppearanceFits();

    // Presentation-only equipment snapshots retain their visual-only override.
    // Live equipment uses the Disguise effect provider and restores the original
    // appearance when none remains. Updates _appearance only; callers rebuild the model.
    void updateDisguise();
    void updateEquipmentPresentation();
    void updateCombat(float dt);
    void updateLightsaberSoundPositions();

    void runDeathScript();
    void runDamagedScript();

    ModelType parseModelType(const std::string &s) const;

    /**
     * Whether this creature walking into the other pushes it aside. In TSL the
     * party leader pushes party members and puppets, but not while a
     * conversation is under way: from the moment one is asked for, such as the
     * click to talk that sends the leader walking over, until it ends. Nobody
     * else pushes, and in KotOR nobody does.
     */
    bool canPushAside(const Creature &other) const;
    /** Deals with walking into \p blocker on a step along \p stepDir. */
    Bump bumpIntoCreature(const std::shared_ptr<Creature> &blocker, Pathfinder &pf, const glm::vec2 &stepDir);
    /**
     * Plans a way round \p blocker into the path. \p side picks the way tried
     * first and is set to the way taken.
     */
    Avoidance plotPathAroundCreature(const std::shared_ptr<Creature> &blocker, Pathfinder &pf, const glm::vec2 &stepDir,
                                     bool &side);
    /** Drop any walk under way and plan one along the straight line to a point. */
    void planStraightPath(const glm::vec3 &point);
    /** The walk or run clip \p model shows for the creature's movement. */
    MovementClip movementClip(const scene::ModelSceneNode &model) const;

    /** The area whose music this creature drives: its own, when the player controls it. */
    std::shared_ptr<Area> battleMusicArea() const;
    /** Shouts one of the first five battle cries of the sound set, if the leader hears it. */
    void playBattleCry();
    /** Pure Good powers at good/evil 100, Pure Evil powers at 0, neither otherwise. */
    void updatePureGoodEvilPowers();
    /** Takes off the held Pure Good or Pure Evil powers. */
    void removePureAlignmentPowers(bool good);
    /** Held powers of the same kind are taken off and applied afresh. */
    void addPureAlignmentPowers(bool good);
    /** The weapon swap and the flourish share one second of real time between uses. */
    bool isSwitchWeaponsCoolingDown() const;
    void startSwitchWeaponsCooldown();
    void updateExcitement();
    void updateForcedMove(float dt);
    AnimationUpdater animationUpdater() const;
    /** The party follow slot, 1 or 2, or -1 outside them. */
    int followSlot() const;
    void updateTurning();
    void rotateStep(float target);
    bool isHostileCombatTurn() const;
    void playTurnAnimation(float sign);
    void updateFidget();
    void updateStateHeartbeat(float dt);
    /** Runs the heartbeat scripts of a living creature. */
    void runHeartbeat();
    void updateRegeneration(float dt);
    void updateMineCheck(float dt);
    void reportMineDetection(const Trigger &trap, int roll, int rank, int dc);
    void updateK1HitPointRegeneration(float dt);
    void updateHitPointRegeneration(float dt);
    void updateForcePointRegeneration(float dt);
    void updateK1ForcePointRegeneration(float dt);
    void updateAILevel();
    /** Draws the detection rolls and the perception, heartbeat and fidget counters a new creature starts with. */
    void startUnsavedCounters();
    /** Starts the heartbeat clock of a creature whose heartbeat has not been stamped. */
    void startUnstampedHeartbeat();

    // Talent selection

    /** Whether the creature can pay a known power when the AI picks a talent. */
    bool talentPowerAffordable(const Spell &spell) const;
    /** The item properties through which the creature can use a power. */
    std::vector<ItemPowerSource> talentItemPowers() const;

    // END Talent selection

    // Stealth

    void presentStealth(bool enabled, bool announce);
    void presentStealthField(bool enabled, bool announce);
    /** TSL's stealth feat or one of its Force Camouflage powers. */
    bool hasTSLStealthTalent() const;
    /** TSL Dampen Sound on an equipped item. */
    bool hasSilentMove() const;

    // END Stealth

    // Saving throws

    int getSavingThrowBase(SavingThrow save) const;
    int getSavingThrowEffectBonus(SavingThrow save, SavingThrowType type,
                                  const std::optional<std::reference_wrapper<const Object>> &versus) const;

    // END Saving throws

    /**
     * Whether the creature can defend against a missed shot of \p shooter:
     * a parry or deflection, or, with \p allowShield, a strike on a Force
     * shield (\p shieldHit). \p canReturn tells whether the shot can be sent
     * back.
     */
    bool projectileDefenseEligible(const Creature &shooter, int damageFlags,
                                   const std::shared_ptr<Item> &weapon,
                                   bool allowShield, bool &shieldHit, bool &canReturn) const;

    // Head look-at

    void updateLookAt();
    /**
     * The body model and a separate head model; the look runs in whichever has
     * the bone. The list is rebuilt in a buffer the creature keeps.
     */
    const std::vector<std::reference_wrapper<scene::ModelSceneNode>> &lookAtModels() const;

    // END Head look-at

    // Ground tilt

    /** After its actions, a creature in an area stands on its ground (see Area::groundCreature). */
    void standOnGround();
    void updateGroundTilt();
    void updateTransform() override;

    // END Ground tilt

    // Appearance

    std::shared_ptr<scene::ModelSceneNode> buildModel();
    void finalizeModel(scene::ModelSceneNode &body);

    std::string getBodyModelName() const;
    std::string getBodyTextureName() const;
    std::string getHeadModelName() const;
    std::string getMaskModelName() const;

    // END Appearance

    // Animation

    bool doPlayAnimation(bool fireForget, const std::function<void()> &callback);
    /**
     * Records a loop arriving; a loop other than the stored one empties the
     * queue. False while a one-shot runs: the loop waits for it.
     */
    bool storeLoop(const std::string &clip, const scene::AnimationProperties &properties, bool stateDriven,
                   AnimationSource source);
    void returnToStoredLoop();
    /**
     * The body shows \p clip: one that hides equipped items puts the hand
     * weapons away; the next one that does not, outside a conversation, brings
     * them back and takes away the creature's thrown lightsaber still in flight.
     */
    void presentEquippedItems(const std::string &clip);
    /**
     * A new clip is shown; any but the plain pause restarts the fidget time.
     * A world-space cut clip plays with the model at the world origin and
     * suspends the head look; the next other clip puts the model back.
     */
    void setAnimationSource(AnimationSource source);
    void playTopFireForgetAnimation();
    /**
     * The hit of a melee swing sounds the weapon against the target, makes a
     * creature-model target that can react flinch at once, as a layer outside
     * its queue, may draw a pain grunt from a creature target, and shows the
     * hit's sparks. A parried or missed swing against a parrying target sounds
     * the weapon's parry.
     */
    void presentSwingHit();
    /** The sparks of the swing's hit on a creature or a door. */
    void presentSwingSparks(Object &target);
    /** The weapon of the swing's attack heard against the target, or its parry. */
    void playHitSound(const Object &target, bool parried);
    /** The item whose weapon sound the swing's attack makes, none for an unarmed or creature attack. */
    std::shared_ptr<Item> weaponForHitSound(PhysicalAttackKind kind) const;
    /** The material a weapon sounds against on the target; empty when it has none. */
    std::string hitSoundMaterial(const Object &target) const;
    /** A swing, clash or parry sound of the weapon the creature swings, one of \p variants. */
    void playSwingSound(const std::string &name, int variants);
    /** Plays a weaponsounds.2da sound at the creature. */
    void playWeaponSound(int weaponSoundRow, const std::string &column);
    /** Plays the creature's body-fall sound for the surface under it. */
    void playFallSound();
    /** The shown clip is a parry. */
    bool isShowingParry() const;
    void queueLoopTransition(int newId, int oldId, float speed);
    void queueTransitionPair(const std::string &clip, int clipId, const std::string &follow, int followId);
    void switchOffOverlay(const RunningOneShot &oneShot);
    /** The draw played as a layer over movement, or empty without one. */
    std::string getWeaponDrawOverlayAnimation() const;

    std::string getAnimationName(AnimationType anim) const override;
    std::string getAnimationName(CombatAnimation anim, CreatureWieldType wield, int variant) const;
    /** The clip under any layers, or empty without one. */
    std::string getActiveAnimationName() const override;

    std::string getDeadAnimation() const;
    std::string getDieAnimation() const;
    bool isInFallenLoop() const;
    /** The clips that lead a dying creature into its dead pose from the loop \p leftLoopId, or -1 for none. */
    void playDieTransition(int leftLoopId);
    /** The appearance's death visual, shown at its node on the body. */
    void presentDeathVisual();
    std::string getHeadTalkAnimation() const;
    /** Takes the talking head off the body and the head: at once, or blending it out when \p fade. */
    void removeHeadTalk(bool fade);
    std::string getPauseAnimation(bool injured) const;
    std::string getReadyAnimation() const;
    /** The ready pose of a class-0 character stands, showing the pause clip. */
    bool showsClassZeroReady() const;
    /** Hands with no flinch take the flinch pose, showing the pause clip. */
    void holdDamageFlinchPose();
    /** The flinch pose of hands with no flinch stands. */
    bool showsDamageFlinchPose() const;
    std::string getDamageFlinchAnimation() const;
    std::string getRunAnimation() const;
    std::string getStealthWalkAnimation() const;
    std::string getWalkAnimation() const;
    /** A hand item powered on or off updates which hands count as powered. */
    void updatePoweredHands(const Item &item, bool on);
    /** Powers both hand items on or off, whether or not a script holds them. */
    void powerHandItems(bool powered, bool animate);
    /** Powers the item in \p slot on or off; only a lightsaber in a hand lights. */
    void powerSlotItem(int slot, bool powered, bool animate);

    /**
     * @return creatureAnim if model type is creature, elseAnim otherwise
     */
    inline std::string getFirstIfCreatureModel(std::string creatureAnim, std::string elseAnim) const;

    /**
     * The attack penalty of fighting with two weapons when attacking with
     * \p weapon, if any. A light off-hand weapon eases the main hand's
     * penalty; \p rangedSheet judges it, as the ranged attack value of the
     * equip screen does, by the right-hand weapon's size.
     */
    TwoWeaponPenalty getTwoWeaponAttackPenalty(
        const std::shared_ptr<Item> &weapon,
        bool offHand,
        bool rangedSheet = false) const;
    int getDuelingBonus() const;

    // END Animation

    // Blueprint
    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void deserializeName(const resource::Gff &gff);
    void deserializeSoundSet(const resource::Gff &gff);
    void deserializeAttributes(const resource::Gff &gff);
    void deserializeClass(const resource::Gff &gff);
    void deserializePerception(const resource::Gff &gff);
    /** The skill rank, \p versus a creature or unopposed. */
    int skillRank(SkillType skill, bool baseOnly,
                  const std::optional<std::reference_wrapper<const Creature>> &versus) const;
    bool isStationaryForDetection() const;
    void reportStealthDetection(const Creature &target, int spotRoll, int hideRoll, int aware, int rank,
                                int observerStill, int targetStill, int distance, int facing, int run, int combat) const;
    /**
     * The slot a loaded equipment record may take beside the equipment
     * already loaded; none when the creature may not wear it.
     *
     * @param reportRefusal whether a refusal tells the player why
     */
    std::optional<int> admitLoadedEquipment(
        const Item &item, uint32_t slotMask,
        const std::map<int, std::shared_ptr<Item>> &loaded, bool &bodyRefused, bool reportRefusal);
    /**
     * Replaces the carried items and the equipment with those of the record's
     * lists.
     *
     * @param placedTemplate whether the record is the template of a creature
     *        an area places: each entry then names its list's template and
     *        nothing more, and an entry without one is left out
     */
    void deserializeOwnedItemsAndEquipment(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        bool placedTemplate);
    /** Credits, chemicals, components and pazaak cards read into the inventory are acquired as counts. */
    void countLoadedItems();
    /**
     * Appends the effects \p item worn in \p slot gives; with
     * \p onlyDeferredEffects, only its armour class and disguise effects.
     */
    void appendEquippedItemEffects(
        std::deque<EffectInstance> &effects,
        int slot,
        const std::shared_ptr<Item> &item, bool onlyDeferredEffects = false) const;
    /** The creature's effects without those \p source, if any, gives as worn equipment. */
    std::deque<EffectInstance> effectsWithoutEquippedSource(
        const std::optional<std::reference_wrapper<const Item>> &source) const;
    std::deque<EffectInstance> rebuildEquippedItemEffects(
        const std::map<int, std::shared_ptr<Item>> &equipment) const;
    void refreshVitalityConstitution();
    void followMaximumHitPoints(int previousMaximum);
    /**
     * Whether the creature's vitality makes it dead: never for a party member,
     * at -10 or less for a player character, else at 0 or less.
     */
    bool hitPointsAtDeathThreshold() const;
    void restoreSerializedVitality();
    void applyHitPointDamage(int amount, const std::shared_ptr<Object> &damager,
                             std::optional<DamageReaction> reaction = std::nullopt);
    int consumeTemporaryHitPoints(int amount);
    void updateDeathFromCurrentHitPoints();
    // END Blueprint
};

} // namespace game

} // namespace reone
