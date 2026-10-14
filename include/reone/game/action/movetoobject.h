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

#include "../action.h"
#include "../object/creature.h"

namespace reone {

namespace game {

class MoveToObjectAction : public Action {
public:
    struct ForcedState {
        glm::vec3 destination {0.0f};
        uint32_t areaId {kSavedRuntimeInvalidObjectId};
        glm::vec2 offset {0.0f};
        bool active {false};
        /**
         * Absolute deadline in world milliseconds. The saved record stores a
         * day/time pair; it is composed on restore and split again on save, so
         * the running action never rebuilds a calendar.
         */
        uint64_t expiryMilliseconds {0};
    };

    /**
     * A move to within range of an object: a door or placeable is walked to
     * at the mover's use point for it, which the move takes when it starts
     * and takes anew at most once, should the point or the use range change;
     * when \p closeToUseRange is set it is closed on to the use range so
     * taken rather than to \p range. With a check range the move ends only
     * once the object is within the mover's use range for it, lengthened to
     * the check range, and otherwise sets out afresh; a move to an object in
     * no area that is not a creature then ends as it is.
     */
    MoveToObjectAction(Game &game,
                       ServicesView &services,
                       std::shared_ptr<Object> moveTo,
                       bool run,
                       float range,
                       bool force = false,
                       float timeout = -1.0f,
                       bool pointPath = false,
                       std::optional<float> checkRange = std::nullopt,
                       bool closeToUseRange = false);

    MoveToObjectAction(Game &game,
                       ServicesView &services,
                       std::shared_ptr<Object> moveTo,
                       bool run,
                       float range,
                       float timeout,
                       ForcedState forcedState,
                       bool force = true);

    static bool classof(Action *from) {
        return from->type() == ActionType::MoveToObject;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    void onQueued(Object &actor) override;

    std::optional<SavedActionRecord> saveFacingState() const override;

    bool isRun() const { return _run; }
    const std::shared_ptr<Object> &target() const { return _moveTo; }
    float range() const { return _range; }
    const std::optional<float> &checkRange() const { return _checkRange; }
    bool isForced() const { return _force; }
    bool usesPointPath() const { return _pointPath || _force || _timeout >= 0.0f; }
    float timeout() const { return _timeout; }
    const ForcedState &forcedState() const { return _forcedState; }

private:
    std::shared_ptr<Object> _moveTo;
    bool _run;
    float _range;
    bool _force;
    bool _pointPath {false};
    float _timeout;
    ForcedState _forcedState;
    std::optional<float> _checkRange;
    bool _closeToUseRange {false};
    /** The use point and range of a move to a door or placeable. */
    std::optional<Creature::UseApproach> _approach;

    /** The distance the mover closes to: the use range taken, when it closes on it, else the move's range. */
    float closingRange() const;
};

} // namespace game

} // namespace reone
