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

namespace reone {

namespace game {

/**
 * The player steering a creature. Each update the creature faces the way it
 * is steered and steps that way at the speed the player's drive gives it, or,
 * before the player gives one, as a drive restored from a save does, at a run
 * unless it is to walk or cannot run. Commands queued behind the drive wait
 * for it.
 *
 * A drive lasts 0.8 seconds. A creature the player keeps steering is given a
 * new drive before the old one runs out; one left alone stops once its drive
 * does.
 */
class DriveAction : public Action {
public:
    /** How long a drive lasts. */
    static constexpr float kSeconds = 0.8f;

    /**
     * @param direction the way the creature is steered, a unit vector across
     *        the ground
     * @param start where the creature stands as the drive is given
     */
    DriveAction(Game &game, ServicesView &services, glm::vec2 direction, bool run, glm::vec2 start,
                float seconds = kSeconds) :
        Action(game, services, ActionType::Drive),
        _direction(direction),
        _run(run),
        _start(start),
        _timeLeft(seconds) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::Drive;
    }

    /**
     * Steers the creature \p direction from its next step on, showing a run or
     * a walk, at \p speed metres a second.
     */
    void steer(glm::vec2 direction, bool run, float speed) {
        _direction = direction;
        _run = run;
        _speed = speed;
    }
    /**
     * The speed the drive moves \p creature at: the speed it is steered at;
     * before it is steered, a run, unless it is to walk or the creature is
     * limited from running.
     */
    float speed(const Creature &creature) const;

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    glm::vec2 _direction;
    bool _run;
    /** The speed the drive is steered at; none until it is steered. */
    std::optional<float> _speed;
    glm::vec2 _start;
    float _timeLeft;

    bool runs(const Creature &creature) const;
};

} // namespace game

} // namespace reone
