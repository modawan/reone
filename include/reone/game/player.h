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

#include <memory>
#include <optional>

#include "reone/input/event.h"

namespace reone {

namespace game {

class Camera;
class Creature;
class Module;
class Party;

/**
 * Encapsulates third-person player controls. The steering drives the leader
 * through a speed that gathers and falls away over time and turns it toward
 * where it is steered. While the drive moves it, the leader carries a drive in
 * its queue that moves it the way it faces at that speed: the leader is given
 * a new one, its orders cleared, as it sets off and each drive update while
 * the drive moves it, and the drive is steered in between.
 */
class Player : boost::noncopyable {
public:
    /** The drive speed from which the leader counts as moving: its combat-mode line says so, and its target lets go. */
    static constexpr float kMovingDriveSpeed = 0.25f;

    Player(Module &module, Camera &camera, const Party &party) :
        _module(module),
        _camera(camera),
        _party(party) {
    }

    bool handle(const input::Event &event);
    void update(float dt);

    void stopMovement();
    /** The drive comes to rest at once. */
    void resetDriveAcceleration();

    bool isMovementRequested() const;
    /** Whether the movement keys steer the leader back; see keysSteer. */
    bool steersBack() const;
    /** Whether the player drives the leader. */
    bool isMoving() const;
    /** The drive's speed along the faster of its two axes. */
    float driveSpeed() const;

private:
    /** What an update of the drive does. */
    enum class DriveOutcome {
        /** The drive comes to rest at once. */
        Halted,
        /** The drive covers next to nothing. */
        Still,
        /** The drive covers ground, too slowly to move the leader. */
        Slow,
        /** The drive moves the leader. */
        Moving
    };

    Module &_module;
    Camera &_camera;
    const Party &_party;

    bool _moveForward {false};
    bool _moveLeft {false};
    bool _moveBackward {false};
    bool _moveRight {false};
    bool _walk {false};
    /** Time left until the next drive update while the drive moves the leader. */
    float _driveUpdateDelay {0.0f};
    /** The creature the last drive update went to. */
    std::weak_ptr<Creature> _driven;
    /** The drive's velocity in the steering's axes: across, then ahead, metres a second. */
    glm::vec2 _driveVelocity {0.0f};
    /** While the busy leader is steered, the seconds before the steering passes; none while it is not. */
    std::optional<float> _busySteeringWait;

    /** The steering of the movement keys and the left button held in mouse look: across, then ahead. */
    glm::vec2 steeringInput() const;
    /**
     * Whether the movement keys steer: in TSL they do not while the global
     * fade is drawn at an opacity of 0.01 or more.
     */
    bool keysSteer() const;
    /**
     * Holds back \p steering of a busy leader: it waits a while, then passes
     * and breaks off what the leader does once the drive gathers speed.
     *
     * @return the steering that passes
     */
    glm::vec2 holdBusySteering(Creature &leader, glm::vec2 steering, float dt);
    /** Shows the moving combat-mode line, or takes it away, by the drive's speed. */
    void presentMovingCombatMessage(const Creature &leader);
    /**
     * Turns \p leader toward where \p steering points relative to the view
     * (viewHeading), over \p dt seconds of a drive with top speed \p topSpeed.
     */
    void turnTowardSteering(Creature &leader, glm::vec2 steering, float topSpeed, float dt);
    /** The heading, in radians, of where the camera that shows the scene looks. */
    float viewHeading() const;
    /** Turns \p leader toward its hostile target over \p dt seconds, behind the combat camera. */
    void faceCombatTarget(Creature &leader, float dt);
    /** Gathers the drive's speed toward \p steering over \p dt seconds, up to \p topSpeed. */
    DriveOutcome accelerateDrive(const Creature &leader, glm::vec2 steering, float topSpeed, float dt);
    /**
     * Has the drive in \p leader's queue move it the way it faces at the
     * drive's speed: a new drive, clearing its orders, as it sets off and each
     * drive update; otherwise the one it carries is steered.
     */
    void driveLeader(Creature &leader, float dt);

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleMouseButtonUp(const input::MouseButtonEvent &event);
};

} // namespace game

} // namespace reone
