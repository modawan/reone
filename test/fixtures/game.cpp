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

#include "game.h"

namespace reone {

namespace game {

MockSpells::MockSpells() {
    ON_CALL(*this, range(testing::_)).WillByDefault(testing::ReturnRef(_blankRange));
    ON_CALL(*this, findRange(testing::_)).WillByDefault(testing::Return(std::nullopt));
}

MockSpells::~MockSpells() = default;

MockCameraStyles::MockCameraStyles() = default;
MockCameraStyles::~MockCameraStyles() = default;

MockClasses::MockClasses() = default;
MockClasses::~MockClasses() = default;

MockFeats::MockFeats() = default;
MockFeats::~MockFeats() = default;

MockFootstepSounds::MockFootstepSounds() = default;
MockFootstepSounds::~MockFootstepSounds() = default;

MockGUISounds::MockGUISounds() = default;
MockGUISounds::~MockGUISounds() = default;

MockPortraits::MockPortraits() = default;
MockPortraits::~MockPortraits() = default;

MockReputes::MockReputes() = default;
MockReputes::~MockReputes() = default;

MockSkills::MockSkills() = default;
MockSkills::~MockSkills() = default;

MockSurfaces::MockSurfaces() = default;
MockSurfaces::~MockSurfaces() = default;

MockProjectiles::MockProjectiles() = default;
MockProjectiles::~MockProjectiles() = default;

MockAnimations::MockAnimations() = default;
MockAnimations::~MockAnimations() = default;

MockVisualEffects::MockVisualEffects() = default;
MockVisualEffects::~MockVisualEffects() = default;

} // namespace game

} // namespace reone
