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
#include <optional>
#include <set>
#include <string>

#include <glm/vec3.hpp>

#include "savedruntime.h"
#include "types.h"

namespace reone {

namespace scene {
class ModelSceneNode;
class ISceneGraph;
} // namespace scene

namespace game {

class Action;
struct CombatRound;
class Creature;
class Item;
class Object;
class Game;
struct Spell;
class Location;
enum class SpellCastAnimation;

/** What a committed cast was cast with, as its impact script reads it. */
struct SpellCastContext {
    /** The spell cast, or -1 for none. */
    int spellId {-1};
    /** The caster level the impact script reads. */
    int casterLevel {0};
    /** The metamagic the impact script reads; 255 for none. */
    int metaMagic {255};
    /** The Force points the cast cost. */
    int forcePointCost {0};
    /** The class the spell was cast from. */
    int castingClass {kUnselectedCastingClass};
};

/**
 * What a spell's impact script reads of the cast: its context, target, item
 * and location, the target of the cast under way, and a caster level that
 * overrides the cast's own. It is saved with its object.
 */
class SpellScriptContext {
public:
    /** The cast the impact script reads. */
    SpellCastContext cast;
    /** The caster level that replaces the cast's own, if any. */
    std::optional<int> levelOverride;

    /**
     * Takes the impact's cast \p context, its \p target and \p item, and the
     * \p location it lands at, if any.
     */
    void setImpact(const SpellCastContext &context,
                   const std::shared_ptr<Object> &target,
                   const std::shared_ptr<Object> &item,
                   std::optional<std::reference_wrapper<const Location>> location);
    /** The impact's target, if it still exists. */
    std::shared_ptr<Object> target() const;
    /** The item the impact comes from, if it still exists. */
    std::shared_ptr<Object> item() const;
    /** A copy of the location the impact lands at, if it has one. */
    std::shared_ptr<Location> location() const;
    /** The target of the cast under way, if it still exists. */
    std::shared_ptr<Object> activeTarget() const;
    /** Sets the target of the cast under way. */
    void setActiveTarget(const std::shared_ptr<Object> &target);
    /** Forgets the target of the cast under way. */
    void clearActiveTarget();

    bool restored() const { return _restored; }
    /** Restores the context from its saved \p state; without one it is cleared. */
    void restore(std::optional<std::reference_wrapper<const resource::Gff>> state);
    /**
     * Turns the object ids of a restored context into objects, through
     * \p resolver; a context that was not restored is left as it is.
     */
    void bind(const std::function<std::shared_ptr<Object>(uint32_t)> &resolver);
    /**
     * The context as saved, with its objects given by \p objectId; none for a
     * context that holds nothing.
     */
    std::shared_ptr<resource::Gff> save(
        const std::function<uint32_t(const Object &)> &objectId) const;
    /** Forgets the objects that leave with the area, keeping those whose ids are in \p retainedObjectIds. */
    void retireAreaRuntime(const std::set<uint32_t> &retainedObjectIds);

private:
    RuntimeObjectRef<Object> _target;
    RuntimeObjectRef<Object> _item;
    RuntimeObjectRef<Object> _activeTarget;
    std::shared_ptr<Location> _location;
    std::array<uint32_t, 3> _savedIds {{kSavedRuntimeInvalidObjectId,
                                        kSavedRuntimeInvalidObjectId, kSavedRuntimeInvalidObjectId}};
    bool _needsBind {false};
    bool _restored {false};
};

/**
 * The caster level a script reads for \p object: an area of effect's is its
 * creator's, for the area's spell.
 */
int queryCasterLevel(const Object &object);
/** The spell an item property casts for \p actor: in TSL, a wrist launcher launches grenades. */
SpellType itemSpellForActor(const Object &actor, SpellType propertySpell);
/** Casts the next item spell of \p caster at the level of its last class. */
void publishItemCastLevel(Creature &caster);

/** The caster's equipment neither forbids the power nor lacks what it requires. */
bool equipmentAllowsSpell(const Creature &caster, const Spell &spell);
/** Whether \p actor may cast \p spell now, from \p selection if given, without paying for it. */
bool admitSpellCast(const Object &actor, const Spell &spell, bool freeCast, bool itemCast = false,
                    std::optional<SpellSelection> selection = std::nullopt);
/** How far \p actor can cast \p spell, at \p target if given. */
float spellRange(const Object &actor, const Spell &spell, std::optional<std::reference_wrapper<const Object>> target = std::nullopt);
/** Whether \p position, or \p target there, is within the spell's range in three dimensions; the actor itself always is. */
bool withinSpellRange(const Object &actor, const Spell &spell, const glm::vec3 &position,
                      std::optional<std::reference_wrapper<const Object>> target = std::nullopt);
/**
 * Pays for a cast admitted when it was dispatched, filling \p context with
 * what it was cast with; the payment itself never fails. A free cast (a cheat
 * or an item use) pays nothing and casts at the spell's innate level. An
 * instant cast pays nothing, uses up no ability and leaves its caster's
 * casting class as it was. Every creature cast records what its charge
 * spends in Force points. Returns whether the cast goes ahead.
 */
bool commitSpellCast(Object &actor, const Spell &spell, bool freeCast, bool instant,
                     SpellCastContext &context, std::optional<SpellSelection> selection = std::nullopt);
/** The path itself if a projectile can take it, otherwise the default path. */
ProjectilePathType normalizeProjectilePath(ProjectilePathType path);
/**
 * The path a script command names: the default, homing, ballistic, high
 * ballistic or accelerating path. Any other value names none, and the command
 * then casts nothing.
 */
std::optional<ProjectilePathType> projectilePathFromScript(int value);
/**
 * The casting source a script command picks for a creature: the first of its
 * first two classes that uses the Force, has uses of the spell left and can
 * cast it, else its spell-like ability for the spell when that is ready.
 * Without either the command casts nothing.
 */
std::optional<SpellSelection> scriptCastingSource(const Creature &caster, const Spell &spell);
/**
 * What a spell's release shows on its caster: the cast sound and, on a
 * creature, the cast visuals. Ordinary, fake and instant casts alike, and an
 * item used at once.
 */
void presentSpellRelease(Object &caster, const Spell &spell);
/**
 * How long a spell's projectile flies from \p origin to \p destination on
 * \p path, in milliseconds; 0 for a spell without one.
 */
uint32_t spellProjectileTimeMilliseconds(
    const Spell &spell, const glm::vec3 &origin, const glm::vec3 &destination,
    ProjectilePathType path, bool tsl);
/** The path a spell's projectile takes: the spell's own, unless another is given. */
ProjectilePathType effectiveProjectilePath(const Spell &spell, ProjectilePathType overridePath);

/**
 * Queues the spell's impact on the module's event queue, \p delayMilliseconds
 * from now, to land at \p position with the cost \p context records. Returns
 * whether it was queued.
 */
bool queueSpellImpact(Game &game, const Spell &spell, Object &caster,
                      const std::shared_ptr<Object> &target, const glm::vec3 &position,
                      const SpellCastContext &context, const std::shared_ptr<Object> &item = nullptr,
                      uint32_t delayMilliseconds = 0);

/**
 * Use an item at once, as the inventory does: its first cast-spell property,
 * whether or not an upgrade gates it, takes effect on the user after the
 * user's last measured spell flight, with no action or animation (the release
 * still shows its cast sound and cast visuals), spends one
 * use (an exhausted item is destroyed), and, in combat, holds back the next
 * item use. A party member's use breaks the forfeit condition that forbids
 * items.
 */
void useItemAtOnce(Game &game, Creature &user, const std::shared_ptr<Item> &item);

/**
 * The location an item use from a menu carries: its target's position, or
 * the origin when the user uses the item on itself.
 */
glm::vec3 menuItemLocation(const Object &user, const Object &target);

/**
 * A creature's cast or item use at a creature fails once that creature has
 * died or is a party member at zero vitality.
 */
bool spellTargetLost(const Object &caster, const Object &target);

/**
 * After its release an item use ends as soon as its item is gone, except a
 * droid utility's (base item type 12), which plays on to its end.
 */
bool itemUseEndsWithItem(int itemType);

/**
 * A failed item use leaves its user in its pause or ready pose. One that
 * fails before taking effect because its item has no use left tells the
 * controlled creature so.
 */
void presentFailedItemUse(Game &game, Object &user, bool noUseLeft = false);

/**
 * Runs the spell's impact script for \p caster, with the cast's context, the
 * caster's own unless given, and \p levelOverride as the caster level.
 */
void runSpellImpact(Game &game, const Spell &spell, Object &caster,
                    const std::shared_ptr<Object> &target, const std::shared_ptr<Location> &location,
                    std::optional<SpellCastContext> context = std::nullopt, const std::shared_ptr<Object> &item = nullptr,
                    std::optional<int> levelOverride = std::nullopt);

/** The phases of a cast in time: the conjure, the cast and the catch. */
class SpellSchedule {
public:
    explicit SpellSchedule(float conjTime, float castTime, float catchTime = 0.0f) :
        _conjTime(conjTime), _castTime(castTime), _catchTime(catchTime) {}

    /**
     * Where the cast is: each phase is waited for, then entered for one
     * update.
     */
    enum State {
        WaitConjure,
        Conjure,
        WaitCast,
        Cast,
        WaitEffect,
        Effect,
        WaitFinish,
        Finish,
    };

    /**
     * Advances the cast by \p dt seconds as part of \p round. The cast waits
     * to start while the round holds \p action back; once started it keeps
     * its own time. Returns the state reached.
     */
    State update(const CombatRound &round, Action &action, float dt);
    /**
     * Advances the cast by \p dt seconds; it starts once \p canStart. Returns
     * the state reached.
     */
    State update(bool canStart, float dt);
    /** Sets how long the conjure, the cast and the catch last, in seconds. */
    void setDurations(float conjTime, float castTime, float catchTime) {
        _conjTime = conjTime;
        _castTime = castTime;
        _catchTime = catchTime;
    }
    /** Writes the schedule into the saved cast \p record. */
    void save(SavedCastAction &record) const;
    /** Takes the schedule from the saved cast \p record. */
    void restore(const SavedCastAction &record);
    /** Whether the cast has not yet been released. */
    bool awaitingRelease() const { return _state < WaitFinish; }
    /** The seconds since the cast started. */
    float time() const { return _time; }
    /** The last update carried the cast's time across \p seconds. */
    bool reached(float seconds) const { return _previousTime < seconds && _time >= seconds; }
    /**
     * The cast holds its round until its completion (including the
     * associated-feat expenditure) has run.
     */
    bool holdsRound() const { return _state != Finish; }
    /** The seconds left until the catch ends. */
    float remaining() const { return std::max(0.0f, _conjTime + _castTime + _catchTime - _time); }

private:
    State _state {WaitConjure};
    float _time {0.0f};
    float _previousTime {0.0f};
    float _conjTime {0.0f};
    float _castTime {0.0f};
    float _catchTime {0.0f};
};

/**
 * What using an item shows, by its base item type: the clip and its animation
 * ID, whether it loops, whether the user is posed instead of holding it,
 * whether it is drawn on the creature it is used on, when the droid utility
 * turns to its loop, when the item takes effect, and when the use ends in the
 * user's pose.
 */
struct ItemUsePresentation {
    std::string clip;
    int clipId {-1};
    bool loops {false};
    bool hold {true};
    bool onTarget {false};
    float loopAt {-1.0f};
    float impact {0.001f};
    float end {1.5f};
};

/**
 * The models a spell shows on its caster's head, hand and feet. The conjure
 * visuals come with the conjure; the release brings the cast visuals and fades
 * the conjure visuals out. After the release the conjure visuals go within five
 * seconds and the cast visuals five seconds later; the next conjure or release
 * replaces them, and a cast that ends before its release takes both away.
 */
class SpellCastVisuals {
public:
    ~SpellCastVisuals();

    /** Shows the spell's conjure visuals on \p caster. */
    void showConjure(Creature &caster, const Spell &spell);
    /** Shows the spell's cast visuals on \p caster and fades the conjure visuals out. */
    void showCast(Creature &caster, const Spell &spell);
    /** Takes every visual away. */
    void clear();
    /** Advances the fading visuals by \p dt seconds. */
    void update(float dt);

private:
    /** A model shown on a node of the caster's body. */
    struct Attached {
        std::weak_ptr<scene::ModelSceneNode> body;
        /** The name of the body node the model hangs from. */
        std::string hook;
        std::shared_ptr<scene::ModelSceneNode> model;
        bool fading {false};
    };
    using Set = std::array<Attached, 3>;

    static void attach(Set &set, Creature &caster, const std::array<std::string, 3> &models,
                       const std::string &animation);
    static void detach(Attached &attached);
    static void detach(Set &set);

    Set _conjure;
    Set _cast;
    bool _released {false};
    float _conjureRemaining {0.0f};
    float _castRemaining {0.0f};
};

/**
 * An entangled caster's paid cast fails: the caster shows its combat-ready
 * pose, in combat or not, its orientation is released, and a controlled caster
 * reads the failure line. Returns whether the cast was interrupted.
 */
bool interruptEntangledCast(Creature &caster);

/**
 * Shows each frame what a cast or an item use does: the conjure, the cast clip
 * held for the cast time, and the pose at the end. An item use is shown by its
 * item's base item type.
 */
class CastPresenter {
public:
    /** The item use's presentation and times, computed when it starts. */
    const ItemUsePresentation &itemUse(const Creature &user, const Spell &spell, int itemType,
                                       std::optional<std::reference_wrapper<const Creature>> targetCreature, const glm::vec3 &targetPosition);
    /** Shows the cast or item use in \p state. */
    void update(Creature &caster, const Spell &spell, std::optional<int> itemType, std::optional<std::reference_wrapper<Creature>> targetCreature,
                const glm::vec3 &targetPosition, const SpellSchedule &schedule, SpellSchedule::State state);
    /** Shows again what a restored cast was showing. */
    void restore(Creature &caster, const Spell &spell, const SpellSchedule &schedule);

private:
    std::optional<ItemUsePresentation> _item;

    void updateItem(Creature &user, std::optional<std::reference_wrapper<Creature>> targetCreature,
                    const SpellSchedule &schedule, SpellSchedule::State state);
};

} // namespace game

} // namespace reone
