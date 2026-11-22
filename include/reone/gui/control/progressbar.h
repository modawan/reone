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

class ProgressBar : public Control {
public:
    ProgressBar(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Control(
            gui,
            ControlType::ProgressBar,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {
    }

    /**
     * Takes the maximum and current values from the layout, as the setters
     * do, and a wide bar's start edge from STARTFROMLEFT: the left edge
     * unless the layout sets it to 0.
     */
    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) override;
    /**
     * Draws the control's border, then the progress border, frame and fill,
     * over the inner extent cut along the bar's long axis to the current
     * value's share of the maximum, truncated to whole pixels, or over all
     * of it with no maximum; the art fits the cut by its fill style. The
     * share starts at the top or left edge, or with STARTFROMLEFT 0 at the
     * bottom or right edge. An empty share draws nothing.
     */
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;

    /**
     * The share of a bar's length that a value fills.
     *
     * @param curValue the value shown, taken between 0 and the maximum
     * @param maxValue the value a full bar shows
     * @return the value's share of a positive maximum; the whole bar with none
     */
    static float fillFraction(int curValue, int maxValue);

    /** The value a full bar shows; a negative one is ignored. */
    void setMaxValue(int value);
    /** The value the bar shows, kept between 0 and the maximum. */
    void setCurValue(int value);

protected:
    /** The progress border's dimension takes the frame's scale, as the border's does. */
    void scaleOwnFrames() override;

private:
    /**
     * The border drawn over the filled share. Its colour multiplies all of
     * it; TSL authors greyscale bars and colours them here. Its fill pulses
     * as authored.
     */
    Border _progress {};
    /** The progress border's dimension as authored. */
    int _authoredProgressDimension {0};
    int _maxValue {0};
    int _curValue {0};
    bool _startFromLeft {true};
};

} // namespace gui

} // namespace reone
