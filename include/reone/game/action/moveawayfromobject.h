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

#include "../action.h"

namespace reone {

namespace game {

/**
 * Keeps a creature walking away from an object until it is farther than the
 * range from it, the object leaves the area, or it has set off ten times.
 */
class MoveAwayFromObject : public Action {
public:
    MoveAwayFromObject(Game &game,
                       ServicesView &services,
                       std::shared_ptr<Object> fleeFrom,
                       bool run,
                       float moveAwayRange,
                       int attemptsLeft = kMaxAttempts) :
        Action(game, services, ActionType::MoveAwayFromObject),
        _fleeFrom(std::move(fleeFrom)),
        _run(run),
        _moveAwayRange(moveAwayRange),
        _attemptsLeft(attemptsLeft) {
        requireRuntimeObject(_fleeFrom);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::MoveAwayFromObject;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved MoveAwayFromObject command; none once the object is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    static constexpr int kMaxAttempts = 10;

    std::shared_ptr<Object> _fleeFrom;
    bool _run;
    float _moveAwayRange;
    int _attemptsLeft;
};

} // namespace game

} // namespace reone
