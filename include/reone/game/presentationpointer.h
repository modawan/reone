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

#include "reone/graphics/cursor.h"
#include "reone/graphics/options.h"
#include "reone/resource/provider/cursors.h"
#include "types.h"

namespace reone::game {

/** One software pointer for a local presentation surface.
 * The provider and graphics options outlive the surface. Screens do not own
 * pointers: the surface draws this once, after its screens and modal overlays.
 */
class PresentationPointer {
public:
    explicit PresentationPointer(resource::ICursors &cursors) : _cursors(cursors) {}

    void setType(resource::CursorType type);
    resource::CursorType type() const { return _type; }
    void setPosition(glm::ivec2 position);
    void setPressed(bool pressed);
    void render(const graphics::GraphicsOptions &options, bool relativeMouseMode);

private:
    resource::ICursors &_cursors;
    resource::CursorType _type {resource::CursorType::None};
    std::shared_ptr<graphics::Cursor> _cursor;
};

/** Existing cursor decision for an already selected object; performs no picking. */
resource::CursorType contextualCursor(ObjectType type, bool dead, bool hostile);

} // namespace reone::game
