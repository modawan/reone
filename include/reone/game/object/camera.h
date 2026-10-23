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

#include <optional>

#include "reone/input/event.h"
#include "reone/resource/parser/gff/git.h"

#include "../object.h"

namespace reone {

namespace game {

/**
 * Mouse sensitivity at the default setting, 44 on a scale of 100: the degrees
 * a full-strength frame of mouse travel is worth. The scale runs from 10
 * degrees at 0 to 55 at 100.
 */
constexpr float kDefaultMouseSensitivity = 44.0f / 100.0f * 45.0f + 10.0f;

/**
 * Advances a rate that approaches \p rate times the input over one frame of
 * at most 0.2 s. Under an input it accelerates with \p acceleration, and
 * without one it brakes with \p deceleration; the pull toward the rate falls
 * off in proportion to the velocity already reached.
 *
 * The step takes four velocity samples. The first is the velocity at the start
 * of the frame, driven by the previous frame's input. The second is half the
 * sum of that velocity and the change the first sample makes over the frame,
 * driven by the average of the two inputs; the third is half the sum of the
 * starting velocity and the change the second makes, under the same input.
 * The last is the starting velocity plus the change the third makes, driven
 * by this frame's input. The velocity advances by the changes the four samples
 * make, and the distance by the samples times the frame, each weighted one
 * sixth, one third, one third and one sixth. Under a steady input the distance
 * moved per second settles near the rate, the velocity near one and a half
 * times it.
 *
 * @param velocity the velocity, carried across frames
 * @param previousInput the previous frame's input, carried across frames
 * @return the distance moved this frame
 */
float stepAcceleratedRate(
    float &velocity, float &previousInput, float input, float dt,
    float rate, float acceleration, float deceleration);

/**
 * Advances a keyboard turn that approaches 200 degrees per second under a
 * held input and brakes without one; see stepAcceleratedRate().
 *
 * @param velocity the turn velocity, carried across frames
 * @param previousInput the previous frame's input, carried across frames
 * @return the degrees turned this frame
 */
float stepKeyboardTurn(float &velocity, float &previousInput, float input, float dt);

/**
 * Mouse travel gathered over a frame as a strength from -1 to 1: a hundred
 * counts is full strength, and rightward or downward travel reads negative.
 */
float mouseFrameStrength(float travel);

class Camera : public Object {
public:
    Camera(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Camera,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Camera;
    }

    virtual bool handle(const input::Event &event) {
        return false;
    }

    void update(float dt) override;

    virtual void stopMovement() {
    }

    inline std::shared_ptr<scene::CameraSceneNode> cameraSceneNode() const {
        return std::static_pointer_cast<scene::CameraSceneNode>(_sceneNode);
    }

    int cameraId() const { return _cameraId; }
    float fieldOfView() const { return _fieldOfView; }

    /** The vertical view angle in degrees. */
    float viewAngle() const;
    /** Sets the vertical view angle to \p degrees, apart from the camera's own. */
    void setViewAngle(float degrees);
    /**
     * Moves the view angle linearly to \p target over \p seconds. A running
     * animation is dropped and the new one starts from 45 degrees.
     */
    void beginViewAngleAnimation(float target, float seconds);
    /** Drops a running view angle animation where it stands. */
    void endViewAngleAnimation();
    /**
     * Keeps the subject the same size on screen while the view angle moves.
     *
     * @return whether the camera applied the change
     */
    virtual bool setDollyZoom(bool enabled) { return false; }

    /**
     * A visual program that animates the view angle holds the angle and the
     * dolly zoom to return to. Releasing the hold returns them; a pause
     * releases it at once.
     *
     * @return the hold's id
     */
    uint32_t beginViewAngleHold();
    /** Whether the hold \p hold is the one in place. */
    bool holdsViewAngle(uint32_t hold) const { return _viewAngleHold && _viewAngleHold->id == hold; }
    /** The view angle a hold returns to; without a hold, the current one. */
    float heldViewAngle() const { return _viewAngleHold ? _viewAngleHold->restoreAngle : viewAngle(); }
    /** Releases the hold \p hold, if it is the one in place, returning its view angle and dolly zoom. */
    void releaseViewAngleHold(uint32_t hold);

    float facing() const { return _facing; }
    bool isMouseLookMode() const { return _mouseLookMode; }

protected:
    /** A linear move of the view angle, in seconds and degrees. */
    struct ViewAngleAnimation {
        float duration {0.0f};
        float elapsed {0.0f};
        float start {0.0f};
        float target {0.0f};
    };

    /** The view angle and dolly zoom a hold returns to. */
    struct ViewAngleHold {
        /** The id beginViewAngleHold() returned. */
        uint32_t id {0};
        /** The view angle to return to, in degrees. */
        float restoreAngle {0.0f};
        /** Whether the hold turned the dolly zoom on. */
        bool dolly {false};
    };

    int _cameraId {0};
    float _fieldOfView {0.0f};

    float _facing {0.0f};
    bool _mouseLookMode {false};

    int _projectionWidth {-1};
    int _projectionHeight {-1};

    /** The view angle in degrees, when set apart from the camera's own. */
    std::optional<float> _viewAngleOverride;
    /** The running view angle animation, if any. */
    std::optional<ViewAngleAnimation> _viewAngleAnimation;
    /** The view angle hold in place, if any. */
    std::optional<ViewAngleHold> _viewAngleHold;
    /** The id the next hold gets. */
    uint32_t _nextViewAngleHold {1};

    /** Rebuilds the projection from the view angle and the screen's aspect. */
    void rebuildProjection();
    /** Advances a view angle animation by \p dt seconds; a pause releases a hold at once. */
    void updateViewAngle(float dt);
    /** The camera's own vertical field of view, in radians. */
    virtual float projectionFovy() const = 0;
};

} // namespace game

} // namespace reone
