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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <vector>

#include "reone/script/enginetype.h"

#include "runtimeref.h"
#include "saveprovenance.h"
#include "types.h"

namespace reone {

namespace resource {
class Gff;
}

namespace script {
struct Variable;
}

namespace game {

/**
 * The milliseconds an effect of \p seconds lasts. Whole days are taken off
 * the duration one at a time, in single precision, so the remainder carries
 * its rounding.
 */
inline uint64_t getEffectDurationMilliseconds(float seconds, uint32_t dayMilliseconds) {
    if (seconds <= 0.0f) {
        return 0;
    }
    auto daySeconds = static_cast<float>(dayMilliseconds / 1000u);
    uint64_t days = 0;
    while (seconds >= daySeconds) {
        seconds -= daySeconds;
        ++days;
    }
    return days * dayMilliseconds + static_cast<uint32_t>(seconds * 1000.0f);
}

/** The game time, in milliseconds, an effect of \p seconds applied at \p start expires. */
inline uint64_t getEffectExpiryMilliseconds(uint64_t start, float seconds, uint32_t dayMilliseconds) {
    return start + getEffectDurationMilliseconds(seconds, dayMilliseconds);
}

/** The most the ability increases on a creature add to one ability. */
constexpr int getAbilityEffectIncreaseCap(bool tsl) {
    return tsl ? 60 : 20;
}
/** The most the ability decreases on a creature take from one ability. */
constexpr int getAbilityEffectDecreaseCap(bool tsl) {
    return tsl ? 90 : 30;
}

/**
 * Whether a newly applied ability effect takes hold: never on a dead or
 * temporarily dead creature, never for nothing, and a decrease never on a
 * plot creature. An effect restored from a save passes both dead flags as
 * false: loading bypasses only those two gates.
 */
constexpr bool admitsAbilityEffect(bool dead, bool temporarilyDead, int amount, bool decrease, bool plot) {
    return !dead && !temporarilyDead && amount > 0 && !(decrease && plot);
}

/**
 * The rate a movement speed increase keeps: an amount up to 99 is a percentage
 * added to the normal rate, a larger one is the rate itself.
 */
constexpr int normalizeMovementSpeedIncrease(int percent) {
    return percent <= 99 ? percent + 100 : percent;
}

/** The factor a movement speed increase or decrease of \p payload percent gives. */
constexpr float getMovementSpeedMultiplier(bool increase, int payload) {
    return increase ? payload / 100.0f : 1.0f - payload / 100.0f;
}

/** A movement rate factor held between 0.125 and 1.5. */
constexpr float clampMovementRate(float rate) {
    return std::clamp(rate, 0.125f, 1.5f);
}

/**
 * The movement rate factor of a creature: its stored factor, raised by 0.1 in
 * TSL for a creature owning the mobility it applies.
 */
inline float getMovementRateFactor(float stored, bool tsl, bool applyMobility, bool ownsMobility) {
    return clampMovementRate(stored + (tsl && applyMobility && ownsMobility ? 0.1f : 0.0f));
}

class Object;
class Creature;
class Game;

using EffectId = uint64_t;

constexpr EffectId kUnassignedEffectId = 0;
constexpr uint32_t kSavedEffectInvalidObjectId = 0x7f000000;

/**
 * The category of an effect, with the values scripts read for it. A new
 * effect has no category; only magical effects can be dispelled.
 */
enum class EffectCategory {
    None = 0,
    Magical = 0x08,
    Supernatural = 0x10,
    Extraordinary = 0x18
};

/**
 * The type numbers effects are saved with. A creature keeps its effects sorted
 * by them, which is the order scripts walk them in.
 */
struct SavedEffectTypes {
    static constexpr uint16_t haste = 1;
    static constexpr uint16_t damageResistance = 2;
    static constexpr uint16_t slow = 3;
    static constexpr uint16_t resurrection = 4;
    static constexpr uint16_t disease = 5;
    static constexpr uint16_t regenerate = 7;
    static constexpr uint16_t setState = 8;
    static constexpr uint16_t setStateInternal = 9;
    static constexpr uint16_t attackIncrease = 10;
    static constexpr uint16_t attackDecrease = 11;
    static constexpr uint16_t damageReduction = 12;
    static constexpr uint16_t damageIncrease = 13;
    static constexpr uint16_t damageDecrease = 14;
    static constexpr uint16_t temporaryHitPoints = 15;
    static constexpr uint16_t damageImmunityIncrease = 16;
    static constexpr uint16_t damageImmunityDecrease = 17;
    static constexpr uint16_t entangle = 18;
    static constexpr uint16_t death = 19;
    static constexpr uint16_t knockdown = 20;
    static constexpr uint16_t deaf = 21;
    static constexpr uint16_t immunity = 22;
    static constexpr uint16_t setAIState = 23;
    static constexpr uint16_t enemyAttackBonus = 24;
    static constexpr uint16_t arcaneSpellFailure = 25;
    static constexpr uint16_t savingThrowIncrease = 26;
    static constexpr uint16_t savingThrowDecrease = 27;
    static constexpr uint16_t movementSpeedIncrease = 28;
    static constexpr uint16_t movementSpeedDecrease = 29;
    static constexpr uint16_t visualEffect = 30;
    static constexpr uint16_t areaOfEffect = 31;
    static constexpr uint16_t beam = 32;
    static constexpr uint16_t forceResistanceIncrease = 33;
    static constexpr uint16_t forceResistanceDecrease = 34;
    static constexpr uint16_t poison = 35;
    static constexpr uint16_t abilityIncrease = 36;
    static constexpr uint16_t abilityDecrease = 37;
    static constexpr uint16_t damage = 38;
    static constexpr uint16_t heal = 39;
    static constexpr uint16_t link = 40;
    static constexpr uint16_t hasteInternal = 41;
    static constexpr uint16_t slowInternal = 42;
    static constexpr uint16_t modifyAttacks = 44;
    static constexpr uint16_t curse = 45;
    static constexpr uint16_t silence = 46;
    static constexpr uint16_t invisibility = 47;
    static constexpr uint16_t acIncrease = 48;
    static constexpr uint16_t acDecrease = 49;
    static constexpr uint16_t spellImmunity = 50;
    static constexpr uint16_t dispelMagicAll = 51;
    static constexpr uint16_t dispelMagicBest = 52;
    static constexpr uint16_t light = 54;
    static constexpr uint16_t skillIncrease = 55;
    static constexpr uint16_t skillDecrease = 56;
    static constexpr uint16_t hitPointChangeWhenDying = 57;
    static constexpr uint16_t walkAnimation = 58;
    static constexpr uint16_t limitMovementSpeed = 59;
    static constexpr uint16_t forcePush = 60;
    static constexpr uint16_t damageShield = 61;
    static constexpr uint16_t disguise = 62;
    static constexpr uint16_t sanctuary = 63;
    static constexpr uint16_t timeStop = 64;
    static constexpr uint16_t spellLevelAbsorption = 65;
    static constexpr uint16_t icon = 67;
    static constexpr uint16_t vision = 69;
    static constexpr uint16_t seeInvisible = 70;
    static constexpr uint16_t ultravision = 71;
    static constexpr uint16_t trueSeeing = 72;
    static constexpr uint16_t blindness = 73;
    static constexpr uint16_t darkness = 74;
    static constexpr uint16_t missChance = 75;
    static constexpr uint16_t concealment = 76;
    static constexpr uint16_t negativeLevel = 82;
    static constexpr uint16_t bonusFeat = 83;
    static constexpr uint16_t wounding = 84;
    static constexpr uint16_t forceDrain = 90;
    static constexpr uint16_t temporaryForcePoints = 91;
    static constexpr uint16_t blasterDeflectionIncrease = 92;
    static constexpr uint16_t blasterDeflectionDecrease = 93;
    /** Has an effect immunity row (gameeffects.2da HORRIFIED) but no effect of its own. */
    static constexpr uint16_t horrified = 94;
    static constexpr uint16_t damageForcePoints = 95;
    static constexpr uint16_t healForcePoints = 96;
    /** Has an effect immunity row (gameeffects.2da CHOKE) but no effect of its own. */
    static constexpr uint16_t choke = 97;
    static constexpr uint16_t bodyFuel = 98;
    static constexpr uint16_t psychicStatic = 99;
    static constexpr uint16_t lightsaberThrow = 100;
    static constexpr uint16_t assuredHit = 101;
    static constexpr uint16_t forceJump = 102;
    static constexpr uint16_t forceJumpDelayed = 103;
    static constexpr uint16_t assuredDeflection = 104;
    static constexpr uint16_t forceResisted = 105;
    static constexpr uint16_t forceFizzle = 106;
    static constexpr uint16_t forceShield = 107;
    static constexpr uint16_t pureGoodPowers = 108;
    static constexpr uint16_t pureEvilPowers = 109;
    static constexpr uint16_t forceBody = 110;
    static constexpr uint16_t fury = 111;
    static constexpr uint16_t forcePointRegenerationModifier = 112;
    static constexpr uint16_t vitalityRegenerationModifier = 113;
    static constexpr uint16_t forceSight = 114;
    static constexpr uint16_t factionModifier = 115;
    static constexpr uint16_t destroyShields = 116;
    static constexpr uint16_t assassinate = 117;
};

enum class EffectIdImportResult {
    Unassigned,
    Imported,
    Existing,
};

enum class EffectExpiryOrigin {
    None,
    LoadedAbsoluteGameTime,
    RuntimeCountdown,
    RuntimeAbsoluteGameTime,
};

/** Runtime-session owner for the global exposed-effect identity namespace. */
class EffectIdNamespace {
public:
    static constexpr EffectId kFirstId = 1;

    EffectId allocate();
    EffectIdImportResult importId(EffectId id);
    bool setNextId(EffectId id);
    void reset();

    EffectId nextId() const { return _nextId; }
    bool contains(EffectId id) const { return _ids.count(id) != 0; }
    size_t size() const { return _ids.size(); }

private:
    EffectId _nextId {kFirstId};
    std::set<EffectId> _ids;
};

/** What applying an effect did, before the creature keeps it. */
enum class EffectApplicationResult {
    Rejected, /**< nothing happened and nothing is kept */
    Applied,  /**< the effect did its work once and is not kept */
    Retained, /**< the effect is kept on the object */
};

/** How the effect removal script commands match the effects they remove. */
enum class ScriptEffectRemovalMatch {
    PackageId,
    Integer0,
    TypeAndFirstTwoIntegers
};

/** How many effects of a linked package were kept and how many rejected. */
struct EffectPackageApplicationResult {
    size_t accepted {0};
    size_t rejected {0};

    bool partial() const { return accepted != 0 && rejected != 0; }
};

struct EffectInstance;

class Effect : public script::CopyableEngineType {
public:
    Effect(EffectType type) :
        _type(type) {
    }

    Effect(const Effect &) = default;

    std::shared_ptr<script::EngineType> cloneForScript() const override;
    uint64_t scriptValueId() const override { return _saveFacingId; }
    /** A copy of this effect as its own concrete type. */
    std::shared_ptr<Effect> cloneEffect() const {
        return std::static_pointer_cast<Effect>(cloneForScript());
    }
    /** Sets the identity the effect is known by to scripts and saves. */
    void setSaveFacingId(EffectId id) { _saveFacingId = id; }

    /** Applies the effect to \p object once, and says whether it is kept. */
    virtual EffectApplicationResult onApply(Object &object, EffectInstance &instance);
    /** Undoes the effect on \p object as it is removed. */
    virtual void onRemove(Object &object, const EffectInstance &instance);
    /**
     * Advances a kept effect on \p object by \p dt seconds. \p instance is the
     * kept record, which an update that changes the object's effects must no
     * longer read afterwards.
     */
    virtual void onUpdate(Object &object, const EffectInstance &instance, float dt);
    /**
     * Release executable payload owned by the outgoing Area lifetime.
     *
     * @param retainedObjectIds ids of the session objects that outlive the Area
     */
    virtual void retireAreaRuntime(
        const std::set<uint32_t> &retainedObjectIds) {}

    /**
     * Build the saved effect value, as a script's saved state carries it.
     *
     * Runtime effects retain executable C++ behavior, while this description
     * carries the save-facing fields and object bindings. Subclasses configure
     * their parameters through the protected helpers.
     */
    virtual EffectInstance saveFacingInstance() const;
    void setSaveFacingCreator(const std::shared_ptr<Object> &creator);
    /**
     * Makes the script caller the creator. An area of effect passes on its
     * own creator; a world object (not an area or the module) also gives the
     * spell it is running, otherwise the spell is kept.
     */
    void setCreatorFromCaller(const std::shared_ptr<Object> &caller);
    void setSaveFacingSpellId(int32_t spellId);
    /** Sets the category of the effect. */
    virtual void setCategory(EffectCategory category);
    /** The category of the effect: none, magical, supernatural or extraordinary. */
    virtual EffectCategory category() const { return _category; }
    /** Limits the effect to creatures of an alignment; false for an effect that cannot be. */
    virtual bool setVersusAlignment(int lawChaos, int goodEvil);
    /** Limits the effect to creatures of a racial type; false for an effect that cannot be. */
    virtual bool setVersusRacialType(int racialType);
    void captureSaveFacingScriptArguments(
        const std::vector<script::Variable> &arguments,
        const Game &game);

    EffectType type() const { return _type; }

protected:
    /** The saved integer at \p index; 0 when the effect has none there. */
    int32_t saveFacingInteger(size_t index) const {
        return index < _saveFacingIntegers.size() ? _saveFacingIntegers[index] : 0;
    }
    void setSaveFacingInteger(size_t index, int32_t value);
    void setSaveFacingFloat(size_t index, float value);
    void setSaveFacingString(size_t index, std::string value);
    void setSaveFacingObject(
        size_t index,
        const std::shared_ptr<Object> &object);

    EffectType _type;

private:
    EffectId _saveFacingId {kUnassignedEffectId};
    EffectCategory _category {EffectCategory::None};
    std::vector<int32_t> _saveFacingIntegers;
    std::array<float, 4> _saveFacingFloats {};
    std::array<std::string, 6> _saveFacingStrings {};
    RuntimeObjectRef<Object> _saveFacingCreator;
    /** The creator's id, kept after the creator is gone. */
    uint32_t _saveFacingCreatorId {kSavedEffectInvalidObjectId};
    std::array<RuntimeObjectRef<Object>, 4> _saveFacingObjects;
    uint32_t _saveFacingSpellId {std::numeric_limits<uint32_t>::max()};
    bool _saveFacingRepresentable {true};
};

/** An effect copied as its concrete type T when a script copies it. */
template <typename T>
class CopyableEffect : public Effect {
public:
    using Effect::Effect;
    std::shared_ptr<script::EngineType> cloneForScript() const override {
        return std::make_shared<T>(static_cast<const T &>(*this));
    }
};

/**
 * Semantic state of one applied effect.
 *
 * The executable Effect is optional: saved records whose behavior has no
 * executable form still remain typed, queryable and removable without losing
 * their saved identity or payload.
 */
struct EffectInstance {
    std::shared_ptr<Effect> effect;
    EffectId id {kUnassignedEffectId};
    /** The order effects were applied to their object in. */
    uint64_t applicationOrder {0};
    /** Whether the effect is being applied from a save rather than anew; not saved. */
    bool restoring {false};
    uint16_t serializedType {0};
    float duration {0.0f};
    /**
     * Derived observation of the canonical expiry day/time. Pending values use
     * RuntimeCountdown until admission assigns their absolute world deadline.
     * The saved duration stays as the effect was given it, or as its
     * application adjusted it.
     */
    std::optional<float> remainingDuration;
    uint32_t expiryDay {0};
    uint32_t expiryTime {0};
    EffectExpiryOrigin expiryOrigin {EffectExpiryOrigin::None};
    uint32_t creatorId {0};
    uint32_t spellId {std::numeric_limits<uint32_t>::max()};
    int32_t exposed {0};
    bool skipOnLoad {false};
    std::vector<int32_t> integerParameters;
    std::array<float, 4> floatParameters {};
    std::array<std::string, 6> stringParameters {};
    std::array<uint32_t, 4> objectParameters {
        kSavedEffectInvalidObjectId,
        kSavedEffectInvalidObjectId,
        kSavedEffectInvalidObjectId,
        kSavedEffectInvalidObjectId,
    };
    RuntimeObjectRef<Object> creator;
    std::array<RuntimeObjectRef<Object>, 4> objectParameterObjects;
    std::optional<SerializedIdentityContext> serializedReferenceContext;

    static EffectInstance fromGff(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    DurationType durationType() const { return _durationType; }
    /** Sets the duration type and the duration in seconds of a temporary effect. */
    void setDuration(DurationType type, float seconds);
    void setIntegerParameter(size_t index, int32_t value);
    /** Gives a saved record its executable effect, if it has none. */
    void materialize();
    /** The effect as a saved record. */
    std::shared_ptr<resource::Gff> toGff() const;
    /** A record its kept root rebuilds whenever applied, not one a script linked: hidden from scripts and not restored from a save. */
    void markGeneratedForLoad() {
        exposed = 0;
        skipOnLoad = true;
    }
    /** New executable payload under this existing package and bound source. */
    EffectInstance linkedChild(const std::shared_ptr<Effect> &child) const;
    EffectType type() const;
    /** The EFFECT_TYPE_* constant scripts see for this record; 0 when it has none. */
    int scriptEffectType(bool tsl) const;
    int32_t integerParameter(size_t index, int32_t defaultValue = 0) const;
    /** The category of the effect. */
    EffectCategory category() const { return _category; }
    /** Sets the category of the effect. */
    void setCategory(EffectCategory category) { _category = category; }
    /** Sets the duration type, leaving the duration as it is. */
    void setDurationType(DurationType type) { _durationType = type; }
    bool hasStableId() const { return id != kUnassignedEffectId; }
    bool hasSerializedObjectReferences() const {
        return serializedReferenceContext.has_value();
    }
    /** Whether loading a save applies the effect again. */
    bool shouldRestoreOnLoad() const;
    /** Whether scripts see the effect when they walk an object's effects. */
    bool isScriptEnumerable() const {
        auto lifetime = durationType();
        return exposed != 0 && serializedType != SavedEffectTypes::icon &&
               (lifetime == DurationType::Temporary || lifetime == DurationType::Permanent);
    }
    bool hasSerializableTemporalProvenance() const {
        return durationType() != DurationType::Temporary ||
               expiryOrigin != EffectExpiryOrigin::None;
    }

    std::shared_ptr<Object> boundCreator() const;
    std::shared_ptr<Object> boundObjectParameter(size_t index) const;
    /**
     * Whether an effect limited to a racial type or alignment applies against
     * \p creature; an effect without a limit applies against no creature too.
     */
    bool appliesVersus(std::optional<std::reference_wrapper<const Creature>> creature) const;
    /** Equipped effects stop contributing as soon as their exact Item retires. */
    bool hasLiveRuntimeSource() const;

    /**
     * Rebase live object bindings at an Area lifetime boundary.
     *
     * Effects are durable gameplay state, but their object identities are not:
     * a saved-graph identity and a module-owned runtime object both retire with
     * the outgoing Area. Only bindings to explicitly retained session objects
     * survive, rewritten as runtime-session identities.
     *
     * @param retainedObjectIds ids of the session objects that outlive the Area
     */
    void retireAreaRuntimeBindings(
        const std::set<uint32_t> &retainedObjectIds);

private:
    friend class Game;
    DurationType _durationType {DurationType::Instant};
    EffectCategory _category {EffectCategory::None};
    std::optional<uint64_t> _runtimeSession;
    bool bindCreator(const std::shared_ptr<Object> &object);
    bool bindObjectParameter(size_t index, const std::shared_ptr<Object> &object);
};

/**
 * A serialized EffectInstance used as an NWScript engine value.
 *
 * This deliberately reuses the save-facing effect model. Applying it through
 * Object::applyEffect preserves that semantic payload instead of inventing a
 * second, save-only effect representation.
 */
class SavedEffectValue : public CopyableEffect<SavedEffectValue> {
public:
    explicit SavedEffectValue(EffectInstance instance);
    const EffectInstance &instance() const { return _instance; }
    uint64_t scriptValueId() const override { return _instance.id; }
    EffectInstance saveFacingInstance() const override { return _instance; }
    void setCategory(EffectCategory category) override;
    EffectCategory category() const override { return _instance.category(); }
    bool setVersusAlignment(int lawChaos, int goodEvil) override;
    bool setVersusRacialType(int racialType) override;

private:
    EffectInstance _instance;
};

} // namespace game

} // namespace reone
