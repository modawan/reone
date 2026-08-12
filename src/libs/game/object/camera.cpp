/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/object/camera.h"

#include "reone/game/game.h"
#include "reone/graphics/types.h"
#include "reone/scene/node/camera.h"

namespace reone {

namespace game {

void Camera::update(float) {
    auto &options = _game.options().graphics;
    if (_projectionWidth != options.width || _projectionHeight != options.height) {
        rebuildProjection();
    }
}

void Camera::rebuildProjection() {
    auto &options = _game.options().graphics;
    float aspect = options.width / static_cast<float>(options.height);
    cameraSceneNode()->setPerspectiveProjection(projectionFovy(), aspect, graphics::kDefaultClipPlaneNear, graphics::kDefaultClipPlaneFar);
    _projectionWidth = options.width;
    _projectionHeight = options.height;
}

} // namespace game

} // namespace reone
