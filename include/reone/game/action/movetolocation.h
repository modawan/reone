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
#include "../location.h"

namespace reone {

namespace game {

class MoveToLocationAction : public Action {
public:
    struct ForcedState {
        uint32_t areaId {kSavedRuntimeInvalidObjectId};
        bool active {false};
        /**
         * Absolute deadline in world milliseconds. The saved record stores a
         * day/time pair; it is composed on restore and split again on save, so
         * the running action never rebuilds a calendar.
         */
        uint64_t expiryMilliseconds {0};
    };

    MoveToLocationAction(Game &game,
                         ServicesView &services,
                         std::shared_ptr<Location> destination,
                         bool run,
                         bool force = false,
                         float timeout = -1.0f,
                         float closeRange = 0.0f,
                         bool straight = false) :
        Action(game, services, ActionType::MoveToLocation),
        _destination(std::move(destination)),
        _run(run),
        _force(force),
        _timeout(timeout),
        _closeRange(closeRange),
        _straight(straight) {
    }

    MoveToLocationAction(Game &game,
                         ServicesView &services,
                         std::shared_ptr<Location> destination,
                         bool run,
                         bool force,
                         float timeout,
                         ForcedState forcedState,
                         float closeRange = 0.0f,
                         bool straight = false) :
        Action(game, services, ActionType::MoveToLocation),
        _destination(std::move(destination)),
        _run(run),
        _force(force),
        _timeout(timeout),
        _closeRange(closeRange),
        _straight(straight),
        _forcedState(std::move(forcedState)) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::MoveToLocation;
    }

    /** A move order releases the mover's orientation lock. */
    void onQueued(Object &actor) override;

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
    /** A saved MoveToPoint command: a walk to a point, or to an object along a point path; none unless its area is loaded, or once the object is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

    bool isRun() const { return _run; }
    const std::shared_ptr<Location> &destination() const { return _destination; }
    bool isForced() const { return _force; }
    float timeout() const { return _timeout; }
    /** How near the walk must end; a script's move walks onto the point itself. */
    float closeRange() const { return _closeRange; }
    /** The walk keeps to the straight line to its destination. */
    bool isStraight() const { return _straight; }
    const ForcedState &forcedState() const { return _forcedState; }

private:
    std::shared_ptr<Location> _destination;
    bool _run;
    bool _force;
    float _timeout;
    float _closeRange;
    bool _straight;
    ForcedState _forcedState;
};

} // namespace game

} // namespace reone
