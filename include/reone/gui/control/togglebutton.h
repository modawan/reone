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

#include <optional>

#include "button.h"

namespace reone {

namespace gui {

class ToggleButton : public Button {
public:
    ToggleButton(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Button(
            gui,
            ControlType::ToggleButton,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {
    }

    /**
     * Loads the button and the frames it shows while on: the SELECTED and
     * HILIGHTSELECTED borders.
     */
    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem = false) override;

    using Button::setPresentationScale;
    void setPresentationScale(float frameLayoutScale, float textLayoutScale) override;

    const glm::vec3 &getBorderColor() const override;

    void toggle();
    /** Turns the button on or off. */
    void setOn(bool on) { _on = on; }

    bool isOn() const { return _on; }

    void setOnColor(const glm::vec3 &color);

protected:
    const std::shared_ptr<Border> &shownBorder() const override;
    const std::shared_ptr<Border> &shownHilight() const override;

private:
    bool _on {false};
    std::optional<glm::vec3> _onColor;
    std::shared_ptr<Border> _onBorder;  /**< drawn while on, in place of the border */
    std::shared_ptr<Border> _onHilight; /**< drawn while on, in place of the hilight */

    /** Gives the on frames the presentation scale of the frames they replace. */
    void scaleOnFrames();
};

} // namespace gui

} // namespace reone
