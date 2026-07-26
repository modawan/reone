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

#include "reone/resource/types.h"

#include "../effect.h"

namespace reone {

namespace scene {
class ModelSceneNode;
struct ParticleRenderProfile;
}

namespace game {

class ServicesView;
struct VisualEffectDesc;

bool particleRenderProfileForVisualEffect(
    resource::GameID gameId,
    uint32_t visualEffectId,
    const VisualEffectDesc &desc,
    scene::ParticleRenderProfile &profile);

class VisualEffect : public Effect {
public:
    VisualEffect(
        int visualEffectId,
        bool missEffect,
        resource::GameID gameId,
        ServicesView &services);
    ~VisualEffect();

    void applyTo(Object &object) override;
    void setLocation(glm::vec3 loc) { _location = loc; }
    float duration() const;

private:
    int _visualEffectId;
    bool _missEffect;
    resource::GameID _gameId;
    const VisualEffectDesc *_desc {nullptr};
    std::optional<glm::vec3> _location;
    ServicesView &_services;

    std::shared_ptr<scene::ModelSceneNode> _node;
};

} // namespace game

} // namespace reone
