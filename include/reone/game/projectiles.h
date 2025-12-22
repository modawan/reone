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

class Creature;

enum class ProjectileAttackType {
    Basic = 1,
    Rapid = 2,
    Sniper = 3,
    Power = 4,
};

struct ProjectileSpec {
    std::vector<std::pair<float, int>> projectiles;
    uint32_t misses;
};

class IProjectiles {
public:
    virtual ~IProjectiles() = default;
    virtual void clear() = 0;
    virtual ProjectileSpec *get(ProjectileAttackType attack, CreatureWieldType wield, int appearance) = 0;
};

class Projectiles : public IProjectiles {
public:
    Projectiles(resource::TwoDAs &twoDas) :
        _twoDas(twoDas) {}

    void init();
    void clear() override;

    ProjectileSpec *get(ProjectileAttackType attack, CreatureWieldType wield, int appearance) override;

private:
    void parseHumanoidWeaponDischarge(resource::TwoDA &weaponDa);

    void parseDroidWeaponDischarge(resource::TwoDA &weaponDa,
                                   resource::TwoDA &droidDa,
                                   resource::TwoDA &animDa);

    resource::TwoDAs &_twoDas;

    std::map<std::pair<CreatureWieldType, ProjectileAttackType>,
             ProjectileSpec>
        _humanoids;

    std::map<std::pair<int, ProjectileAttackType>,
             ProjectileSpec>
        _droids;
};

} // namespace game

} // namespace reone
