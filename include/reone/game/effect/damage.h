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

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

#include "../effect.h"

namespace reone {

namespace game {

/**
 * The damage event slot of a damage type a packet carries: the power of two of
 * its highest flag. No flags take slot 0.
 */
inline std::size_t getDirectDamageSlot(int flags) {
    if (flags <= 0) {
        return 0;
    }
    std::size_t slot = 0;
    for (unsigned remaining = static_cast<unsigned>(flags); remaining > 1; remaining >>= 1) {
        ++slot;
    }
    return slot;
}

/**
 * The damage event slot of the damage flags a script names (0 to 8192): the
 * base 2 logarithm of the flags, rounded in single precision, so that
 * combined flags take a slot of their own. No flags take slot 0.
 */
inline std::size_t getScriptDamageSlot(int flags) {
    if (flags <= 0) {
        return 0;
    }
    // Keep each intermediate in single precision and prevent reassociation.
    volatile float logarithm = std::log10(static_cast<float>(flags));
    volatile float scaled = logarithm * 0x1.a934f0p+1f; // single-precision multiplier
    volatile float shifted = scaled + 0.5f;
    return static_cast<std::size_t>(static_cast<int>(shifted)); // truncate toward zero
}

/** Every damage type flag. */
inline constexpr int kAllDamageTypeFlags = 0x3fff;
/** The damage type flags that count as physical. */
inline constexpr int kPhysicalDamageTypeFlags = 0x4007;

/** The highest damage type among \p damageFlags, which must not be empty. */
DamageType getPrimaryDamageType(int damageFlags);
/** Whether a modifier for \p modifierFlags applies to damage of \p damageFlags. */
bool damageTypeMatches(int modifierFlags, int damageFlags);

/**
 * The part of a typed damage bonus from \p source that would pass the
 * target's immunity and resistance, leaving every pool untouched.
 */
int simulateDamageBonusMitigation(Object &object, const Creature &source, int damageFlags, int damage);

/**
 * Stable identifiers for mitigation-feedback payloads.
 *
 * These IDs are independent of external feedback-message identifiers.
 */
enum class MitigationFeedbackType {
    DamageImmunity = 0,
    DamageResistance = 1,
    DamageReduction = 2,
    FiniteDamageResistance = 3,
    FiniteDamageReduction = 4,
};

/**
 * Semantic payload consumed by the mitigation-feedback presentation.
 *
 * Every type carries an amount. Finite resistance and reduction also carry
 * the remaining pool, while immunity uses damageFlags to select its label.
 */
struct MitigationFeedback {
    /** Which mitigation the line reports. */
    MitigationFeedbackType type;
    /** The damage it prevented. */
    int amount {0};
    /** What is left of a finite pool. */
    std::optional<int> remaining;
    /** The damage flags whose immunity the line names. */
    int damageFlags {0};
};

/**
 * Values produced while resolving one damage packet against its target.
 *
 * This is an observational record of the existing mitigation calculation.
 * It retains both post-mutation pool state and producer-time feedback values.
 */
struct DamageResolution {
    int damageFlags {0};
    DamagePower damagePower {DamagePower::Normal};
    int rawDamage {0};
    bool plotSuppressed {false};

    int immunityPercent {0};
    int immunityPrevented {0};
    int vulnerabilityAdded {0};
    int damageAfterImmunity {0};

    int resistanceAmount {0};
    int resistancePrevented {0};
    int resistanceFeatPrevented {0};
    int percentageResistanceBonus {0};
    int improvedToughnessBonus {0};
    int wookieeEnduranceBonus {0};
    std::optional<int> resistancePoolRemaining;
    int damageAfterResistance {0};

    int reductionAmount {0};
    DamagePower reductionPower {DamagePower::Normal};
    bool reductionBypassed {false};
    int reductionPrevented {0};
    std::optional<int> reductionPoolRemaining;

    std::vector<MitigationFeedback> mitigationFeedback;
    int finalDamage {0};
    std::array<int, 15> damageAmounts = [] {
        std::array<int, 15> values;
        values.fill(-1);
        return values;
    }();
};

/**
 * Typed damage caused by one hit.
 *
 * A packet may contain multiple damage types, but it is applied to the target
 * as one damage event.
 */
class DamagePacket {
public:
    explicit DamagePacket(DamagePower power = DamagePower::Normal) :
        _power(power) {
    }

    /** Adds \p amount of \p type: a component of that type changes by it, to no less than 1; a new one must be positive. */
    void add(int amount, DamageType type);
    /**
     * Adds to the base damage. Base adjustments may be negative; the base is
     * raised to its minimum only when the packet is resolved.
     */
    void addPhysicalBase(int amount, DamageType type);
    /** Adds a typed bonus; increases and decreases of a type are kept apart. */
    void addBonus(int amount, DamageType type);
    /** Sets the amounts the typed slots keep when the packet is taken unmitigated. */
    void setBaseDamageAmounts(const std::array<int, 14> &amounts) { _baseDamageAmounts = amounts; }
    /** Sets the damage flags the packet's mitigation is looked up by. */
    void setDamageFlags(int damageFlags);
    /** Raises the packet's damage power; it is never lowered. */
    void setPower(DamagePower power);
    /**
     * Resolves damage that is not a weapon hit: the target's immunity, its
     * resistance, counting a Power Attack of \p source, and, for physical
     * damage, its reduction.
     */
    void resolve(Object &object, std::optional<std::reference_wrapper<const Creature>> source = std::nullopt);
    /**
     * Resolves a weapon hit by \p source: each typed bonus meets the target's
     * immunity and resistance, then the base damage its immunity, resistance
     * and reduction. A simulated resolution leaves the target's resistance and
     * reduction pools untouched.
     */
    void resolvePhysical(Object &object, const Creature &source, bool simulate = false);
    /**
     * Resolve a deflected bolt's packet against its own shooter. The typed
     * slots keep their rolled amounts; the total meets the shooter's damage
     * resistance and then immunity for the packet's damage flags, without
     * damage reduction, and replaces the flag slot and the total.
     */
    void resolveReturned(Creature &shooter);
    /**
     * Resolves a thrown lightsaber's damage: a creature meets it with its
     * resistance and then its immunity; a plot object takes none.
     */
    void resolveLightsaberThrow(Object &object, const Creature &caster);
    /**
     * Take the packet as it stands, without immunity, resistance or
     * reduction: the typed slots keep their base amounts and the total is the
     * base damage. A plot target still takes nothing when the damage applies.
     */
    void resolveUnmitigated();

    /** The sum of the packet's components before mitigation. */
    int total() const;
    int damageFlags() const { return _damageFlags; }
    DamagePower power() const { return _power; }
    /** The weapon cuts doors: a door or placeable takes its maximum hit points as base damage. */
    void setCutsDoors(bool cuts) { _cutsDoors = cuts; }
    /** The packet as saved with an attack still under way. */
    std::shared_ptr<resource::Gff> saveContinuation() const;
    /** The packet saved as \p record with an attack still under way. */
    static DamagePacket restoreContinuation(const resource::Gff &record);
    /** The damage left after mitigation; the packet must be resolved. */
    int resolvedDamage() const;
    /** How the packet was resolved; the packet must be resolved. */
    const DamageResolution &resolution() const;
    bool empty() const { return _components.empty(); }
    bool isResolved() const { return _resolution.has_value(); }

private:
    struct Component {
        int amount;
        DamageType type;
        bool bonus {false};
    };

    void requireUnresolved() const;
    void addResolved(int amount, DamageType type, bool bonus = false);

    DamagePower _power;
    int _damageFlags {0};
    bool _cutsDoors {false};
    std::vector<Component> _components;
    std::optional<DamageResolution> _resolution;
    std::array<int, 14> _baseDamageAmounts = [] {
        std::array<int, 14> values;
        values.fill(-1);
        return values;
    }();
};

/**
 * The amount of a single damage type in the damage event slots; an absent
 * slot is -1, and combined flags give 0.
 */
inline int getDamageAmountByType(const std::array<int, 15> &amounts, int flags) {
    if (flags <= 0 || (flags & (flags - 1)) != 0) {
        return 0;
    }
    size_t slot = 0;
    for (; flags > 1; flags >>= 1) {
        ++slot;
    }
    return slot < amounts.size() ? amounts[slot] : 0;
}

/** The sum of the damage event slots that hold damage. */
inline int getTotalDamageAmounts(const std::array<int, 15> &amounts) {
    int result = 0;
    for (int amount : amounts) {
        if (amount > 0) {
            result += amount;
        }
    }
    return result;
}

class Creature;

/**
 * Reports at once the mitigation lines of damage resolved outside a weapon
 * hit, to the damaged object and to the creature that caused it, if any.
 */
void showMitigationFeedback(Object &object, const std::shared_ptr<Creature> &source,
                            const DamageResolution &resolution);

/**
 * Damage, in fifteen damage event slots followed by the total, the reaction
 * delay, the damage flags and power, and whether the amounts are already
 * resolved. Damage that is not resolved meets the target's mitigation when it
 * is applied.
 */
class DamageEffect : public CopyableEffect<DamageEffect> {
public:
    /** The damage event slots, one per damage type. */
    static constexpr size_t kSlotCount = 15;
    /** The integer holding the total. */
    static constexpr size_t kTotal = 14;
    /** The integer holding the reaction delay, in milliseconds. */
    static constexpr size_t kReactionDelay = 16;
    /** The integer holding the damage flags. */
    static constexpr size_t kDamageFlags = 17;
    /** The integer holding the damage power. */
    static constexpr size_t kPower = 18;
    /** The integer that is 1 when the amounts are already resolved. */
    static constexpr size_t kPreResolved = 19;
    /** The integer that is 1 when the damage sets off no damage shield. */
    static constexpr size_t kSuppressShields = 20;
    /** The integer that is 1 when the damage's feedback is already shown. */
    static constexpr size_t kFeedbackHandled = 21;

    /** The reaction delay, in milliseconds, of damage that does not name one. */
    static constexpr int kDefaultReactionDelay = 1000;

    /** What a resolved packet's damage carries besides its amounts. */
    struct ApplicationContext {
        /** The amount of a slot the damage does not fill. */
        static constexpr int kAbsentDamageAmount = -1;

        ApplicationContext() {
            // DamageType::Universal uses slot 3; -1 marks an unpopulated slot.
            damageAmounts.fill(kAbsentDamageAmount);
        }

        /** The amount in each damage event slot. */
        std::array<int, kSlotCount> damageAmounts;
        /** The damage sets off no damage shield. */
        bool suppressDamageShields {false};
        /** The damage's feedback is already shown. */
        bool feedbackHandled {false};
    };

    /** Damage of one type, in the slot of that type. */
    DamageEffect(int amount, DamageType type, DamagePower power,
                 bool feedbackHandled = false) :
        DamageEffect(amount, static_cast<int>(type), power,
                     getDirectDamageSlot(static_cast<int>(type)), feedbackHandled) {}

    /** Damage of the flags a script names, which may combine types, in the slot those flags take. */
    DamageEffect(int amount, int damageFlags, DamagePower power) :
        DamageEffect(amount, damageFlags, power,
                     getScriptDamageSlot(damageFlags), false) {}

    /** The damage of a resolved packet, with the slots and flags of \p context. */
    DamageEffect(DamagePacket damage, ApplicationContext context) :
        CopyableEffect(EffectType::Damage) {
        assert(damage.isResolved() && "Damage packet has not been resolved");
        for (size_t i = 0; i < context.damageAmounts.size(); ++i) {
            setSaveFacingInteger(i, context.damageAmounts[i]);
        }
        const auto &resolution = damage.resolution();
        setSaveFacingInteger(kTotal, resolution.finalDamage);
        setSaveFacingInteger(kReactionDelay, kDefaultReactionDelay);
        setSaveFacingInteger(kDamageFlags, resolution.damageFlags);
        setSaveFacingInteger(kPower, static_cast<int>(resolution.damagePower));
        setSaveFacingInteger(kPreResolved, 1);
        setSaveFacingInteger(kSuppressShields, context.suppressDamageShields);
        if (context.feedbackHandled) {
            setSaveFacingInteger(kFeedbackHandled, 1);
        }
    }

    /** The damage of a resolved packet, with no slot filled. */
    explicit DamageEffect(DamagePacket damage) :
        DamageEffect(std::move(damage), ApplicationContext()) {}

    /** The damage a saved record holds. */
    explicit DamageEffect(const EffectInstance &instance) :
        CopyableEffect(EffectType::Damage) {
        for (size_t i = 0; i < instance.integerParameters.size(); ++i) {
            setSaveFacingInteger(i, instance.integerParameters[i]);
        }
    }

    /** Damage already resolved, of \p amount in \p slot, with no reaction. */
    static std::shared_ptr<DamageEffect> fromResolvedAttackAmount(int amount, size_t slot);
    /**
     * A damage shield's retaliation: only the shield's typed slot holds the
     * amount, and the total stays absent, so it resolves to no damage.
     */
    static std::shared_ptr<DamageEffect> fromShieldRetaliation(int amount, int shieldFlags);

    EffectApplicationResult onApply(Object &object, EffectInstance &instance) override;

private:
    DamageEffect(int amount, int flags, DamagePower power,
                 size_t slot, bool feedbackHandled) :
        CopyableEffect(EffectType::Damage) {
        for (size_t i = 0; i < kSlotCount; ++i) {
            setSaveFacingInteger(i, ApplicationContext::kAbsentDamageAmount);
        }
        setSaveFacingInteger(slot, amount);
        setSaveFacingInteger(kTotal, amount);
        setSaveFacingInteger(kReactionDelay, kDefaultReactionDelay);
        setSaveFacingInteger(kDamageFlags, flags);
        setSaveFacingInteger(kPower, static_cast<int>(power));
        setSaveFacingInteger(kPreResolved, 0);
        setSaveFacingInteger(kSuppressShields, 0);
        if (feedbackHandled) {
            setSaveFacingInteger(kFeedbackHandled, 1);
        }
    }
};

/** An application result, never stored back into a reusable effect descriptor. */
struct DamageEffectApplication {
    /** The damage dealt. */
    int amount {0};
    /** The amount in each damage event slot, then the total. */
    std::array<int, 15> damageAmounts {};
    /** The amounts were resolved before the damage was applied. */
    bool preResolved {false};
    /** The damage sets off no damage shield. */
    bool suppressDamageShields {false};
};

/**
 * The damage an application of \p instance deals: its slots, with a plot
 * target's zeroed, and its total, resolved through \p resolve unless it is
 * already resolved. None when there is no damage to a target that is not plot.
 */
template <typename Resolve>
std::optional<DamageEffectApplication> resolveDamageEffect(
    const EffectInstance &instance, bool plot, Resolve &&resolve) {
    DamageEffectApplication result;
    for (size_t i = 0; i < result.damageAmounts.size(); ++i) {
        const int value = instance.integerParameter(i);
        result.damageAmounts[i] = plot && value >= 0 ? 0 : value;
    }
    result.amount = result.damageAmounts[DamageEffect::kTotal];
    // Damage that mitigation reduces to nothing is still applied.
    if (result.amount == 0 && !plot) {
        return std::nullopt;
    }

    result.preResolved = instance.integerParameter(DamageEffect::kPreResolved) != 0;
    result.suppressDamageShields = instance.integerParameter(DamageEffect::kSuppressShields) != 0;
    if (!result.preResolved) {
        result.amount = resolve(result.amount,
                                instance.integerParameter(DamageEffect::kDamageFlags));
        // Every slot the damage fills shows the resolved amount.
        for (int &value : result.damageAmounts) {
            if (value >= 0) {
                value = result.amount;
            }
        }
    }
    return result;
}

} // namespace game

} // namespace reone
