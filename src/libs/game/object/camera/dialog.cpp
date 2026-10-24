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
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/placeable.h"
#include "reone/scene/collision.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"

using namespace reone::scene;

namespace reone {

namespace game {

// The view angle of a conversation shot, in degrees.
static constexpr float kShotViewAngle = 55.0f;
static constexpr int kPlacedAngle = 6;
static constexpr int kFirstSide = 1;
static constexpr int kSecondSide = 2;
// A shot's points stand this far above where an object stands, below its head height.
static constexpr float kPointLift = 0.1f;
// The head height of an object that is no creature.
static constexpr float kObjectHeadHeight = 1.5f;
// A pull-back closer to nothing than this is none.
static constexpr float kNoPullBack = 0.001f;
// A shot of an object with itself frames it against a point this far off.
static constexpr glm::vec3 kSamePointOffset {1.5f, 1.5f, 0.1f};
// The face shot: its turn off the line between the pair, in degrees, how far
// the point looked at lies from the speaker toward the listener and below
// the speaker's point, how far back the camera stands, and how much a pull-
// back lowers the point looked at and raises the camera per metre.
static constexpr float kFaceTurn = 30.0f;
static constexpr float kFaceLead = 0.2f;
static constexpr float kFaceDrop = 0.04f;
static constexpr float kFaceDistance = 0.5f;
static constexpr float kPullBackLift = 0.2f;
// The shoulder shot stands this much further back along its line.
static constexpr float kShoulderStepBack = 0.15f;
// A camera pulled in stands this far before what hides the pair.
static constexpr float kPullInMargin = 0.1f;
// A vector shorter than this has no direction of its own.
static constexpr float kMinDirectionLength = 1e-9f;
static const std::string kCameraHookNode("camerahook");

// How a shot frames a pair, per metre between them where noted: how far
// from the listener toward the speaker the point looked at lies, how far back
// along the turned line the camera stands, the turn in degrees, and the lifts
// per metre of the speaker's and listener's points, of the point looked at
// and of the camera.
struct Framing {
    float focus {1.0f};
    float distance {0.5f};
    float turn {30.0f};
    float speakerLift {0.0f};
    float listenerLift {0.0f};
    float targetLift {0.0f};
    float eyeLift {0.0f};
};

static Framing framingOf(int angle) {
    switch (angle) {
    case DialogCamera::kShoulderAngle:
        return {0.3f, 0.8f, 30.0f, -0.1f, -0.05f, 0.0f, 0.0f};
    case DialogCamera::kWideAngle:
        return {0.5f, 1.5f, 90.0f, 0.0f, 0.0f, -0.2f, 0.3f};
    default:
        return {};
    }
}

// The direction of v; a vector without one points along x.
static glm::vec3 unit(const glm::vec3 &v) {
    const float length = glm::length(v);
    return length < kMinDirectionLength ? glm::vec3(1.0f, 0.0f, 0.0f) : v / length;
}

// v turned by degrees about the vertical, counterclockwise seen from above.
static glm::vec3 turned(const glm::vec3 &v, float degrees) {
    const float radians = glm::radians(degrees);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {v.x * cosine - v.y * sine, v.x * sine + v.y * cosine, v.z};
}

static std::shared_ptr<ModelSceneNode> modelOf(const Object &object) {
    auto node = object.sceneNode();
    return node && node->type() == SceneNodeType::Model ? std::static_pointer_cast<ModelSceneNode>(node) : nullptr;
}

void DialogCamera::load() {
    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    rebuildProjection();
}

float DialogCamera::projectionFovy() const {
    return glm::radians(kShotViewAngle);
}

void DialogCamera::startConversation(bool modelPoints) {
    _modelPoints = modelPoints;
    _linesOfAction = {};
    _speaker.reset();
    _listener.reset();
    _obstructed = false;
    _angle = 0;
    _placedPosition = glm::vec3(0.0f);
}

void DialogCamera::setShot(const std::shared_ptr<Object> &speaker, const std::shared_ptr<Object> &listener, const Shot &shot) {
    if (_angle == kWideAngle && shot.angle == kWideAngle) {
        return;
    }
    _obstructed = false;
    // Only a creature, placeable or door with a model is framed.
    auto framed = [](const std::shared_ptr<Object> &object) {
        return object && (isa<Creature>(*object) || isa<Placeable>(*object) || isa<Door>(*object)) && modelOf(*object);
    };
    if (!framed(speaker) || !framed(listener)) {
        return;
    }
    auto headHeight = [](const Object &object) {
        auto *creature = dyn_cast<Creature>(&object);
        return creature ? creature->cameraHookHeight() : kObjectHeadHeight;
    };
    _speakerHeight = headHeight(*speaker);
    _listenerHeight = headHeight(*listener);
    _speaker = speaker;
    _listener = listener;
    _shot = shot;
    if (std::abs(_shot.pullBack) < kNoPullBack) {
        _shot.pullBack = 0.0f;
    }
    const int side = addLineOfAction(*speaker, *listener);
    const glm::vec3 lift {0.0f, 0.0f, kPointLift};
    glm::vec3 speakerPoint;
    glm::vec3 listenerPoint;
    View view;
    if (_modelPoints) {
        speakerPoint = speaker->position() + lift;
        listenerPoint = listener->position() + lift;
        view = frame(shot.angle, side, speaker->position(), listener->position(), glm::vec3(0.0f));
    } else {
        speakerPoint = framedPoint(*speaker, _speakerHeight);
        listenerPoint = framedPoint(*listener, _listenerHeight);
        view = frame(shot.angle, side, speakerPoint, listenerPoint, glm::vec3(0.0f));
    }
    // A wide shot must see the point between the pair, and each of them with
    // camera hooks; any other shot the speaker.
    int hits = 0;
    if (shot.angle == kWideAngle) {
        hits = blocked(pairMidpoint(*speaker, *listener), view.eye);
        if (!_modelPoints) {
            hits += blocked(speakerPoint, view.eye) + blocked(listenerPoint, view.eye);
        }
    } else {
        hits = blocked(speakerPoint, view.eye);
    }
    if (hits > 0) {
        _obstructed = true;
    }
    _angle = shot.angle;
    followShot();
}

// The dialog camera takes the placed pose at position, unturned: looking
// straight down.
void DialogCamera::takePlacedShot(const glm::vec3 &position) {
    _obstructed = false;
    _angle = kPlacedAngle;
    _placedPosition = position;
    _sceneNode->setLocalTransform(glm::translate(glm::mat4(1.0f), position));
}

void DialogCamera::followShot() {
    if (_angle == kPlacedAngle) {
        return;
    }
    auto speaker = _speaker.resolve();
    auto listener = _listener.resolve();
    if (!speaker || !listener) {
        return;
    }
    const int side = lineOfActionSide(*speaker, *listener);
    if (side == 0) {
        return;
    }
    if (_modelPoints) {
        applyView(frame(_angle, side, speaker->position(), listener->position(), _placedPosition));
    } else {
        applyView(frame(_angle, side, framedPoint(*speaker, _speakerHeight), framedPoint(*listener, _listenerHeight), _placedPosition));
    }
}

int DialogCamera::addLineOfAction(const Object &speaker, const Object &listener) {
    size_t slot = 0;
    for (; slot < _linesOfAction.size() && _linesOfAction[slot].side != 0; ++slot) {
        const auto &line = _linesOfAction[slot];
        const bool same = line.first == speaker.id() && line.second == listener.id();
        const bool swapped = line.first == listener.id() && line.second == speaker.id();
        if (same || swapped) {
            return _modelPoints ? line.side : lineOfActionSide(speaker, listener);
        }
    }
    const int side = unobstructedSide(speaker, listener);
    _linesOfAction[slot == _linesOfAction.size() ? 0 : slot] = {speaker.id(), listener.id(), side};
    return side;
}

int DialogCamera::lineOfActionSide(const Object &speaker, const Object &listener) const {
    for (const auto &line : _linesOfAction) {
        if (line.side == 0) {
            break;
        }
        if (line.first == speaker.id() && line.second == listener.id()) {
            return line.side;
        }
        if (line.first == listener.id() && line.second == speaker.id()) {
            return line.side == kSecondSide ? kFirstSide : kSecondSide;
        }
    }
    return 0;
}

// Each side is tried with a wide shot and with a shoulder shot over each of
// the pair, counting the lines the scene blocks.
int DialogCamera::unobstructedSide(const Object &speaker, const Object &listener) {
    glm::vec3 speakerPoint;
    glm::vec3 listenerPoint;
    if (_modelPoints) {
        const glm::vec3 lift {0.0f, 0.0f, kPointLift};
        speakerPoint = speaker.position() + lift;
        listenerPoint = listener.position() + lift;
    } else {
        speakerPoint = framedPoint(speaker, _speakerHeight);
        listenerPoint = framedPoint(listener, _listenerHeight);
    }
    const glm::vec3 midpoint = pairMidpoint(speaker, listener);
    const glm::vec3 origin {0.0f};
    auto hits = [&](int side, int otherSide) {
        const glm::vec3 wide = frame(kWideAngle, side, speakerPoint, listenerPoint, origin).eye;
        int count = blocked(midpoint, wide);
        if (!_modelPoints) {
            count += blocked(speakerPoint, wide) + blocked(listenerPoint, wide);
        }
        count += blocked(speakerPoint, frame(kShoulderAngle, side, speakerPoint, listenerPoint, origin).eye);
        count += blocked(listenerPoint, frame(kShoulderAngle, otherSide, listenerPoint, speakerPoint, origin).eye);
        return count;
    };
    const int second = hits(kSecondSide, kFirstSide);
    const int first = hits(kFirstSide, kSecondSide);
    return second <= first ? kSecondSide : kFirstSide;
}

// With model points the head heights lift the points here, and a shot that
// something hides pulls the camera in front of it. With camera hooks, such a
// shot frames the speaker's face, not pulled back.
DialogCamera::View DialogCamera::frame(int angle, int side, glm::vec3 a, glm::vec3 b, const glm::vec3 &from) {
    if (a == b) {
        b = a + kSamePointOffset;
        _obstructed = true;
    }
    const Framing framing = framingOf(angle);
    const float turn = side == kSecondSide ? -framing.turn : framing.turn;
    const float faceTurn = side == kSecondSide ? -kFaceTurn : kFaceTurn;
    View view;
    glm::vec3 line {0.0f};
    const bool face = angle == kFaceAngle || (!_modelPoints && _obstructed);
    if (face) {
        float pullBack = 0.0f;
        if (_modelPoints) {
            a.z += _speakerHeight;
            b.z += _speakerHeight;
        } else {
            b.z = a.z;
            pullBack = _obstructed ? 0.0f : _shot.pullBack;
        }
        view.target = a - kFaceLead * unit(a - b);
        view.target.z += _shot.targetHeight - kFaceDrop - kPullBackLift * pullBack;
        line = unit(turned(a - b, faceTurn));
        view.eye = view.target - (kFaceDistance + pullBack) * line;
        view.eye.z += _shot.cameraHeight + kPullBackLift * pullBack;
    } else {
        const float distance = glm::length(b - a);
        a.z += framing.speakerLift * distance;
        b.z += framing.listenerLift * distance;
        if (_modelPoints) {
            a.z += _speakerHeight;
            b.z += _listenerHeight;
        }
        view.target = b + framing.focus * (a - b);
        view.target.z += framing.targetLift * distance + _shot.targetHeight;
        line = turned(a - b, turn);
        if (angle == kShoulderAngle) {
            view.eye = view.target - (framing.distance * line + kShoulderStepBack * unit(line));
        } else if (angle == kWideAngle) {
            view.eye = view.target - framing.distance * line;
        } else {
            view.eye = from;
        }
        view.eye.z += framing.eyeLift * distance + _shot.cameraHeight;
    }
    if (_modelPoints && _obstructed) {
        if (auto hit = blockedAt(view.target, view.eye)) {
            view.eye = *hit - kPullInMargin * unit(*hit - view.target);
        }
    }
    // The camera faces along the turned line, tilted toward the point looked at.
    const glm::vec3 direction = unit(line);
    view.heading = direction.x == 0.0f && direction.y == 0.0f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::normalize(glm::vec3(direction.x, direction.y, 0.0f));
    return view;
}

glm::vec3 DialogCamera::framedPoint(const Object &object, float height) const {
    if (auto model = modelOf(object)) {
        if (auto hook = model->getNodeByName(kCameraHookNode)) {
            return glm::vec3(hook->absoluteTransform()[3]);
        }
    }
    return object.position() + glm::vec3(0.0f, 0.0f, kPointLift + height);
}

glm::vec3 DialogCamera::pairMidpoint(const Object &speaker, const Object &listener) const {
    const glm::vec3 lift {0.0f, 0.0f, kPointLift};
    glm::vec3 midpoint {0.5f * (speaker.position() + listener.position()) + lift};
    midpoint.z += 0.5f * (_speakerHeight - _listenerHeight) + _speakerHeight;
    return midpoint;
}

int DialogCamera::blocked(const glm::vec3 &from, const glm::vec3 &to) const {
    return blockedAt(from, to) ? 1 : 0;
}

std::optional<glm::vec3> DialogCamera::blockedAt(const glm::vec3 &from, const glm::vec3 &to) const {
    Collision collision;
    auto &scene = _services.scene.graphs.get(_sceneName);
    if (!scene.testLineOfSight(from, to, collision)) {
        return std::nullopt;
    }
    auto speaker = _speaker.resolve();
    auto listener = _listener.resolve();
    if (collision.user && ((speaker && collision.user == speaker.get()) || (listener && collision.user == listener.get()))) {
        return std::nullopt;
    }
    return collision.intersection;
}

void DialogCamera::applyView(const View &view) {
    static const glm::vec3 up {0.0f, 0.0f, 1.0f};
    const glm::vec3 toTarget = unit(view.target - view.eye);
    const float pitch = std::atan2(toTarget.z, glm::length(glm::vec2(toTarget)));
    const glm::vec3 forward = std::cos(pitch) * view.heading + std::sin(pitch) * up;
    _sceneNode->setLocalTransform(glm::inverse(glm::lookAt(view.eye, view.eye + forward, up)));
}

} // namespace game

} // namespace reone
