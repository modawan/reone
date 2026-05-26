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

#include "reone/game/object/camera/static.h"

#include "reone/game/di/services.h"
#include "reone/resource/gff.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

void StaticCamera::deserialize(const resource::Gff &gff) {
    gff.readInt(_cameraId, "CameraID");
    gff.readFloat(_fieldOfView, "FieldOfView");
    gff.readFloat(_height, "Height");
    if (gff.readVector(_position, "Position")) {
        _position.z += _height;
    }

    // Keep orientation and pitch separate, so we can serialize them back.
    gff.readOrientation(_staticOrientation, "Orientation");
    gff.readFloat(_staticPitch, "Pitch");
    _orientation = _staticOrientation * glm::quat_cast(glm::eulerAngleX(glm::radians(_staticPitch)));

    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    cameraSceneNode()->setPerspectiveProjection(glm::radians(_fieldOfView), _aspect, kDefaultClipPlaneNear, kDefaultClipPlaneFar);

    updateTransform();
}

} // namespace game

} // namespace reone
