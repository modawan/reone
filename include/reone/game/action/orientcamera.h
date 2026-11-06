/*
 * Copyright (c) 2026 The reone project contributors
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

#include "../action.h"

namespace reone {

namespace game {

/**
 * A camera command: the bits of its mask say what it sets (1 the facing, 2
 * the distance, 4 the pitch, 8 the mode). It goes to the camera of the player
 * whose creature runs it, and the third-person camera ignores it, as the
 * game's default camera does. Done at once.
 */
class OrientCameraAction : public Action {
public:
    struct Command {
        int32_t mask {0};
        float facing {0.0f};
        float distance {0.0f};
        float pitch {0.0f};
        int32_t mode {0};
    };

    OrientCameraAction(Game &game, ServicesView &services, Command command) :
        Action(game, services, ActionType::OrientCamera),
        _command(command) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::OrientCamera;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved OrientCamera command. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    Command _command;
};

} // namespace game

} // namespace reone
