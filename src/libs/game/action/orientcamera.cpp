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

#include "reone/game/action/orientcamera.h"

#include "reone/game/game.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

void OrientCameraAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    complete();
}

std::optional<SavedActionRecord> OrientCameraAction::saveFacingState() const {
    return SavedActionRecord::command(SavedActionType::OrientCamera, {
                                                                         {kSavedIntegerParameter, _command.mask},
                                                                         {kSavedFloatParameter, _command.facing},
                                                                         {kSavedFloatParameter, _command.distance},
                                                                         {kSavedFloatParameter, _command.pitch},
                                                                         {kSavedIntegerParameter, _command.mode},
                                                                     });
}

std::shared_ptr<Action> OrientCameraAction::restore(const SavedActionRecord &record, Game &game) {
    return game.newAction<OrientCameraAction>(Command {
        record.integerParameter(0),
        record.floatParameter(1),
        record.floatParameter(2),
        record.floatParameter(3),
        record.integerParameter(4)});
}

void Creature::queueCameraFacing(float facing) {
    static constexpr int32_t kFacingMask = 1;
    addAction(_game.newAction<OrientCameraAction>(OrientCameraAction::Command {kFacingMask, facing}));
}

} // namespace game

} // namespace reone
