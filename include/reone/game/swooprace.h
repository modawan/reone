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

#pragma once

#include <string>

#include "reone/input/event.h"

#include "minigame.h"
#include "minigameview.h"

namespace reone {

namespace audio {

class AudioSource;

}

namespace scene {

class ModelSceneNode;
class SceneNode;

}

namespace game {

class Game;
class FirstPersonCamera;
struct ServicesView;

/**
 * Presentation of the swoop race. The race itself (speed, steering, pads,
 * obstacles, the countdown, the timer and the result) is the MinigameSession
 * running the module's scripts. This view loads the models, places them from
 * the session state after each step, plays the animations and sounds the
 * scripts ask for, and turns the keys into the minigame input.
 */
class SwoopRace : public IMinigameView {
public:
    SwoopRace(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * @param obstaclePositions world placement of each session obstacle, in
     *        session obstacle order
     */
    bool start(MinigameSession &session,
               const MinigameSpec &spec,
               const std::vector<glm::vec3> &obstaclePositions,
               FirstPersonCamera *camera);
    void stop();

    /** After each session step: place the models, run queued animations, move the camera. */
    void update(float dt);

    bool handle(const input::Event &event);

    bool isActive() const { return _active; }

    // IMinigameView

    void playMinigameAnimation(const MinigameActor &actor,
                               const std::string &name,
                               bool looping,
                               bool queue,
                               bool overlay) override;
    void removeMinigameAnimation(const MinigameActor &actor, const std::string &name) override;
    void playMinigameSound(const std::string &resRef, const glm::vec3 &position) override;
    void onMinigameActorDied(const MinigameActor &actor) override;

    // END IMinigameView

    // Diagnostics

    size_t modelCount() const;
    bool haveCameraHook() const { return _cameraHook != nullptr; }

    /** The engine loop playing, "" when none. */
    const std::string &engineSound() const { return _engineSoundResRef; }

    // END Diagnostics

private:
    struct ActorModels {
        std::vector<std::shared_ptr<scene::ModelSceneNode>> nodes; // [0] is the root
        float removeAfter {-1.0f}; // seconds until a dying actor leaves the scene
        bool removed {false};
    };

    Game &_game;
    ServicesView &_services;

    bool _active {false};
    MinigameSession *_session {nullptr};
    FirstPersonCamera *_camera {nullptr};
    std::vector<ActorModels> _actors;
    scene::SceneNode *_cameraHook {nullptr};
    MinigameAnimator _animator;

    // The bike's engine loop (the player's Sounds.Engine), pitched with its speed.
    std::shared_ptr<audio::AudioSource> _engineSound;
    std::string _engineSoundResRef;

    bool _steerLeft {false};
    bool _steerRight {false};
    bool _fireKey {false};
    bool _fireButton {false};
    bool _brakeKey {false};
    bool _brakeButton {false};

    std::shared_ptr<scene::ModelSceneNode> loadModel(const std::string &resRef);
    void releaseActor(ActorModels &models);
    void placeActors();
    void updateCamera();
    void setCameraProjection(float fovDegrees, float nearClip, float farClip);
    void publishInput();
};

} // namespace game

} // namespace reone
