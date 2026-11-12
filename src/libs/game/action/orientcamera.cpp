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
#include "reone/game/object/camera/thirdperson.h"
#include "reone/game/object/creature.h"

namespace reone {

namespace game {

// The command's mask bit for a facing.
static constexpr int32_t kFacingMask = 1;

// The command keeps the script's facing in degrees scaled by 180/pi, as
// though they were radians; the heading is those degrees in radians.
static float scriptHeading(float facing) {
    const float scriptDegrees = glm::radians(facing);
    return glm::radians(scriptDegrees);
}

// Under a script's camera mode the leader's camera command turns the follow
// camera to its facing: the script's direction, anticlockwise from east, in
// degrees. Otherwise the camera on a stick keeps to its own.
void OrientCameraAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    auto leader = _game.party().getLeader();
    if ((_command.mask & kFacingMask) != 0 && leader && leader->id() == actor.id() && _game.isScriptCameraMode()) {
        if (auto *camera = dynamic_cast<ThirdPersonCamera *>(_game.getActiveCamera())) {
            camera->setFacing(scriptHeading(_command.facing) - glm::half_pi<float>());
        }
    }
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
    addAction(_game.newAction<OrientCameraAction>(OrientCameraAction::Command {kFacingMask, facing}));
}

} // namespace game

} // namespace reone
