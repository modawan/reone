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

#include "reone/input/event.h"

#include "../../camerastyle.h"

#include "../camera.h"

namespace reone {

namespace game {

class ThirdPersonCamera : public Camera {
public:
    ThirdPersonCamera(
        uint32_t id,
        CameraStyle style,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Camera(
            id,
            std::move(sceneName),
            game,
            services),
        _style(std::move(style)) {
    }

    void load();

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void stopMovement() override;
    /**
     * Whether the mouse turns the camera: while the right button or a Ctrl key
     * holds mouse look, or, with the Mouse Look option, while neither does.
     */
    bool isMouseLookMode() const override;

    void setTargetPosition(glm::vec3 position);
    /** The point the camera follows: the leader's camera hook. */
    const glm::vec3 &targetPosition() const { return _targetPosition; }
    void setFacing(float facing) override;
    void setStyle(CameraStyle style);
    /**
     * Out of combat the distance shrinks as the view angle widens.
     *
     * @return false when enabling it in combat, which keeps the distance
     */
    bool setDollyZoom(bool enabled) override;

    /**
     * In combat the camera holds its style rigidly behind the target at a fixed
     * pitch, turning only on request, and keeps its facing across the change.
     */
    void setCombat(bool combat);
    bool isCombat() const { return _combat; }
    /**
     * In combat the camera is placed from a base point over the target and
     * looks back toward an eye point for obstructions.
     */
    void setCombatAnchor(glm::vec3 base, glm::vec3 eye);
    /** Returning to the combat camera starts its turning from rest. */
    void resetCombatTurn();
    /** Face along the line from one point to another. */
    void faceAlong(const glm::vec3 &from, const glm::vec3 &to);
    /**
     * Out of combat, swing to keep an object 15 degrees off centre on its
     * side, until turned by hand or the object lies close to the view line.
     */
    void setLookAtTarget(const std::shared_ptr<Object> &target);

    /**
     * The death view takes over from the given view transform and orbits the
     * fallen creature: it swings around it at thirty degrees per second,
     * closes on three metres, and tilts over to look straight down while its
     * aim point settles from above the body onto it. Nothing else moves the
     * camera until the area is left.
     */
    void startDeathOrbit(const std::shared_ptr<Object> &target, const glm::mat4 &view);
    bool isDeathOrbit() const { return _deathOrbit; }

    /**
     * Play moves the limits the view tilts within. Under mouse look without
     * the left button, while the mouse moves vertically or the tilt-up limit
     * is above zero, they widen to the style's free-look limits; otherwise
     * each above the style's tilt limit comes down toward it at 180 degrees a
     * second. Then mouse look with the left button held, unless the keys
     * steer back, brings each above zero down toward zero at the same rate,
     * centring the view. A style sets the limits to its own tilt limits.
     *
     * @param leftButtonHeld whether the left button is held
     * @param steersBack whether the movement keys steer the leader back
     * @param dt the seconds since the last update
     */
    void updateTiltLimits(bool leftButtonHeld, bool steersBack, float dt);

private:
    CameraStyle _style;

    glm::vec3 _targetPosition {0.0f};
    bool _rotateCCW {false};
    bool _rotateCW {false};
    float _rotationSpeed {0.0f};
    bool _combat {false};
    bool _dollyZoom {false};
    float _turnVelocity {0.0f}; /**< degrees per second */
    float _turnInput {0.0f};    /**< turn input of the previous frame */
    float _mouseTurn {0.0f};    /**< mouse travel this frame while looking */
    float _mouseRise {0.0f};    /**< vertical mouse travel this frame */
    /** The view's tilt from tiltCentre, in degrees, toward the tilt-down limit taken positive. */
    float _tiltOffset {0.0f};
    float _viewPitch {0.0f}; /**< the view's pitch, in degrees from straight down */
    float _lookUp {0.0f};    /**< how far the view may tilt up, in degrees (see updateTiltLimits) */
    float _lookDown {0.0f};  /**< how far the view may tilt down, in degrees (see updateTiltLimits) */
    glm::vec3 _eyePosition {0.0f};

    // Look-at swing

    RuntimeObjectRef<Object> _lookAtTarget;
    float _desiredFacing {0.0f};
    float _swingRate {0.0f}; /**< per second */
    bool _swinging {false};

    // END Look-at swing

    // Death orbit

    bool _deathOrbit {false};
    RuntimeObjectRef<Object> _deathTarget;
    float _deathYaw {0.0f};  /**< degrees, the orbit's own heading */
    float _deathLift {0.0f}; /**< aim point height above the body */
    glm::vec3 _deathPosition {0.0f};
    glm::quat _deathOrientation {1.0f, 0.0f, 0.0f, 0.0f};

    // END Death orbit

    /** Lets go of the look-at target; a swing under way ends at the facing it was swinging to. */
    void releaseLookAt();
    /** Swings the view by \p dt seconds toward keeping the look-at target off centre. */
    void updateLookAtSwing(float dt);
    /** Advances the death view's orbit of the fallen creature by \p dt seconds. */
    void updateDeathOrbit(float dt);

    void updateSceneNode();
    /** Places the combat camera behind the target, pulled in where the view is obstructed. */
    void updateCombatSceneNode();
    /** Turns the camera outside combat by the mouse look's travel over \p dt seconds. */
    void updateMouseTurn(float dt);
    /** Tilts the view outside combat by the mouse's vertical travel over \p dt seconds. */
    void updateTilt(float dt);
    /** Starts the tilt when the camera is built: at the style's pitch, held within its limits. */
    void resetTilt();
    /** The pitch the tilt is measured from, as the title's camera styles measure it. */
    float tiltCentre() const;
    /** 1 when a greater tilt offset raises the pitch, -1 when it lowers it, as the title's camera styles measure it. */
    float tiltSign() const;
    /** Holds the tilt within the tilt limits and sets the view's pitch from it. */
    void clampTilt();
    /** Turns the combat camera by \p dt seconds of the turn keys or the mouse look. */
    void updateCombatTurn(float dt);
    /** The degrees the combat camera turns over \p dt seconds under the turn \p input. */
    float integrateCombatTurn(float input, float dt);
    float projectionFovy() const override;

    bool handleKeyDown(const input::KeyEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);
    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleMouseButtonUp(const input::MouseButtonEvent &event);
};

} // namespace game

} // namespace reone
