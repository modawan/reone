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

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "reone/game/savedruntime.h"
#include "reone/game/types.h"

namespace reone {

namespace resource {

class TwoDAs;
class TwoDA;

} // namespace resource

namespace game {

class Creature;
struct Spell;
class Object;
class Item;
class Game;
struct ServicesView;
struct EffectInstance;

/**
 * One weapondischarge.2da row: the discharge times of an attack animation, in
 * milliseconds from its start, and the hand of each discharge (0 right, 1 left).
 */
struct ProjectileSpec {
    struct Shot {
        int timeMilliseconds {0};
        int hand {0};
    };
    std::vector<Shot> shots;
    /**
     * The hits column. The physical attack round takes its hit shots from the
     * attack count; only the cutscene presentation sequence reads this value.
     */
    int hits {0};
};

/**
 * One discharge of a ranged attack: the bolt leaves \p hand (0 right bullet
 * hook, 1 left, 2 the source's impact node) and reaches \p endpoint after
 * \p delayMilliseconds. \p special selects the power-blast shot sound.
 */
struct SafeProjectileShot {
    AttackResultType result {AttackResultType::Miss};
    glm::vec3 endpoint {0.0f};
    uint32_t delayMilliseconds {0};
    int hand {0};
    bool special {false};
    uint32_t missedByMilliseconds {0};
};

/**
 * The projectiles in flight: spell projectiles, a thrown lightsaber's route
 * and the bolts of ranged attacks. They are presentation only; the hits they
 * show are queued as events.
 */
class IProjectiles {
public:
    virtual ~IProjectiles() = default;
    virtual void clear() = 0;
    /** Sends the caster's lightsaber along the targets of \p owner, queueing its hits. */
    virtual void launchLightsaberThrow(Creature &caster, const EffectInstance &owner, Game &game, ServicesView &services) = 0;
    virtual void update(float dt, Game &game, ServicesView &services) = 0;
    /** Removes every projectile, as the area they fly in goes. */
    virtual void retireAreaRuntime() = 0;
    // Presentation hooks remain optional for headless providers.
    /**
     * Shows a spell's projectile at its caster while the cast is conjured,
     * aimed at \p target, or at \p position when there is none.
     *
     * @return the presentation's ID, for releaseSpell and cancelSpell; 0 when
     *         the spell has no projectile
     */
    virtual uint64_t beginSpell(Object &caster, std::optional<std::reference_wrapper<const Object>> target,
                                const glm::vec3 &position, const Spell &spell, ProjectilePathType path,
                                Game &game, ServicesView &services) { return 0; }
    /** Sends a conjured spell projectile off; it arrives after \p duration seconds. */
    virtual void releaseSpell(uint64_t id, float duration, Game &game, ServicesView &services) {}
    /** Removes a spell projectile that has not been released. */
    virtual void cancelSpell(uint64_t id) {}
    /** A thrown lightsaber keeps the creature from parrying ranged attacks. */
    virtual bool blocksRangedParry(const Creature &creature) const { return false; }
    /** The creature's thrown lightsaber now in flight is taken away. */
    virtual void dropThrownLightsaber(const Creature &creature) {}
    /** Shows one discharge of \p weapon from \p source at \p target. */
    virtual void launchSafeProjectile(Creature &source, Object &target, const Item &weapon,
                                      const SafeProjectileShot &shot, Game &game, ServicesView &services) {}
    virtual std::vector<SavedProjectile> savePresentations() const { return {}; }
    virtual void restorePresentations(std::vector<SavedProjectile> records, Game &game, ServicesView &services) {}
    /** Discharge row of \p animation (an animations.2da index) for \p attacker. */
    virtual std::optional<ProjectileSpec> discharge(int animation, const Creature &attacker) const = 0;
};

/**
 * animations.2da index of the physical ranged attack animation for a combat
 * attack type (feat) and wield type. Creature-model attackers use their own row.
 */
int rangedAttackAnimation(int attackType, CreatureWieldType wield, bool creatureModel);

class Projectiles : public IProjectiles {
public:
    Projectiles(resource::TwoDAs &twoDas) :
        _twoDas(twoDas) {}

    void init();
    void clear() override;
    void launchLightsaberThrow(Creature &, const EffectInstance &, Game &, ServicesView &) override;
    void update(float dt, Game &, ServicesView &) override;
    void retireAreaRuntime() override;
    uint64_t beginSpell(Object &caster, std::optional<std::reference_wrapper<const Object>> target,
                        const glm::vec3 &position, const Spell &spell, ProjectilePathType path,
                        Game &game, ServicesView &services) override;
    void releaseSpell(uint64_t, float, Game &, ServicesView &) override;
    void cancelSpell(uint64_t) override;
    bool blocksRangedParry(const Creature &) const override;
    void dropThrownLightsaber(const Creature &) override;
    void launchSafeProjectile(Creature &, Object &, const Item &,
                              const SafeProjectileShot &, Game &, ServicesView &) override;
    std::vector<SavedProjectile> savePresentations() const override;
    void restorePresentations(std::vector<SavedProjectile>, Game &, ServicesView &) override;

    std::optional<ProjectileSpec> discharge(int animation, const Creature &attacker) const override;

private:
    struct ActiveProjectile;

    /** A weapondischarge.2da row; a row without shots has no spec. */
    struct Discharge {
        bool droid {false};
        std::optional<ProjectileSpec> spec;
    };

    resource::TwoDAs &_twoDas;

    std::vector<std::shared_ptr<ActiveProjectile>> _active;
    uint64_t _nextPresentationId {1};
    /** Successive burst projectiles set out to alternate sides. */
    bool _burstWentLeft {false};
    /** weapondischarge.2da by lower-case label. */
    std::unordered_map<std::string, Discharge> _discharges;
    /** droiddischarge.2da: race, then animation label, to label prefix. */
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _droidPrefixes;
    /** grenadesnd.2da sound of each row, lower case; empty when the row has none. */
    std::vector<std::string> _grenadeSounds;

    void attachPresentation(ActiveProjectile &active, Game &game, ServicesView &services, bool restoring);
    void startLeg(ActiveProjectile &active, Game &game, ServicesView &services, bool restoring = false);
};

} // namespace game

} // namespace reone
