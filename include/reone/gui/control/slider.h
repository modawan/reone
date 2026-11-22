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
 * A value from 0 to the authored maximum, shown by a thumb along a track.
 *
 * A wide slider runs left to right, a tall one top to bottom. Dragging the
 * thumb moves the value with the pointer, the point pressed staying under it;
 * a press on the track either side of the thumb moves it by one toward the
 * press; the arrow keys along the track step it by one while the slider
 * holds the keyboard; the wheel steps it by its notches, turned up moving
 * the thumb up or right. Every step announces itself (setOnStep), which the
 * GUI sounds.
 * A key or wheel step reports the value at once; what a press and a drag
 * leave is reported on the release, when they moved it or the release is
 * over the slider.
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

        _takesKeyboardOnHover = true;
        _selectable = true;
    }

    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem = false) override;
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;

    bool handleMouseDown(int x, int y) override;
    /** The slider takes every move while it keeps the mouse. */
    bool handleMouseDrag(int x, int y) override;
    /**
     * The release ends a drag and reports the value when the press moved it,
     * or over the slider when it did not; it never clicks the slider.
     */
    bool handleMouseUp(int x, int y, bool over, int clicks) override;
    bool handleKeyDown(input::KeyCode key) override;
    /** The wheel steps the value by its notches, turned up moving the thumb up or right. */
    bool handleMouseWheel(int x, int y) override;

    /** The value, from 0 to maxValue. */
    int value() const { return _value; }
    /** The authored maximum value, the slider's length in steps. */
    int maxValue() const { return _maxValue; }

    /** Clamps the value to the range without reporting it. */
    void setValue(int value);
    /**
     * Steps the value on by \p steps, or back for a negative count, within
     * its range; announces the step and reports the value when it changed.
     */
    void step(int steps);

    /** Called with the value when a key or wheel step moves it, and on a release (handleMouseUp). */
    void setOnValueChanged(std::function<void(int)> fn) { _onValueChanged = std::move(fn); }
    /** Called on each step of the value, by a key, the wheel or a track press; a drag is no step. */
    void setOnStep(std::function<void()> fn) { _onStep = std::move(fn); }

private:
    enum class Press {
        None,
        Thumb,
        TrackBefore,
        TrackAfter
    };

    int _maxValue {0};
    int _value {0};
    std::shared_ptr<graphics::Texture> _thumb;
    bool _dragging {false};
    /** The value when the left button was pressed. */
    int _pressValue {0};
    /** Where along the track the thumb was pressed. */
    int _thumbPressAlong {0};

    std::function<void(int)> _onValueChanged;
    std::function<void()> _onStep;

    bool isVertical() const { return _extent.height > _extent.width; }
    /** Moves the value by \p steps within its range, announcing the move; whether it moved. */
    bool moveThumb(int steps);
    Extent thumbExtent() const;
    Press pressAt(int x, int y) const;
};

} // namespace gui

} // namespace reone
