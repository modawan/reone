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

#include "reone/game/types.h"

namespace reone {

namespace resource {

class TwoDAs;
class TwoDA;

} // namespace resource

namespace game {

class IAnimations {
public:
    virtual ~IAnimations() = default;
    virtual void clear() = 0;

    virtual std::string getNameById(uint32_t id) const = 0;
    virtual std::string getAttackResult(std::string attackAnim, CreatureWieldType targetWield, AttackResultType result) const = 0;
};

class Animations : public IAnimations {
public:
    Animations(resource::TwoDAs &twoDas) :
        _twoDas(twoDas) {}

    void init();
    void clear() override;

    std::string getNameById(uint32_t id) const override;
    std::string getAttackResult(std::string attackAnim, CreatureWieldType targetWield, AttackResultType result) const override;

private:
    struct Anim {
        std::string name;
        bool attack {false};
    };

    static constexpr uint32_t kNoAnim = std::numeric_limits<uint32_t>::max();

    struct AttackResult {
        uint32_t parry {kNoAnim};
        uint32_t dodge {kNoAnim};
        uint32_t damage {kNoAnim};
    };

    void parseAnims(resource::TwoDA &animDa);
    void parseCombatAnim(resource::TwoDA &combatAnimDa);

    using AttackResultMap = std::map<std::pair<std::string, CreatureWieldType>, AttackResult>;

    resource::TwoDAs &_twoDas;
    std::vector<Anim> _anims;
    AttackResultMap _attackResults;
};

} // namespace game

} // namespace reone
