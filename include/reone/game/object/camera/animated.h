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

#include "reone/graphics/model.h"
#include "reone/scene/node/model.h"

#include "../camera.h"

namespace reone {

namespace game {

const float kDefaultAnimCamFOV = 55.0f;

class AnimatedCamera : public Camera {
public:
    AnimatedCamera(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Camera(
            id,
            std::move(sceneName),
            game,
            services) {
    }

    void load();

    void update(float dt) override;

    /**
     * Plays the clip a conversation camera ordinal names: once for 1000-1327,
     * looping for 1400-1727. An ordinal that names no clip leaves the camera
     * on its current clip.
     */
    void playAnimation(int ordinal);

    /**
     * The camera clip a conversation camera ordinal names, or empty when it
     * names none. Ordinals 1000-1127, 1200-1327, 1400-1527 and 1600-1727 name
     * cutNNN, cutNNNw, cutNNNl and cutNNNwl, NNN counting from 001 within the
     * band, except that the 29th ordinal of every band names clip 039.
     */
    static std::string getShotClipName(int ordinal);

    bool isAnimationFinished() const;

    void setModel(std::shared_ptr<graphics::Model> model);
    void setFieldOfView(float fovy);

private:
    std::shared_ptr<scene::ModelSceneNode> _model;
    float _fovy {kDefaultAnimCamFOV};

    float projectionFovy() const override;
};

} // namespace game

} // namespace reone
