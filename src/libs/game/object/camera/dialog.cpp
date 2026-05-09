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

#include "reone/game/object/camera/dialog.h"

#include <cmath>

#include "reone/game/di/services.h"
#include "reone/graphics/types.h"
#include "reone/scene/collision.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kMinDialogCameraDistance = 0.0001f;

static bool isFinite(const glm::vec3 &position) {
    return std::isfinite(position.x) &&
           std::isfinite(position.y) &&
           std::isfinite(position.z);
}

void DialogCamera::load() {
    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    cameraSceneNode()->setPerspectiveProjection(glm::radians(_style.viewAngle), _aspect, kDefaultClipPlaneNear, kDefaultClipPlaneFar);
}

void DialogCamera::setSpeakerPosition(glm::vec3 position) {
    if (_speakerPosition != position) {
        _speakerPosition = std::move(position);
        updateSceneNode();
    }
}

void DialogCamera::setListenerPosition(glm::vec3 position) {
    if (_listenerPosition != position) {
        _listenerPosition = std::move(position);
        updateSceneNode();
    }
}

void DialogCamera::setVariant(Variant variant) {
    if (_variant != variant) {
        _variant = variant;
        updateSceneNode();
    }
}

void DialogCamera::updateSceneNode() {
    static glm::vec3 up(0.0f, 0.0f, 1.0f);
    static glm::vec3 down(0.0f, 0.0f, -1.0f);

    glm::vec3 listenerPosition(isFinite(_listenerPosition) ? _listenerPosition : glm::vec3(0.0f));
    glm::vec3 speakerPosition(isFinite(_speakerPosition) ? _speakerPosition : listenerPosition + glm::vec3(1.0f, 0.0f, 0.0f));
    if (!isFinite(_listenerPosition) && isFinite(_speakerPosition)) {
        listenerPosition = speakerPosition - glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::vec3 listenerToSpeaker(speakerPosition - listenerPosition);
    float distance = glm::length(listenerToSpeaker);
    if (!std::isfinite(distance) || distance < kMinDialogCameraDistance) {
        // Some computer/dialog paths provide coincident endpoints; avoid normalizing a zero vector.
        listenerToSpeaker = glm::vec3(1.0f, 0.0f, 0.0f);
        speakerPosition = listenerPosition + listenerToSpeaker;
        distance = 1.0f;
    }
    glm::vec3 dir(listenerToSpeaker / distance);
    glm::vec3 center(0.5f * (listenerPosition + speakerPosition));

    glm::vec3 eye(0.0f);
    glm::vec3 target(0.0f);
    switch (_variant) {
    case Variant::SpeakerClose:
        eye = center;
        eye -= glm::min(0.25f * distance, 1.0f) * dir;
        eye += glm::min(0.25f * distance, 1.0f) * glm::cross(dir, down);
        eye += 0.1f * up;

        target = speakerPosition;
        target -= 0.1f * distance * glm::cross(dir, down);
        target += 0.1f * up;
        break;
    case Variant::SpeakerFar:
        eye = listenerPosition;
        eye -= 0.5f * distance * dir;
        eye += 0.5f * distance * glm::cross(dir, down);

        target = center;
        break;
    case Variant::ListenerClose:
        eye = center;
        eye += glm::min(0.25f * distance, 1.0f) * dir;
        eye += glm::min(0.25f * distance, 1.0f) * glm::cross(dir, down);
        eye += 0.1f * up;

        target = listenerPosition;
        target -= 0.1f * distance * glm::cross(dir, down);
        target += 0.1f * up;
        break;
    case Variant::ListenerFar:
        eye = speakerPosition;
        eye += 0.5f * distance * dir;
        eye += 0.5f * distance * glm::cross(dir, down);

        target = center;
        break;
    case Variant::Both:
    default:
        eye = center;
        eye += glm::min(2.25f * distance, 4.0f) * glm::cross(dir, down);
        eye += 0.25f * up;

        target = center;
        target += 0.25f * down;
        break;
    }

    Collision collision;
    auto &scene = _services.scene.graphs.get(_sceneName);
    if (scene.testLineOfSight(target, eye, collision)) {
        eye = collision.intersection;
    }

    glm::mat4 transform(1.0f);
    transform *= glm::inverse(glm::lookAt(eye, target, up));

    _sceneNode->setLocalTransform(transform);
}

} // namespace game

} // namespace reone
