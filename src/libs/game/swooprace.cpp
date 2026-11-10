/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/swooprace.h"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/camera/firstperson.h"
#include "reone/game/types.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/types.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graph.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/modelnode.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

namespace {

constexpr float kDefaultCameraFovDegrees = 75.0f;

// Chase placement used only when no player model carries a "camerahook".
constexpr float kFallbackChaseDistance = 6.5f;
constexpr float kFallbackChaseHeight = 2.8f;

} // namespace

std::shared_ptr<ModelSceneNode> SwoopRace::loadModel(const std::string &resRef) {
    if (resRef.empty()) {
        return nullptr;
    }
    auto model = _services.resource.models.get(resRef);
    if (!model) {
        return nullptr;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    auto node = sceneGraph.newModel(model, ModelUsage::Placeable);
    // Minigame actors are placed by transform far from any room; the culling
    // that assumes objects stay near their rooms does not apply to them.
    node->setCullingEnabled(false);
    node->setDrawDistance(std::numeric_limits<float>::max());
    return node;
}

bool SwoopRace::start(MinigameSession &session,
                      const MinigameSpec &spec,
                      const std::vector<glm::vec3> &obstaclePositions,
                      FirstPersonCamera *camera) {
    stop();
    _session = &session;
    _camera = camera;
    _actors.assign(session.actors().size(), ActorModels());
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);

    size_t obstacleIndex = 0;
    for (size_t i = 0; i < session.actors().size(); ++i) {
        const auto &actor = session.actors()[i];
        auto &slot = _actors[i];
        std::vector<std::string> models;
        if (actor.kind == MinigameActorKind::Obstacle) {
            models.push_back(actor.name);
        } else {
            for (const auto &model : actor.models) {
                models.push_back(model.resRef);
            }
        }
        for (const auto &resRef : models) {
            auto node = loadModel(resRef);
            if (!node) {
                continue;
            }
            if (slot.nodes.empty()) {
                sceneGraph.addRoot(node);
            } else {
                slot.nodes.front()->addChild(*node);
            }
            if (actor.kind == MinigameActorKind::Player && !_cameraHook) {
                if (auto hook = node->getNodeByName("camerahook")) {
                    _cameraHook = hook;
                }
            }
            slot.nodes.push_back(std::move(node));
        }
        if (actor.kind == MinigameActorKind::Obstacle && !slot.nodes.empty()) {
            glm::vec3 position = obstacleIndex < obstaclePositions.size() ? obstaclePositions[obstacleIndex] : actor.position;
            slot.nodes.front()->setLocalTransform(glm::translate(position));
            slot.nodes.front()->playAnimation("ready", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
        }
        if (actor.kind == MinigameActorKind::Obstacle) {
            ++obstacleIndex;
        }
    }

    _steerLeft = _steerRight = _fireKey = _fireButton = _brakeKey = _brakeButton = false;
    _active = true;
    if (const auto *player = session.player(); player && !player->sounds.engine.empty()) {
        if (auto clip = _services.resource.audioClips.get(player->sounds.engine)) {
            _engineSound = _services.audio.mixer.play(std::move(clip), AudioType::Sound, kDefaultSoundGroup, 1.0f, true);
            _engineSoundResRef = player->sounds.engine;
        }
    }
    float nearClip = spec.nearClip > 0.0f ? spec.nearClip : kDefaultClipPlaneNear;
    float farClip = spec.farClip > 0.0f ? spec.farClip : kDefaultClipPlaneFar;
    setCameraProjection(spec.cameraViewAngle > 0.0f ? spec.cameraViewAngle : kDefaultCameraFovDegrees, nearClip, farClip);
    placeActors();
    updateCamera();
    return true;
}

void SwoopRace::releaseActor(ActorModels &models) {
    if (models.removed) {
        return;
    }
    models.removed = true;
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    for (auto &node : models.nodes) {
        _animator.forget(*node);
    }
    if (!models.nodes.empty()) {
        models.nodes.front()->removeAllChildren();
        sceneGraph.removeRoot(*models.nodes.front());
    }
    models.nodes.clear();
}

void SwoopRace::stop() {
    if (_camera) {
        setCameraProjection(kDefaultCameraFovDegrees, kDefaultClipPlaneNear, kDefaultClipPlaneFar);
    }
    for (auto &actor : _actors) {
        releaseActor(actor);
    }
    _actors.clear();
    _animator.clear();
    if (_engineSound) {
        _engineSound->stop();
        _engineSound.reset();
    }
    _engineSoundResRef.clear();
    _cameraHook = nullptr;
    _camera = nullptr;
    _session = nullptr;
    _active = false;
}

size_t SwoopRace::modelCount() const {
    size_t count = 0;
    for (const auto &actor : _actors) {
        count += actor.nodes.size();
    }
    return count;
}

void SwoopRace::placeActors() {
    if (!_session) {
        return;
    }
    const auto &actors = _session->actors();
    for (size_t i = 0; i < actors.size() && i < _actors.size(); ++i) {
        const auto &actor = actors[i];
        auto &slot = _actors[i];
        if (slot.nodes.empty() || actor.kind == MinigameActorKind::Obstacle) {
            continue;
        }
        glm::mat4 transform = actor.hook;
        if (actor.kind == MinigameActorKind::Player) {
            transform *= glm::translate(_session->offset());
        }
        slot.nodes.front()->setLocalTransform(transform);
    }
}

void SwoopRace::update(float dt) {
    if (!_active || !_session) {
        return;
    }
    publishInput();
    for (auto &actor : _actors) {
        if (actor.removed || actor.removeAfter < 0.0f) {
            continue;
        }
        actor.removeAfter -= dt;
        if (actor.removeAfter <= 0.0f) {
            releaseActor(actor);
        }
    }
    _animator.update(dt);
    placeActors();
    updateCamera();
    if (_engineSound) {
        // Idle to full throttle: a low hum rising with the bike's speed, at the volume the
        // scripts set for the engine (SWMG_SetSoundVolume, sound 1).
        const auto *player = _session->player();
        // Before the scripts set a top speed, the K1 top gear's 420.
        float top = _session->maxSpeed() > 0.0f ? _session->maxSpeed() : 420.0f;
        float ratio = glm::clamp(_session->playerSpeed() / top, 0.0f, 1.0f);
        _engineSound->setPitch(0.7f + 0.8f * ratio);
        _engineSound->setGain(player ? glm::clamp(player->soundVolume[1] / 127.0f, 0.0f, 1.0f) : 1.0f);
    }
}

void SwoopRace::updateCamera() {
    if (!_camera || !_session || _actors.empty() || _actors.front().nodes.empty()) {
        return;
    }
    glm::mat4 eye;
    if (_cameraHook) {
        eye = _cameraHook->absoluteTransform();
    } else {
        // Behind and above the bike, looking down the course.
        const auto &root = _actors.front().nodes.front()->absoluteTransform();
        glm::vec3 bike(root[3]);
        glm::vec3 forward(glm::normalize(glm::vec3(root[1])));
        glm::vec3 position = bike - forward * kFallbackChaseDistance + glm::vec3(0.0f, 0.0f, kFallbackChaseHeight);
        glm::quat orientation(glm::quatLookAt(glm::normalize(bike + forward * 10.0f - position), glm::vec3(0.0f, 0.0f, 1.0f)));
        eye = glm::translate(position) * glm::mat4_cast(orientation);
    }
    _camera->cameraSceneNode()->setLocalTransform(eye);
}

void SwoopRace::setCameraProjection(float fovDegrees, float nearClip, float farClip) {
    if (!_camera) {
        return;
    }
    const auto &graphicsOpts = _game.options().graphics;
    float aspect = graphicsOpts.height > 0 ? graphicsOpts.width / static_cast<float>(graphicsOpts.height) : 1.0f;
    _camera->cameraSceneNode()->setPerspectiveProjection(glm::radians(fovDegrees), aspect, nearClip, farClip);
}

void SwoopRace::playMinigameAnimation(const MinigameActor &actor,
                                      const std::string &name,
                                      bool looping,
                                      bool queue,
                                      bool overlay) {
    if (!_session) {
        return;
    }
    size_t index = static_cast<size_t>(&actor - _session->actors().data());
    if (index >= _actors.size()) {
        return;
    }
    for (auto &node : _actors[index].nodes) {
        _animator.play(*node, name, looping, queue, overlay);
    }
}

void SwoopRace::removeMinigameAnimation(const MinigameActor &actor, const std::string &name) {
    if (!_session) {
        return;
    }
    size_t index = static_cast<size_t>(&actor - _session->actors().data());
    if (index >= _actors.size()) {
        return;
    }
    for (auto &node : _actors[index].nodes) {
        _animator.remove(*node, name);
    }
}

void SwoopRace::playMinigameSound(const std::string &resRef, const glm::vec3 &position) {
    auto clip = _services.resource.audioClips.get(resRef);
    if (!clip) {
        return;
    }
    _services.audio.mixer.play(std::move(clip), AudioType::Sound, kDefaultSoundGroup);
}

void SwoopRace::onMinigameActorDied(const MinigameActor &actor) {
    if (!_session || actor.kind == MinigameActorKind::Player) {
        return;
    }
    size_t index = static_cast<size_t>(&actor - _session->actors().data());
    if (index >= _actors.size()) {
        return;
    }
    auto &slot = _actors[index];
    // Each model plays its "die" and leaves when it ends; one without leaves now.
    float longest = 0.0f;
    for (auto &node : slot.nodes) {
        if (auto die = node->model().getAnimation("die")) {
            _animator.play(*node, "die", false, false, false);
            longest = glm::max(longest, die->length());
        }
    }
    slot.removeAfter = longest;
    if (longest <= 0.0f) {
        releaseActor(slot);
    }
}

void SwoopRace::publishInput() {
    auto &input = _game.minigameInput();
    // Steering keys held over the update keep steering.
    if (_steerLeft || _steerRight) {
        input.steer = (_steerRight ? 1.0f : 0.0f) - (_steerLeft ? 1.0f : 0.0f);
    }
}

bool SwoopRace::handle(const input::Event &event) {
    auto &in = _game.minigameInput();
    switch (event.type) {
    case input::EventType::KeyDown:
    case input::EventType::KeyUp: {
        bool down = event.type == input::EventType::KeyDown;
        switch (event.key.code) {
        case input::KeyCode::Left:
        case input::KeyCode::A:
            _steerLeft = down;
            break;
        case input::KeyCode::Right:
        case input::KeyCode::D:
            _steerRight = down;
            break;
        case input::KeyCode::Space:
        case input::KeyCode::Up:
        case input::KeyCode::W:
            // The shift key: fire on KotOR, accelerate on TSL.
            _fireKey = down;
            setMinigameShiftKey(in, _fireKey || _fireButton, _game.isTSL());
            return true;
        case input::KeyCode::Down:
        case input::KeyCode::S:
            // Brake: the jump of the TSL swoop races.
            _brakeKey = down;
            in.brake = _brakeKey || _brakeButton;
            return true;
        case input::KeyCode::Escape:
        case input::KeyCode::Pause:
            // Escape pauses the race; it does not leave it.
            if (down && !event.key.repeat) {
                _game.pressPauseKey();
            }
            return true;
        default:
            return false;
        }
        in.steer = (_steerRight ? 1.0f : 0.0f) - (_steerLeft ? 1.0f : 0.0f);
        return true;
    }
    case input::EventType::MouseButtonDown:
    case input::EventType::MouseButtonUp:
        if (event.button.button == input::MouseButton::Right) {
            _brakeButton = event.button.pressed;
            in.brake = _brakeKey || _brakeButton;
            return true;
        }
        if (event.button.button != input::MouseButton::Left) {
            return false;
        }
        _fireButton = event.button.pressed;
        setMinigameShiftKey(in, _fireKey || _fireButton, _game.isTSL());
        return true;
    default:
        return false;
    }
}

} // namespace game

} // namespace reone
