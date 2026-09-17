/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include "reone/game/presentationpointer.h"

namespace reone::game {

void PresentationPointer::setType(resource::CursorType type) {
    if (_type == type)
        return;
    _cursor = type == resource::CursorType::None ? nullptr : _cursors.get(type);
    _type = type;
}

void PresentationPointer::setPosition(glm::ivec2 position) {
    if (_cursor)
        _cursor->setPosition(position);
}

void PresentationPointer::setPressed(bool pressed) {
    if (_cursor)
        _cursor->setPressed(pressed);
}

void PresentationPointer::render(const graphics::GraphicsOptions &options, bool relativeMouseMode) {
    if (!_cursor || relativeMouseMode)
        return;
    static constexpr float kCursorSizeScale = 0.5f;
    float scale = std::min(options.width / 800.0f, options.height / 600.0f) * options.guiScale * kCursorSizeScale;
    _cursor->render(scale);
}

resource::CursorType contextualCursor(ObjectType type, bool dead, bool hostile) {
    using resource::CursorType;
    switch (type) {
    case ObjectType::Creature:
        return dead ? CursorType::Pickup : (hostile ? CursorType::Attack : CursorType::Talk);
    case ObjectType::Door:
        return CursorType::Door;
    case ObjectType::Placeable:
        return CursorType::Pickup;
    default:
        return CursorType::Default;
    }
}

} // namespace reone::game
