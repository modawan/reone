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

#include "reone/game/action/drive.h"
#include "reone/game/game.h"
#include "reone/game/object/camera.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"

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

std::optional<float> Player::steeredFacing() const {
    if (_moveForward) {
        return _camera.facing();
    }
    if (_moveBackward) {
        return _camera.facing() + glm::pi<float>();
    }
    if (_moveLeft) {
        return _camera.facing() + glm::half_pi<float>();
    }
    if (_moveRight) {
        return _camera.facing() - glm::half_pi<float>();
    }
    return std::nullopt;
}

void Player::update(float dt) {
    const auto facing = steeredFacing();
    // Movement input starting is a drive update at once; while the input
    // lasts, one follows each interval.
    _driveUpdateDelay = facing ? _driveUpdateDelay - dt : 0.0f;

    std::shared_ptr<Creature> partyLeader(_party.getLeader());
    // The drive goes with the lead: a creature that no longer leads is no
    // longer driven, and the new leader takes a drive update at once.
    if (auto driven = _driven.lock(); driven && driven != partyLeader) {
        driven->removeActionsOfType(ActionType::Drive);
        _driven.reset();
        _driveUpdateDelay = 0.0f;
    }
    // A leader that cannot be commanded, or held by a time stop, does not
    // answer the steering.
    if (!partyLeader || partyLeader->isMovementRestricted() || !partyLeader->isCommandable() ||
        _module.game().isFrozenByTimeStop(*partyLeader)) {
        return;
    }
    if (!facing) {
        // The leader's drive ends with the input.
        partyLeader->removeActionsOfType(ActionType::Drive);
        if (partyLeader->actions().empty()) {
            partyLeader->setMovementType(Creature::MovementType::None);
            // A leader coming to a halt under the player's control slows to
            // a walk first, so it is no longer running.
            partyLeader->setRunning(false);
        }
        return;
    }

    const glm::vec2 direction {-glm::sin(*facing), glm::cos(*facing)};
    const bool run = !_walk;
    if (_driveUpdateDelay <= 0.0f) {
        // A drive update clears the leader's orders as the player's controls
        // do and gives it a new drive. Orders given in between, such as a
        // script's that clears the drive away, are carried out until the next.
        _driveUpdateDelay = kDriveUpdateInterval;
        _module.game().combat().clearAllOrders(*partyLeader);
        partyLeader->clearPath();
        partyLeader->addAction(_module.game().newAction<DriveAction>(direction, run, glm::vec2(partyLeader->position())));
        _driven = partyLeader;
    } else if (partyLeader->isDriven()) {
        cast<DriveAction>(partyLeader->getCurrentAction())->steer(direction, run);
    }
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

bool Player::isMovementRequested() const {
    return _moveForward || _moveLeft || _moveBackward || _moveRight;
}

bool Player::isMoving() const {
    std::shared_ptr<Creature> partyLeader(_party.getLeader());
    return partyLeader && partyLeader->isDriven();
}

} // namespace game

} // namespace reone
