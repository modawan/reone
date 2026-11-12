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

#include <glm/vec2.hpp>

#include "../action.h"

namespace reone {

namespace game {

/**
 * An ordered follow, a lasting loop: each round the follower takes its point
 * afresh, walks toward it, pauses, and comes back to this follow, which stays
 * put while the point has moved less than the party's follow range since the
 * follower caught up with it.
 */
class FollowAction : public Action {
public:
    /**
     * @param run whether the follower runs
     * @param point where the follower heads: when anchored, an offset from
     *        the party's follow anchor; otherwise, in x, the distance in front
     *        of the one followed, taken each round from where that one stands
     *        and faces
     * @param anchored whether \p point is an offset from the follow anchor,
     *        as it is for a follower in the party when the order is given; it
     *        stays so whether the follower joins or leaves the party. None
     *        for a restored follow, whose point is as saved until it is
     *        queued.
     * @param caughtUp whether the follower caught up with the point last
     *        headed for
     * @param last the point last headed for
     */
    FollowAction(Game &game,
                 ServicesView &services,
                 std::shared_ptr<Object> follow,
                 bool run,
                 glm::vec2 point,
                 std::optional<bool> anchored,
                 bool caughtUp,
                 glm::vec2 last) :
        Action(game, services, ActionType::Follow),
        _follow(std::move(follow)),
        _run(run),
        _point(point),
        _anchored(anchored),
        _caughtUp(caughtUp),
        _last(last) {
        requireRuntimeObject(_follow);
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::Follow;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
    /**
     * Notes the follower. A restored follow takes whether its point is
     * anchored from the follower's place in the party, and unanchored, the
     * distance back from the saved point.
     */
    void onQueued(Object &actor) override;

    /** A saved Follow command; none once the one followed is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Object> _follow;
    bool _run;
    glm::vec2 _point;
    /** None while a restored follow's point is as saved. */
    std::optional<bool> _anchored;
    /** The follower, once queued: its place in the party decides the form the point is saved in. */
    std::optional<uint32_t> _followerId;
    bool _caughtUp;
    glm::vec2 _last;
};

/**
 * Queued behind an ordered follow: when the follow's loop is left, it starts
 * the follow afresh from where the follower stands, keeping itself behind it.
 */
class FollowCheckAction : public Action {
public:
    /** \p point and \p anchored are as for FollowAction. */
    FollowCheckAction(Game &game,
                      ServicesView &services,
                      std::shared_ptr<Object> follow,
                      bool run,
                      glm::vec2 point,
                      std::optional<bool> anchored) :
        Action(game, services, ActionType::ForceFollowObject),
        _follow(std::move(follow)),
        _run(run),
        _point(point),
        _anchored(anchored) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::ForceFollowObject;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;
    /** As FollowAction::onQueued. */
    void onQueued(Object &actor) override;

    /** A saved FollowCheck command; none once the one followed is gone. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::shared_ptr<Object> _follow;
    bool _run;
    glm::vec2 _point;
    /** None while a restored check's point is as saved. */
    std::optional<bool> _anchored;
    /** As for FollowAction. */
    std::optional<uint32_t> _followerId;
};

} // namespace game

} // namespace reone
