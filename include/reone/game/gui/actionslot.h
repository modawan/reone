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

#pragma once

#include "reone/game/contextaction.h"

namespace reone {
namespace game {

struct ContextAction;
class ServicesView;

struct ActionSlot {
    std::vector<ContextAction> actions;
    size_t indexSelected {0};
};

void renderContextActionIcon(const ContextAction &action, glm::mat4 transform, ServicesView &services);

} // namespace game
} // namespace reone
