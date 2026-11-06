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

#include "reone/input/event.h"

#include "minigame.h"

namespace reone {

namespace scene {

class ModelSceneNode;

}

namespace game {

class Game;
class FirstPersonCamera;

/**
 * Developer skeleton for the swoop racing minigame, consuming passive metadata
 * parsed from the area's .are MiniGame struct (see MinigameSpec).
 *
 * The bike rides the "modelhook" node of the player's track model as the
 * model's "track" animation plays, at speed / 100 of the authored rate:
 *
 *   hook    = track.hookAt(trackTime)
 *   bikePos = hook * (lateralOffset, 0, 0)
 *
 * The bike faces along the hook and strafes along the hook's X axis. Left and
 * right input is a lateral strafe only; any lean is small and purely cosmetic.
 * The track is placed at the LYT track position. When the race starts without
 * an animated track (no track model, no "modelhook", no "track" animation, or
 * the leader-anchored fallback), the bike instead moves in a straight line
 * from the start frame:
 *
 *   centerline = trackStart + trackForward * progress
 *   bikePos    = centerline + trackRight   * lateralOffset
 *
 * Intentionally minimal: no obstacles, enemies, boost, lap/finish, HUD, or
 * scripts.
 */
class SwoopRace {
public:
    SwoopRace(Game &game) :
        _game(game) {
    }

    /**
     * Initialize and activate the race from the given metadata, anchored at the
     * supplied world position and facing.
     *
     * @param camera the area first-person camera, reused as a chase camera; may be null
     * @param bikeRoot the visible bike model root scene node (already added to the scene)
     * @param bikeChildNodes visible bike model child scene nodes attached to the root
     * @param finishProgress forward-progress distance at which the race is considered finished
     * @param track the player's track; the bike follows it when its animation has a length
     */
    void start(const MinigameSpec &spec,
               FirstPersonCamera *camera,
               std::shared_ptr<scene::ModelSceneNode> bikeRoot,
               std::vector<std::shared_ptr<scene::ModelSceneNode>> bikeChildNodes,
               const glm::vec3 &startPosition,
               float startFacing,
               float finishProgress,
               std::optional<MinigameTrack> track = std::nullopt);

    void stop();

    void update(float dt);
    bool handle(const input::Event &event);

    bool isActive() const { return _active; }

    // The visible bike model root scene node, so the owner can detach it on exit.
    std::shared_ptr<scene::ModelSceneNode> bikeRoot() const { return _bikeRoot; }

    // Diagnostics

    MinigameType type() const { return _type; }
    const std::string &trackResRef() const { return _trackResRef; }
    size_t modelCount() const { return _modelCount; }
    float movePerSec() const { return _movePerSec; }
    float lateralAccel() const { return _lateralAccel; }
    float cameraViewAngle() const { return _camFov; }
    float elapsed() const { return _elapsed; }
    float speed() const { return _speed; }

    float lateralLeftBound() const { return _lateralLeftBound; }
    float lateralRightBound() const { return _lateralRightBound; }
    const std::string &lateralBoundSource() const { return _lateralBoundSource; }

    float progress() const { return _progress; }
    float lateralOffset() const { return _lateralOffset; }
    glm::vec3 position() const { return bikePosition(); }

    float finishProgress() const { return _finishProgress; }

    // True when the bike rides an animated track rather than the straight line.
    bool followsTrack() const { return _track.has_value(); }

    // Time into the track animation, and the hook transform the bike rides.
    float trackTime() const { return _trackTime; }
    float trackLength() const { return _track ? _track->length() : 0.0f; }
    const glm::mat4 &trackHook() const { return _trackHook; }

    // True once a track with no loops left has played to its end.
    bool trackEnded() const { return _trackEnded; }

    // True once forward progress has reached the finish threshold.
    bool finishReached() const { return _finishProgress > 0.0f && _progress >= _finishProgress; }

    // END Diagnostics

private:
    Game &_game;

    bool _active {false};

    // Tuning copied from metadata at start

    MinigameType _type {MinigameType::None};
    float _movePerSec {0.0f};
    float _lateralAccel {0.0f};
    float _camFov {0.0f};
    std::string _trackResRef;
    size_t _modelCount {0};

    // Track-relative frame, computed once at start. The bike advances along
    // _trackForward and strafes along _trackRight; _facing is the down-course
    // yaw applied to the bike model.
    glm::vec3 _trackStart {0.0f};
    glm::vec3 _trackForward {0.0f, 1.0f, 0.0f};
    glm::vec3 _trackRight {1.0f, 0.0f, 0.0f};
    float _facing {0.0f};

    // The animated track the bike rides, if any; see followsTrack.
    std::optional<MinigameTrack> _track;
    float _trackTime {0.0f};
    glm::mat4 _trackHook {1.0f};
    int _trackLoops {0}; // Num_Loops: below 0 loops forever, above 1 replays
    bool _trackEnded {false};

    // Runtime state

    float _speed {0.0f};
    float _progress {0.0f};       // forward distance along the track frame
    float _finishProgress {0.0f}; // progress at which the race auto-finishes (0 = none)
    float _lateralOffset {0.0f};  // strafe offset along _trackRight
    float _lateralVel {0.0f};
    float _elapsed {0.0f};
    int _steerDir {0}; // -1 left, +1 right, 0 none

    // Lateral travel limits (world units), derived from tunnel metadata or a
    // safe fallback. Asymmetric: negative offset clamps to -_lateralLeftBound,
    // positive offset clamps to +_lateralRightBound.
    float _lateralLeftBound {0.0f};
    float _lateralRightBound {0.0f};
    std::string _lateralBoundSource;

    FirstPersonCamera *_camera {nullptr};
    std::shared_ptr<scene::ModelSceneNode> _bikeRoot;
    std::vector<std::shared_ptr<scene::ModelSceneNode>> _bikeChildNodes;

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);

    float cameraAspect() const;
    glm::vec3 bikePosition() const;
    glm::vec3 bikeForward() const;
    void advanceTrack(float dt);
    void applyChaseCamera();
    void applyBikeTransform();
    void setCameraFieldOfView(float fovDegrees);
    void computeLateralBounds(const MinigamePlayerSpec &player);
};

} // namespace game

} // namespace reone