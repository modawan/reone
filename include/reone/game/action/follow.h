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

#include <glm/vec2.hpp>

#include "../action.h"

namespace reone {

namespace game {

/**
 * An ordered follow, a lasting loop: each round the follower walks toward its
 * point, pauses, and comes back to this follow, which stays put while the
 * point has moved less than the party's follow range since the follower
 * caught up with it.
 */
class FollowAction : public Action {
public:
    /**
     * @param run whether the follower runs
     * @param point where the follower heads: for a party member an offset
     *        from the party's follow anchor, for anyone else a point on the
     *        ground beside the one followed
     * @param caughtUp whether the follower caught up with the point last
     *        headed for
     * @param last the point last headed for
     */
    FollowAction(Game &game,
                 ServicesView &services,
                 std::shared_ptr<Object> follow,
                 bool run,
                 glm::vec2 point,
                 bool caughtUp,
                 glm::vec2 last) :
        Action(game, services, ActionType::Follow),
        _follow(std::move(follow)),
        _run(run),
        _point(point),
        _caughtUp(caughtUp),
        _last(last) {
        requireRuntimeObject(_follow);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::Follow;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved Follow command; none once the one followed is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Object> _follow;
    bool _run;
    glm::vec2 _point;
    bool _caughtUp;
    glm::vec2 _last;
};

/**
 * Queued behind an ordered follow: when the follow's loop is left, it starts
 * the follow afresh from where the follower stands, keeping itself behind it.
 */
class FollowCheckAction : public Action {
public:
    FollowCheckAction(Game &game,
                      ServicesView &services,
                      std::shared_ptr<Object> follow,
                      bool run,
                      glm::vec2 point) :
        Action(game, services, ActionType::ForceFollowObject),
        _follow(std::move(follow)),
        _run(run),
        _point(point) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::ForceFollowObject;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved FollowCheck command; none once the one followed is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Object> _follow;
    bool _run;
    glm::vec2 _point;
};

} // namespace game

} // namespace reone
