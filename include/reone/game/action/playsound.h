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
 * A sound played once where the object stands, as its turn in the queue
 * comes. The object carries on at once.
 */
class PlaySoundAction : public Action {
public:
    PlaySoundAction(Game &game, ServicesView &services, std::string sound) :
        Action(game, services, ActionType::PlaySound),
        _sound(std::move(sound)) {
    }

    static bool classof(Action *from) {
        return from->type() == ActionType::PlaySound;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    std::optional<SavedActionRecord> saveFacingState() const override;

    /** A saved PlaySound command: the sound. */
    static std::shared_ptr<Action> restore(const SavedActionRecord &record, Game &game);

private:
    std::string _sound;
};

} // namespace game

} // namespace reone
