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
#include <cstdint>
#include <optional>
#include <stdexcept>

#include "reone/system/smallvector.h"

#include "../effect.h"

namespace reone {

namespace game {

inline constexpr int kAllDamageTypeFlags = 8199;
inline constexpr int kPhysicalDamageTypeFlags = 16391;

DamageType getPrimaryDamageType(int damageFlags);
bool damageTypeMatches(int modifierFlags, int damageFlags);

enum class MitigationFeedbackType : uint16_t {
    DamageImmunity = 0x3e,
    DamageResistance = 0x3f,
    DamageReduction = 0x40,
    FiniteDamageResistance = 0x42,
    FiniteDamageReduction = 0x43,
};

struct MitigationFeedback {
    MitigationFeedbackType type;
    int amount {0};
    std::optional<int> remaining;
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
    int improvedToughnessBonus {0};
    int wookieeEnduranceBonus {0};
    std::optional<int> resistancePoolRemaining;
    int damageAfterResistance {0};

    int reductionAmount {0};
    DamagePower reductionPower {DamagePower::Normal};
    bool reductionBypassed {false};
    int reductionPrevented {0};
    std::optional<int> reductionPoolRemaining;

    SmallVector<MitigationFeedback, 3> mitigationFeedback;
    int finalDamage {0};
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

    void add(int amount, DamageType type);
    void setDamageFlags(int damageFlags);
    void setPower(DamagePower power);
    void resolve(Object &object);

    int total() const;
    int resolvedDamage() const;
    const DamageResolution &resolution() const;
    bool empty() const { return _components.empty(); }
    bool isResolved() const { return _resolution.has_value(); }

private:
    struct Component {
        int amount;
        DamageType type;
    };

    void requireUnresolved() const;
    void addResolved(int amount, DamageType type);

    DamagePower _power;
    int _damageFlags {0};
    SmallVector<Component, 4> _components;
    std::optional<DamageResolution> _resolution;
};

class DamageEffect : public Effect {
public:
    struct ApplicationContext {
        static constexpr int kAbsentDamageAmount = -1;

        ApplicationContext() {
            // DamageType::Universal uses slot 3; -1 marks an unpopulated slot.
            damageAmounts.fill(kAbsentDamageAmount);
        }

        std::array<int, 15> damageAmounts;
        bool preResolved {false};
        bool suppressDamageShields {false};
        bool feedbackHandled {false};
    };

    DamageEffect(int amount,
                 DamageType type,
                 DamagePower power,
                 uint32_t damager) :
        Effect(EffectType::Damage),
        _damage(power),
        _damager(damager) {
        _damage.add(amount, type);
        _damage.setDamageFlags(static_cast<int>(type));

        auto slot = getDamageAmountSlot(type);
        if (slot && *slot < static_cast<int>(_context.damageAmounts.size())) {
            _context.damageAmounts[*slot] = amount;
        }
    }

    DamageEffect(
        DamagePacket damage,
        uint32_t damager,
        ApplicationContext context) :
        Effect(EffectType::Damage),
        _damage(std::move(damage)),
        _damager(damager),
        _context(std::move(context)) {
        if (!_damage.isResolved()) {
            throw std::invalid_argument("Damage packet has not been resolved");
        }
    }

    bool onApply(Object &object) override;

    uint32_t damager() const { return _damager; }

private:
    static std::optional<int> getDamageAmountSlot(DamageType type) {
        int flags = static_cast<int>(type);
        if (flags <= 0 || (flags & (flags - 1)) != 0) {
            return std::nullopt;
        }

        int slot = 0;
        while (flags > 1) {
            flags >>= 1;
            ++slot;
        }
        return slot;
    }

    DamagePacket _damage;
    uint32_t _damager;
    ApplicationContext _context;
};

} // namespace game

} // namespace reone
