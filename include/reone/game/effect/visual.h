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

#include "../effect.h"

namespace reone {

namespace scene {
class ModelSceneNode;
}

namespace game {

class ServicesView;
struct VisualEffectDesc;

class VisualEffect : public Effect {
public:
    VisualEffect(int visualEffectId, bool missEffect, ServicesView &services);
    ~VisualEffect();

    void applyTo(Object &object) override;
    void setLocation(glm::vec3 loc) { _location = loc; }
    float duration() const;

private:
    int _visualEffectId;
    bool _missEffect;
    const VisualEffectDesc *_desc {nullptr};
    std::optional<glm::vec3> _location;
    ServicesView &_services;

    std::shared_ptr<scene::ModelSceneNode> _node;
};

} // namespace game

} // namespace reone
