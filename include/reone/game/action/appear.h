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

#include "reone/system/timer.h"

#include "../action.h"

namespace reone {

namespace game {

/**
 * A creature created to appear holds everything it is told to do for two
 * seconds, then takes up its ordinary pose. Clearing actions never removes it.
 */
class AppearAction : public Action {
public:
    /** How long the creature holds, in seconds. */
    static constexpr float kDuration = 2.0f;

    AppearAction(Game &game, ServicesView &services) :
        Action(game, services, ActionType::Appear) {
        _timer.reset(kDuration);
        setClearable(false);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::Appear;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved Appear command. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    Timer _timer;
};

} // namespace game

} // namespace reone
