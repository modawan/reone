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

#include "../control.h"

namespace reone {

namespace gui {

/**
 * A horizontal track with a thumb that sits at the value's share of the
 * track. The player drags the thumb, clicks the track either side of it to
 * step the value by one, or steps it with Left and Right while the slider has
 * the focus.
 */
class Slider : public Control {
public:
    Slider(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Control(
            gui,
            ControlType::Slider,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {

        _navigable = true;
        _selectable = true;
    }

    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) override;
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;

    bool handleKeyDown(input::KeyCode key) override;
    bool handleClick(int x, int y, int clicks = 1) override;
    bool handleMouseDown(int x, int y) override;
    void handleMouseDrag(int x, int y) override;

    int value() const { return _value; }

    /** Show a value, held within the slider's range, without reporting it as a change. */
    void setValue(int value);

    /** Called with the new value each time the player moves the slider. */
    void setOnValueChanged(std::function<void(int)> fn) { _onValueChanged = std::move(fn); }

private:
    std::shared_ptr<graphics::Texture> _thumb;
    int _value {0};
    int _maxValue {0};
    std::function<void(int)> _onValueChanged;

    Extent thumbExtent() const;
    void changeValue(int value);
};

} // namespace gui

} // namespace reone
