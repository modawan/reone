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

#include "reone/game/action/wait.h"

namespace reone {

namespace game {

void WaitAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    _timer.update(dt);
    if (_timer.elapsed()) {
        complete();
    }
}

std::optional<SavedActionRecord> WaitAction::saveFacingState() const {
    SavedActionRecord result = originalSavedAction().value_or(SavedActionRecord {});
    result.actionId = 30;
    result.declaredParameterCount = 1;
    result.parameters = {SavedActionParameter {
        static_cast<uint32_t>(SavedActionParameterType::Float),
        _timer.remaining()}};
    return result;
}

} // namespace game

} // namespace reone
