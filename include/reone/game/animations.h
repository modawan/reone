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

#include <functional>
#include <optional>
#include <unordered_map>

#include "reone/game/types.h"

namespace reone {

namespace resource {

class ITwoDAs;
class TwoDA;

} // namespace resource

namespace game {

class IAnimations {
public:
    virtual ~IAnimations() = default;
    virtual void clear() = 0;

    virtual std::string getNameById(uint32_t id) const = 0;
    /** The target's reaction clip to an attack swing: its dodge, parry or damage clip. */
    virtual std::string getReactionAnimation(const std::string &attackAnim, CreatureWieldType targetWield, AttackReaction reaction) const = 0;
    virtual int getMeleeImpactTime(const std::string &attackAnim, size_t attackIndex) const = 0;
    /**
     * The named row is an overlay: queued, it plays as a layer over the loop
     * and is taken off when its time runs out.
     */
    virtual bool isOverlay(const std::string &) const { return false; }
    /** The row loops. */
    virtual bool isLoopingById(uint32_t) const { return false; }
    /** The row plays once and then gives way to the loop. */
    virtual bool isFireForgetById(uint32_t) const { return false; }
    /** The named row is a parry. */
    virtual bool isParry(const std::string &) const { return false; }
    /** The named row puts the equipped items away while it plays. */
    virtual bool hidesEquippedItems(const std::string &) const { return false; }
};

class Animations : public IAnimations {
public:
    Animations(resource::ITwoDAs &twoDas) :
        _twoDas(twoDas) {}

    void init();
    void clear() override;

    std::string getNameById(uint32_t id) const override;
    std::string getReactionAnimation(const std::string &attackAnim, CreatureWieldType targetWield, AttackReaction reaction) const override;
    int getMeleeImpactTime(const std::string &attackAnim, size_t attackIndex) const override;
    bool isOverlay(const std::string &name) const override;
    bool isLoopingById(uint32_t id) const override;
    bool isFireForgetById(uint32_t id) const override;
    bool isParry(const std::string &name) const override;
    bool hidesEquippedItems(const std::string &name) const override;

private:
    struct Anim {
        std::string name;
        bool attack {false};
        bool overlay {false};
        bool looping {false};
        bool fireForget {false};
        bool parry {false};
        bool hidesEquippedItems {false};
    };

    static constexpr uint32_t kNoAnim = std::numeric_limits<uint32_t>::max();

    struct AttackResult {
        uint32_t parry {kNoAnim};
        uint32_t dodge {kNoAnim};
        uint32_t damage {kNoAnim};
    };

    void parseAnims(resource::TwoDA &animDa);
    /** The first row of the name, in any case. */
    std::optional<std::reference_wrapper<const Anim>> findByName(const std::string &name) const;
    void parseCombatAnim(resource::TwoDA &combatAnimDa);

    using AttackResultMap = std::map<std::pair<std::string, CreatureWieldType>, AttackResult>;

    resource::ITwoDAs &_twoDas;
    std::vector<Anim> _anims;
    /** The first row of each lower-case name. */
    std::unordered_map<std::string, size_t> _animIndexByName;
    AttackResultMap _attackResults;
    std::map<std::string, std::vector<int>> _meleeImpactTimes;
};

} // namespace game

} // namespace reone
