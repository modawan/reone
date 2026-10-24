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
#include <string>
#include <unordered_map>
#include <vector>

#include "reone/game/types.h"

namespace reone {

namespace resource {

class ITwoDAs;
class TwoDA;

} // namespace resource

namespace game {

/** A dialoganimations.2da row: the pose's name and whether it is a dialog animation. */
struct DialogAnimation {
    /** The lower-case name of the pose. */
    std::string name;
    /** Whether the row is a looping or fire-and-forget dialog animation. */
    bool dialog {false};
    /** The pose loops. */
    bool looping {false};
    /** The pose plays once; a row may both loop and play once. */
    bool fireForget {false};
    /** How far a face shot of a speaker in this pose pulls back, in metres. */
    float pullBack {0.0f};
};

class IAnimations {
public:
    virtual ~IAnimations() = default;
    virtual void clear() = 0;

    virtual std::string getNameById(uint32_t id) const = 0;
    /**
     * The target's reaction clip to an attack swing: its dodge, parry or damage
     * clip. A swing without a row, or a row with no clip for the reaction and
     * wield, gives the first row (walk).
     */
    virtual std::string getReactionAnimation(const std::string &attackAnim, CreatureWieldType targetWield, AttackReaction reaction) const = 0;
    virtual int getMeleeImpactTime(const std::string &attackAnim, size_t attackIndex) const = 0;
    /**
     * The named row is an overlay: queued, it plays as a layer over the loop
     * and is taken off when its time runs out.
     */
    virtual bool isOverlay(const std::string &) const = 0;
    /** The row loops. */
    virtual bool isLoopingById(uint32_t) const = 0;
    /** The row plays once and then gives way to the loop. */
    virtual bool isFireForgetById(uint32_t) const = 0;
    /** The named row is a parry. */
    virtual bool isParry(const std::string &) const = 0;
    /** The named row puts the equipped items away while it plays. */
    virtual bool hidesEquippedItems(const std::string &) const = 0;
    /**
     * A looping or fire-and-forget dialog animation: dialoganimations.2da rows
     * from ID 10000, animations.2da rows below its row count, otherwise the cut
     * clip IDs (one-shot 1000-1327, looping 1400-1727).
     */
    virtual bool isDialogAnimation(int id) const = 0;
    /** The dialoganimations.2da row, or none outside the table. */
    virtual std::optional<std::reference_wrapper<const DialogAnimation>> findDialogAnimation(int row) const = 0;
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
    bool isDialogAnimation(int id) const override;
    std::optional<std::reference_wrapper<const DialogAnimation>> findDialogAnimation(int row) const override;

private:
    struct Anim {
        std::string name;
        bool attack {false};
        bool overlay {false};
        bool looping {false};
        bool fireForget {false};
        bool parry {false};
        bool hidesEquippedItems {false};
        /** Whether the row is a looping or fire-and-forget dialog animation. */
        bool dialog {false};
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
    void parseDialogAnims(resource::TwoDA &dialogAnimDa);

    using AttackResultMap = std::map<std::pair<std::string, CreatureWieldType>, AttackResult>;

    resource::ITwoDAs &_twoDas;
    std::vector<Anim> _anims;
    /** The first row of each lower-case name. */
    std::unordered_map<std::string, size_t> _animIndexByName;
    AttackResultMap _attackResults;
    std::map<std::string, std::vector<int>> _meleeImpactTimes;
    /** The dialoganimations.2da rows. */
    std::vector<DialogAnimation> _dialogAnims;
};

} // namespace game

} // namespace reone
