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

#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

// A driven step is tried at most this many times.
static constexpr int kDriveTries = 6;
// How far a driven leader standing where it does not fit looks for a spot
// that does: square rings out to about a metre.
static constexpr float kDriveNudgeRadius = -1.01f;
// Door and placeable walls count this far below and above the leader,
// whatever their height.
static constexpr float kStaticWallReach = 1000.0f;

// The leader is driven along a straight line to a point a step ahead. A
// companion or puppet in the way is pushed aside first; when someone is still
// in the way and the leader stands where it does not fit, the leader first
// moves to the nearest spot that does. A point the leader cannot walk straight
// to is slid: past a creature, onto the line through where they meet, across
// the way out from that creature's centre; along a wall, as far along the wall
// from where the leader stands. The leader steps only to a point it can walk
// straight to. A step that would bring a door or placeable wall within the
// leader's personal space is slid round that object, unless the leader stands
// that close already. It stays where it is once six lines have failed, or when
// two meetings in a row turn it opposite ways across its path, as in a corner.
static void driveLeader(Game &game, Area &area, const std::shared_ptr<Creature> &leader, const glm::vec2 &dir, float step) {
    static constexpr glm::vec3 kUp {0.0f, 0.0f, 1.0f};
    const glm::vec3 start {leader->position()};
    glm::vec3 target {start.x + dir.x * step, start.y + dir.y * step, start.z};
    const glm::vec2 back {-dir};
    glm::vec2 previousNormal {0.0f};
    for (int attempt = 1;; ++attempt) {
        auto line = area.testDirectLine(*leader, leader->position(), target);
        if (line.result == Area::DirectLine::CreatureBlocked) {
            leader->pushAside(*game.getObjectById<Creature>(line.blocker->get().id()), leader->position(), target, dir);
            line = area.testDirectLine(*leader, leader->position(), target);
            if (line.result == Area::DirectLine::CreatureBlocked && !area.isSafeLocationPoint(leader->position(), *leader)) {
                if (auto spot = area.computeSafeLocation(leader->position(), kDriveNudgeRadius, *leader, true)) {
                    leader->setPosition(*spot);
                    area.determineObjectRoom(*leader);
                }
                line = area.testDirectLine(*leader, leader->position(), target);
            }
        }
        if (line.result == Area::DirectLine::Clear) {
            const glm::vec3 position {leader->position()};
            const float minZ = position.z - kStaticWallReach;
            const float maxZ = position.z + kStaticWallReach;
            const auto wall = area.findStaticWallNear(target, *leader, minZ, maxZ);
            if (!wall || area.findStaticWallNear(position, *leader, minZ, maxZ)) {
                // The room under the step is looked for around height 0.
                area.stepCreatureTo(leader, glm::vec3(glm::vec2(target), 0.0f), Area::MoveFacing::Keep);
                return;
            }
            // The leader slides across the way out from the object's centre;
            // heading straight at it, the leader stays where it is.
            const glm::vec2 away {glm::vec2(position) - glm::vec2(wall->get().position())};
            const glm::vec2 way {glm::vec2(target) - glm::vec2(position)};
            if (glm::length(away) == 0.0f || glm::dot(way, glm::vec2(-away.y, away.x)) == 0.0f) {
                return;
            }
            line.result = Area::DirectLine::Blocked;
            line.normal = glm::vec3(glm::normalize(away), 0.0f);
        }
        if (attempt == kDriveTries) {
            return;
        }

        const glm::vec2 across {line.normal};
        if (attempt > 1) {
            const float now = glm::dot(back, glm::vec2(-across.y, across.x));
            const float before = glm::dot(back, glm::vec2(-previousNormal.y, previousNormal.x));
            if ((now > 0.0f && before < 0.0f) || (now < 0.0f && before > 0.0f)) {
                return;
            }
        }
        if (line.result == Area::DirectLine::CreatureBlocked) {
            target -= glm::vec3(across, 0.0f) * glm::dot(across, glm::vec2(target - line.stopPoint));
        } else {
            const glm::vec3 position {leader->position()};
            const glm::vec2 way {glm::vec2(target) - glm::vec2(position)};
            const glm::vec2 along {glm::normalize(glm::vec2(glm::cross(kUp, line.normal)))};
            const glm::vec2 slide {along * (glm::sign(glm::dot(way, along)) * glm::length(way))};
            target = glm::vec3(position.x + slide.x, position.y + slide.y, position.z);
        }
        previousNormal = across;
    }
}

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
    switch (event.code) {
    case input::KeyCode::W: {
        _moveForward = true;
        return true;
    }
    case input::KeyCode::Z: {
        _moveLeft = true;
        return true;
    }
    case input::KeyCode::S: {
        _moveBackward = true;
        return true;
    }
    case input::KeyCode::C: {
        _moveRight = true;
        return true;
    }
    case input::KeyCode::X: {
        // The flourish answers the press only; holding the key repeats nothing.
        if (!event.repeat) {
            std::shared_ptr<Creature> partyLeader(_party.getLeader());
            partyLeader->flourishWeapons(true);
        }
        return true;
    }
    case input::KeyCode::B: {
        _walk = true;
        return true;
    }
    default:
        return false;
    }
}

bool Player::handleKeyUp(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::W:
        _moveForward = false;
        return true;

    case input::KeyCode::Z:
        _moveLeft = false;
        return true;

    case input::KeyCode::S:
        _moveBackward = false;
        return true;

    case input::KeyCode::C:
        _moveRight = false;
        return true;

    case input::KeyCode::B:
        _walk = false;
        return true;

    default:
        return false;
    }
}

bool Player::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (_camera.isMouseLookMode() && event.button == input::MouseButton::Left) {
        _moveForward = true;
        _leftPressedInMouseLook = true;
        return true;
    }

    return false;
}

bool Player::handleMouseButtonUp(const input::MouseButtonEvent &event) {
    if (_leftPressedInMouseLook && event.button == input::MouseButton::Left) {
        _moveForward = false;
        _leftPressedInMouseLook = false;
        return true;
    }

    return false;
}

void Player::update(float dt) {
    _moving = false;
    std::shared_ptr<Creature> partyLeader(_party.getLeader());
    if (partyLeader) {
        partyLeader->setDriveSpeed(0.0f);
    }
    // A leader that cannot be commanded, or held by a time stop, does not
    // answer the steering.
    if (!partyLeader || partyLeader->isMovementRestricted() || !partyLeader->isCommandable() ||
        _module.game().isFrozenByTimeStop(*partyLeader)) {
        return;
    }
    float facing = 0.0f;
    bool movement = true;

    if (_moveForward) {
        facing = _camera.facing();
    } else if (_moveBackward) {
        facing = _camera.facing() + glm::pi<float>();
    } else if (_moveLeft) {
        facing = _camera.facing() + glm::half_pi<float>();
    } else if (_moveRight) {
        facing = _camera.facing() - glm::half_pi<float>();
    } else {
        movement = false;
    }

    if (movement) {
        // Moving by input clears as the player's controls do.
        _module.game().combat().clearAllOrders(*partyLeader);
        partyLeader->clearPath();
        // Moving by input releases the leader's orientation lock and ends its
        // engaged exchange.
        partyLeader->dropOrientationLock();
        partyLeader->setEngagedExchange(false);
        glm::vec2 dir(glm::normalize(glm::vec2(-glm::sin(facing), glm::cos(facing))));
        // A leader limited from running walks however it is driven.
        const bool run = !_walk && !partyLeader->isRunLimited();
        // The leader turns at once toward where it is steered.
        partyLeader->setFacing(-glm::atan(dir.x, dir.y));
        const float step = (run ? partyLeader->runSpeed() : partyLeader->walkSpeed()) * dt;
        driveLeader(_module.game(), _area, partyLeader, dir, step);
        _moving = true;
        partyLeader->setMovementType(run ? Creature::MovementType::Run : Creature::MovementType::Walk);
        partyLeader->setRunning(run);
        // The driven speed, which the motion blur follows.
        partyLeader->setDriveSpeed(run ? partyLeader->runSpeed() : partyLeader->walkSpeed());
    } else if (partyLeader->actions().empty()) {
        partyLeader->setMovementType(Creature::MovementType::None);
        // A leader coming to a halt under the player's control slows to a walk
        // first, so it is no longer running.
        partyLeader->setRunning(false);
    }
}

void Player::stopMovement() {
    _moving = false;
    _moveForward = false;
    _moveLeft = false;
    _moveBackward = false;
    _moveRight = false;

    std::shared_ptr<Creature> partyLeader(_party.getLeader());
    if (partyLeader) {
        partyLeader->setMovementType(Creature::MovementType::None);
    }
}

bool Player::isMovementRequested() const {
    return _moveForward || _moveLeft || _moveBackward || _moveRight;
}

} // namespace game

} // namespace reone
