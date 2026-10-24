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

#include "../types.h"

#include "../action.h"
#include "reone/system/timer.h"

namespace reone {

namespace game {

/**
 * PlayAnimation and ActionPlayAnimation. The constant stands for an
 * animation ID on the caller: a creature's or a placeable's, or the constant
 * itself on any other object. A negative duration plays the animation at
 * once at its own speed, for the lower constants only. Otherwise a
 * commandable caller queues the animation ID: behind its actions, or with
 * replaceActions in place of them all. A speaker or listener of the running
 * dialogue first shows its plain pause.
 */
void requestScriptAnimation(Game &game, Object &caller, int constant, float speed, float seconds, bool replaceActions);

class PlayAnimationAction : public Action {
public:
    /**
     * @param animation the animation ID the action plays, as the queued
     *        constant stands for it on the actor
     * @param firstRun the action has not run yet
     */
    PlayAnimationAction(Game &game,
                        ServicesView &services,
                        int animation,
                        float speed,
                        float durationSeconds,
                        bool firstRun = true) :
        Action(game, services, ActionType::PlayAnimation),
        _animation(animation),
        _speed(speed),
        _durationSeconds(durationSeconds),
        _firstRun(firstRun) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::PlayAnimation;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    /** The command's animation ID, speed, requested duration and first-run flag. */
    std::optional<SavedActionRecord> saveFacingState() const override;
    /**
     * A saved PlayAnimation command. A command that has run already times its
     * animation again from the load.
     */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

    /** The animation ID the action plays. */
    int animation() const { return _animation; }
    float speed() const { return _speed; }

private:
    int _animation;
    float _speed;
    float _durationSeconds;

    Timer _timer;
    /** The action has not run yet. */
    bool _firstRun {true};
    /** The animation is being timed since the action was made or loaded. */
    bool _playing {false};
    bool _shown {false}; /**< the clip was put on the actor since the action was made or loaded */
};

} // namespace game

} // namespace reone
