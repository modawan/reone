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
 * PlayAnimation and ActionPlayAnimation. A negative duration plays the
 * animation at once at its own speed, for the lower constants only.
 * Otherwise a commandable caller queues it: behind its actions, or with
 * replaceActions in place of them all. A speaker or listener of the running
 * dialogue first shows its plain pause.
 */
void requestScriptAnimation(Game &game, Object &caller, int constant, float speed, float seconds, bool replaceActions);

class PlayAnimationAction : public Action {
public:
    PlayAnimationAction(Game &game,
                        ServicesView &services,
                        AnimationType animation,
                        float speed,
                        float durationSeconds) :
        Action(game, services, ActionType::PlayAnimation),
        _animation(animation),
        _speed(speed),
        _durationSeconds(durationSeconds) {
    }

    PlayAnimationAction(Game &game,
                        ServicesView &services,
                        AnimationType animation,
                        float speed,
                        float durationSeconds,
                        bool started,
                        bool looping) :
        Action(game, services, ActionType::PlayAnimation),
        _animation(animation),
        _speed(speed),
        _durationSeconds(durationSeconds),
        _looping(looping),
        _playing(started) {
        if (started) {
            _timer.reset(durationSeconds);
        }
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::PlayAnimation;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    AnimationType animation() const { return _animation; }
    float speed() const { return _speed; }

private:
    AnimationType _animation;
    float _speed;
    float _durationSeconds;

    Timer _timer;
    std::optional<bool> _looping;
    bool _playing {false};
    bool _shown {false}; /**< the clip was put on the actor since the action was made or loaded */
};

} // namespace game

} // namespace reone
