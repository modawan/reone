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

#include "reone/game/player.h"

#include <cmath>

#include "reone/game/action/drive.h"
#include "reone/game/game.h"
#include "reone/game/object/camera.h"
#include "reone/game/object/camera/thirdperson.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"

namespace reone {

namespace game {

// The seconds between drive updates while the movement input lasts.
static constexpr float kDriveUpdateInterval = 0.5f;

bool Player::handle(const input::Event &event) {
    std::shared_ptr<Creature> partyLeader(_party.getLeader());
    if (!partyLeader)
        return false;

    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    case input::EventType::KeyUp:
        return handleKeyUp(event.key);
    case input::EventType::MouseButtonDown:
        return handleMouseButtonDown(event.button);
    case input::EventType::MouseButtonUp:
        return handleMouseButtonUp(event.button);
    default:
        return false;
    }
}

bool Player::handleKeyDown(const input::KeyEvent &event) {
    auto &keybinds = _module.game().options().game.activeKeybinds();

    if (keybinds.moveForward.check(event.code)) {
        _moveForward = true;
        return true;
    } else if (keybinds.moveLeft.check(event.code)) {
        _moveLeft = true;
        return true;
    } else if (keybinds.moveBackward.check(event.code)) {
        _moveBackward = true;
        return true;
    } else if (keybinds.moveRight.check(event.code)) {
        _moveRight = true;
        return true;
    } else if (keybinds.flourish.check(event.code)) {
        // The flourish answers the press only; holding the key repeats nothing.
        if (!event.repeat) {
            std::shared_ptr<Creature> partyLeader(_party.getLeader());
            partyLeader->flourishWeapons(true);
        }
        return true;
    } else if (keybinds.walk.check(event.code)) {
        _walk = true;
        return true;
    }

    return false;
}

bool Player::handleKeyUp(const input::KeyEvent &event) {
    auto &keybinds = _module.game().options().game.activeKeybinds();

    if (keybinds.moveForward.check(event.code)) {
        _moveForward = false;
        return true;
    } else if (keybinds.moveLeft.check(event.code)) {
        _moveLeft = false;
        return true;
    } else if (keybinds.moveBackward.check(event.code)) {
        _moveBackward = false;
        return true;
    } else if (keybinds.moveRight.check(event.code)) {
        _moveRight = false;
        return true;
    } else if (keybinds.walk.check(event.code)) {
        _walk = false;
        return true;
    }

    return false;
}

bool Player::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button == input::MouseButton::Left) {
        return _camera.isMouseLookMode();
    }

    return false;
}

bool Player::handleMouseButtonUp(const input::MouseButtonEvent &event) {
    if (event.button == input::MouseButton::Left) {
        return _camera.isMouseLookMode();
    }

    return false;
}

// A leader that is debilitated, lacks the will to move, or is busy with
// something else is not moved by the drive.
static bool holdsDrivenLeader(const Creature &leader) {
    return leader.isDebilitated() || (leader.effectAIStateMask() & Creature::kAIStateMove) == 0 ||
           leader.isDriveBlocked();
}

// A heading in degrees from a direction, from 0 up to 360.
static float headingDegrees(const glm::vec2 &dir) {
    const float degrees = glm::degrees(std::atan2(-dir.x, dir.y));
    return degrees < 0.0f ? degrees + 360.0f : degrees;
}

void Player::update(float dt) {
    static constexpr float kLongestUpdate = 1.0f;
    std::shared_ptr<Creature> partyLeader(_party.getLeader());
    // The drive goes with the lead: a creature that no longer leads is no
    // longer driven, and the new leader takes a drive update at once.
    if (auto driven = _driven.lock(); driven && driven != partyLeader) {
        driven->removeActionsOfType(ActionType::Drive);
        _driven.reset();
        _driveUpdateDelay = 0.0f;
    }
    if (!partyLeader) {
        return;
    }
    auto &game = _module.game();
    // The drive runs on world time, which stands still in a time stop.
    if (game.isTimeStopped()) {
        dt = 0.0f;
    }
    // A leader down for the moment is neither steered nor driven, and its drive
    // stays as it was.
    if (partyLeader->isTemporarilyDead()) {
        return;
    }
    // Only play steers: the third-person camera on the play screen. Under free
    // look, a conversation's camera, a menu or a minigame the drive runs on
    // unsteered: it slows, stops and carries the leader as it would with the
    // keys let go.
    const bool steered = !game.isFreeLook() && game.cameraType() == CameraType::ThirdPerson &&
                         game.currentScreen() == Game::Screen::InGame;
    glm::vec2 steering(0.0f);
    if (steered) {
        steering = steeringInput();
        if (partyLeader->isDriveBlocked()) {
            steering = holdBusySteering(*partyLeader, steering, dt);
        } else {
            _busySteeringWait.reset();
        }
        presentMovingCombatMessage(*partyLeader);
    }
    // A dead leader is not driven, and its drive stays as it was.
    if (partyLeader->isDead()) {
        return;
    }
    dt = std::min(kLongestUpdate, dt);
    // A sneaking leader drives at its walk clip's pace at most.
    const float topSpeed = partyLeader->movesStealthily() ? partyLeader->driveWalkClipSpeed() : partyLeader->driveTopSpeed();
    if (steering != glm::vec2(0.0f)) {
        turnTowardSteering(*partyLeader, steering, topSpeed, dt);
    }
    const DriveOutcome outcome = accelerateDrive(*partyLeader, steering, topSpeed, dt);
    // A leader that cannot be commanded, or held by a time stop, is not
    // driven.
    const bool drivable = !holdsDrivenLeader(*partyLeader) && !partyLeader->isMovementRestricted() &&
                          partyLeader->isCommandable() && !game.isFrozenByTimeStop(*partyLeader);
    if (outcome == DriveOutcome::Moving && drivable) {
        driveLeader(*partyLeader, dt);
        return;
    }
    // The leader's drive ends once the drive no longer moves it.
    _driveUpdateDelay = 0.0f;
    partyLeader->removeActionsOfType(ActionType::Drive);
    // The combat camera turns the leader while it shows the scene: not under
    // free look's or a conversation's camera.
    if (outcome == DriveOutcome::Still && game.getActiveCamera() == &_camera) {
        faceCombatTarget(*partyLeader, dt);
    }
    if (partyLeader->actions().empty()) {
        partyLeader->setMovementType(Creature::MovementType::None);
    }
}

// The drive moves the leader the way it faces at the drive's speed. As the
// leader sets off, and each interval while the drive moves it, its orders are
// cleared as the player's controls clear them and it is given a new drive. A
// script that clears the drive away and gives orders makes the leader busy:
// its orders are carried out until the steering breaks them off
// (holdBusySteering).
void Player::driveLeader(Creature &leader, float dt) {
    const float heading = leader.getFacing();
    const glm::vec2 direction {-glm::sin(heading), glm::cos(heading)};
    const float speed = glm::length(_driveVelocity);
    const bool run = leader.drivesAtRun();
    _driveUpdateDelay -= dt;
    if (!leader.isDriven() || _driveUpdateDelay <= 0.0f) {
        _driveUpdateDelay = kDriveUpdateInterval;
        _module.game().combat().clearAllOrders(leader);
        leader.clearPath();
        auto drive = _module.game().newAction<DriveAction>(direction, run, glm::vec2(leader.position()));
        drive->steer(direction, run, speed);
        leader.addAction(drive);
        _driven = _party.getLeader();
    } else {
        cast<DriveAction>(leader.getCurrentAction())->steer(direction, run, speed);
    }
}

// The opacity of the global fade from which TSL's movement keys stop steering.
static constexpr float kKeysStopFadeOpacity = 0.01f;

bool Player::keysSteer() const {
    const Game &game = _module.game();
    return !game.isTSL() || game.globalFade().opacity() < kKeysStopFadeOpacity;
}

bool Player::steersBack() const {
    return keysSteer() && _moveBackward && !_moveForward;
}

// Each axis comes from its two keys, while they steer. The left button held in
// mouse look steers ahead, unless the keys steer back. Steering along both axes
// at once points the same full way as along one, and walking steers at half
// strength.
glm::vec2 Player::steeringInput() const {
    glm::vec2 steering(0.0f);
    if (keysSteer()) {
        steering = glm::vec2((_moveRight ? 1.0f : 0.0f) - (_moveLeft ? 1.0f : 0.0f),
                             (_moveForward ? 1.0f : 0.0f) - (_moveBackward ? 1.0f : 0.0f));
    }
    if (_module.game().isLeftButtonHeld() && _camera.isMouseLookMode() && steering.y >= 0.0f) {
        steering.y = 1.0f;
    }
    if (steering.x != 0.0f && steering.y != 0.0f) {
        steering = glm::normalize(steering);
    }
    if (_walk) {
        steering *= 0.5f;
    }
    return steering;
}

// In combat mode the leader's line says it is on the move while the drive
// goes at a quarter of a metre a second or more, and turns back to the idle
// line once the drive is slower.
void Player::presentMovingCombatMessage(const Creature &leader) {
    if (!leader.partyCombatMode()) {
        return;
    }
    auto &party = _module.game().party();
    if (driveSpeed() >= kMovingDriveSpeed) {
        party.setCombatMessage(leader, Party::kMovingCombatMessage);
    } else if (party.members().front().combatMessage == Party::kMovingCombatMessage) {
        party.setCombatMessage(leader, party.idleCombatMessage());
    }
}

// The steering points the way the camera looks, turned by the steering's own
// way. The leader turns that way only while the drive moved it last update and
// it is more than a tenth of a degree off. It turns the shorter way round, at
// a rate from the area's fastest at rest to its slowest at the drive's top
// speed, and no further than it needs. Without the will to move it keeps its
// facing.
void Player::turnTowardSteering(Creature &leader, glm::vec2 steering, float topSpeed, float dt) {
    static constexpr float kLeastTurn = 0.1f;
    static constexpr float kLeastTurningSpeed = 0.1f;
    const float look = viewHeading();
    const glm::vec2 way(steering.x * std::cos(look) - steering.y * std::sin(look),
                        steering.x * std::sin(look) + steering.y * std::cos(look));
    const float facing = leader.getFacing();
    const float target = headingDegrees(way);
    const float current = headingDegrees(glm::vec2(-std::sin(facing), std::cos(facing)));
    float off = std::abs(target - current);
    // Positive turns raise the heading.
    float direction = target > current ? 1.0f : -1.0f;
    if (off > 180.0f) {
        direction = -direction;
        off = 360.0f - off;
    }
    if (off <= kLeastTurn || leader.driveSpeed() <= kLeastTurningSpeed) {
        return;
    }
    const auto &style = _module.area()->camStyleDefault();
    const float rate = style.minTurnRate + (1.0f - glm::length(_driveVelocity) / topSpeed) * (style.maxTurnRate - style.minTurnRate);
    const float turn = glm::clamp(rate * dt * direction, -off, off);
    if ((leader.effectAIStateMask() & Creature::kAIStateMove) == 0) {
        return;
    }
    leader.setFacing(glm::mod(facing + glm::radians(turn), glm::two_pi<float>()));
}

// The heading the view looks along: the third-person camera's facing, or
// that of the view direction of whichever other camera shows the scene, such
// as a conversation's.
float Player::viewHeading() const {
    const Camera *view = _module.game().getActiveCamera();
    if (!view || view == &_camera) {
        return _camera.facing();
    }
    const glm::vec3 ahead(-view->sceneNode()->absoluteTransform()[2]);
    return std::atan2(-ahead.x, ahead.y);
}

// Behind the combat camera, a leader the drive leaves where it is turns toward
// the creature it targets when that creature is hostile to it, at up to 900
// degrees a second, unless the leader is busy.
void Player::faceCombatTarget(Creature &leader, float dt) {
    static constexpr float kCombatTurnRate = 900.0f;
    auto &game = _module.game();
    if (!_module.area()->getCamera<ThirdPersonCamera>(CameraType::ThirdPerson)->isCombat()) {
        return;
    }
    auto target = std::dynamic_pointer_cast<Creature>(game.getObjectById(game.lastTarget()));
    if (!target || leader.getReputationToward(*target) > kHighestEnemyReputation || leader.isDriveBlocked()) {
        return;
    }
    const float facing = leader.getFacing();
    float off = glm::degrees(std::atan2(-(target->position().x - leader.position().x),
                                        target->position().y - leader.position().y) -
                             std::atan2(std::sin(facing), std::cos(facing)));
    if (off > 180.0f) {
        off -= 360.0f;
    }
    if (off < -180.0f) {
        off += 360.0f;
    }
    const float limit = kCombatTurnRate * dt;
    const float turn = off >= 0.0f ? std::min(limit, off) : std::max(-limit, off);
    leader.setFacing(glm::mod(facing + glm::radians(turn), glm::two_pi<float>()));
}

// A busy leader is steered only after a wait of 0.3 s. From then on the
// steering passes on every update, and once the drive has gathered more than
// a quarter of a metre a second it breaks off what the leader was doing. A
// leader that cannot be commanded is not steered while busy.
glm::vec2 Player::holdBusySteering(Creature &leader, glm::vec2 steering, float dt) {
    static constexpr float kWait = 0.3f;
    static constexpr float kBreakOffSpeed = 0.25f;
    auto &game = _module.game();
    if (steering == glm::vec2(0.0f)) {
        _busySteeringWait.reset();
        return steering;
    }
    if (!_busySteeringWait) {
        _busySteeringWait = kWait;
    }
    *_busySteeringWait -= dt;
    if (*_busySteeringWait > 0.0f || !leader.isCommandable()) {
        return glm::vec2(0.0f);
    }
    if (driveSpeed() > kBreakOffSpeed) {
        game.combat().clearAllOrders(leader);
        leader.abortFireForgetAnimation();
        leader.dropOrientationLock();
        leader.setEngagedExchange(false);
        leader.clearPath();
        _busySteeringWait.reset();
    }
    return steering;
}

// The drive gathers speed toward the steering times the leader's top speed,
// and loses it without steering, after the leader's acceleration, made
// proportionally slower for a top speed of 1.8 m/s or less. It is worked out
// in four steps an update toward this update's steering, so a change of
// steering counts in full from the update it comes in. With no steering, a
// drive slower than a quarter of a metre a second along both axes stops at
// once; steering along one axis only drops the speed along the other. The
// leader moves only while the drive is faster than a metre a second, and then
// by the way it covered.
Player::DriveOutcome Player::accelerateDrive(const Creature &leader, glm::vec2 steering, float topSpeed, float dt) {
    static constexpr float kRestSpeed = 0.25f;
    static constexpr float kSlowTopSpeed = 1.8f;
    static constexpr float kMovingSpeed = 1.0f;
    const bool steered = steering != glm::vec2(0.0f);
    if (!steered && _driveVelocity != glm::vec2(0.0f) &&
        std::abs(_driveVelocity.x) < kRestSpeed && std::abs(_driveVelocity.y) < kRestSpeed) {
        _driveVelocity = glm::vec2(0.0f);
        return DriveOutcome::Halted;
    }
    if (steering.x == 0.0f && steering.y != 0.0f) {
        _driveVelocity.x = 0.0f;
    }
    if (steering.y == 0.0f && steering.x != 0.0f) {
        _driveVelocity.y = 0.0f;
    }
    float acceleration = leader.driveAcceleration();
    if (topSpeed <= kSlowTopSpeed) {
        acceleration *= topSpeed / kSlowTopSpeed;
    }
    auto accelerationAt = [&](const glm::vec2 &velocity) {
        return acceleration * steering - (acceleration / topSpeed) * velocity;
    };
    const glm::vec2 v1 = _driveVelocity;
    const glm::vec2 a1 = accelerationAt(v1);
    const glm::vec2 v2 = v1 + a1 * (dt * 0.5f);
    const glm::vec2 a2 = accelerationAt(v2);
    const glm::vec2 v3 = v1 + a2 * (dt * 0.5f);
    const glm::vec2 a3 = accelerationAt(v3);
    const glm::vec2 v4 = v1 + a3 * dt;
    const glm::vec2 a4 = accelerationAt(v4);
    _driveVelocity = v1 + (a1 + 2.0f * a2 + 2.0f * a3 + a4) * (dt / 6.0f);
    const glm::vec2 travel = (v1 + 2.0f * v2 + 2.0f * v3 + v4) * (dt / 6.0f);
    if (glm::length(travel) <= 0.0f) {
        return DriveOutcome::Still;
    }
    const float speed = glm::length(_driveVelocity);
    if (!std::isfinite(speed) || speed <= kMovingSpeed) {
        return DriveOutcome::Slow;
    }
    return DriveOutcome::Moving;
}

void Player::stopMovement() {
    _moveForward = false;
    _moveLeft = false;
    _moveBackward = false;
    _moveRight = false;
    _driveUpdateDelay = 0.0f;

    std::shared_ptr<Creature> partyLeader(_party.getLeader());
    if (partyLeader) {
        partyLeader->removeActionsOfType(ActionType::Drive);
        partyLeader->setMovementType(Creature::MovementType::None);
    }
}

void Player::resetDriveAcceleration() {
    _driveVelocity = glm::vec2(0.0f);
}

bool Player::isMovementRequested() const {
    return _moveForward || _moveLeft || _moveBackward || _moveRight;
}

bool Player::isMoving() const {
    std::shared_ptr<Creature> partyLeader(_party.getLeader());
    return partyLeader && partyLeader->isDriven();
}

float Player::driveSpeed() const {
    return std::max(std::abs(_driveVelocity.x), std::abs(_driveVelocity.y));
}

} // namespace game

} // namespace reone
