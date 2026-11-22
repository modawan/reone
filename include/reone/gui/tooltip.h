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

#include "control.h"

namespace reone {

namespace graphics {

struct GraphicsOptions;

}

namespace gui {

class IGUI;

/**
 * The one tooltip of the interface. Once the pointer has rested on a control
 * for the delay, the control's tooltip text shows in the tooltip panel's
 * tooltip label, below and to the right of the pointer and kept on the
 * screen. The pointer is on one control of the whole interface at a time,
 * whatever GUI shows it. The tooltip goes when the pointer leaves the
 * control, a mouse button is pressed, the wheel turned or a GUI stops being
 * shown. After a press no tooltip shows until the pointer leaves the control
 * it was pressed on.
 */
class Tooltip : boost::noncopyable {
public:
    /** The seconds the pointer rests on a control before its tooltip shows, unless set otherwise. */
    static constexpr float kDefaultDelay = 1.0f;

    /** A tooltip placed within the screen \p graphicsOpt describes, with no panel until one is set. */
    Tooltip(graphics::GraphicsOptions &graphicsOpt) :
        _graphicsOpt(graphicsOpt) {
    }

    /** The panel goes first, while the tooltip it tells is whole. */
    ~Tooltip() {
        _label.reset();
        _panel.reset();
    }

    /** Whether tooltips show at all. Turned off, no tooltip comes, but one shown stays until it goes. */
    void setEnabled(bool enabled) { _enabled = enabled; }
    /** Sets the panel whose tooltip label shows the tooltip; none, or one without the label, shows no tooltip. */
    void setPanel(std::shared_ptr<IGUI> panel);

    /** The pointer moved to \p pointer, in screen coordinates. */
    void setPointer(glm::ivec2 pointer) { _pointer = pointer; }
    /** A move of the pointer begins, which the GUIs shown may follow. */
    void beginPointerMove();
    /**
     * \p gui followed the move and finds \p control under the pointer, or
     * none. Of the controls found during a move, the first is the one the
     * pointer is on.
     */
    void hover(const IGUI &gui, const std::optional<std::reference_wrapper<Control>> &control);
    /**
     * The move ends. When the control the pointer is on changed, a tooltip
     * shown goes and the wait for the new control's starts. A move no GUI
     * followed starts the wait again.
     */
    void endPointerMove();
    /**
     * \p gui stops being shown: a tooltip shown goes, whatever it was on, and
     * the pointer is on no control of \p gui until it moves again.
     */
    void closePanel(const IGUI &gui);
    /** A mouse button went down: a tooltip shown goes, and none shows until the pointer leaves the control it is on. */
    void press();
    /**
     * The mouse wheel turned: a tooltip shown goes. Turned over a list, a
     * tooltip held back since a press may show again after the next wait.
     */
    void turnWheel(bool overList);
    /** \p gui is gone: the tooltip forgets its controls. */
    void forget(const IGUI &gui);

    /** The wait runs; once it is over, the control the pointer is on shows its tooltip, if it has one. */
    void update(float dt);
    /** Draws the tooltip shown, in its control's current text. */
    void render();

private:
    /** A control of a GUI. */
    struct Target {
        std::reference_wrapper<const IGUI> gui;
        std::reference_wrapper<Control> control;
    };

    /** What the GUIs found under the pointer during a move. */
    struct PointerMove {
        /** Whether a GUI followed the move. */
        bool followed {false};
        /** The first control a GUI found. */
        std::optional<Target> target;
    };

    graphics::GraphicsOptions &_graphicsOpt;

    bool _enabled {true};
    /** The seconds waited so far; none while no wait runs. */
    std::optional<float> _timer;
    /** No tooltip shows until the pointer leaves the control it was on at a press. */
    bool _disabled {false};
    glm::ivec2 _pointer {0};
    std::shared_ptr<IGUI> _panel;
    /** The panel's label that shows the tooltip. */
    std::shared_ptr<Control> _label;
    /** Where the panel's layout places its tooltip label. */
    Control::Extent _layoutExtent;
    /** The control the pointer is on. */
    std::optional<Target> _hovered;
    /** The move of the pointer under way. */
    PointerMove _move;
    /** The control whose tooltip shows. */
    std::optional<Target> _shown;
    /** The text shown. */
    std::string _shownText;
    /** Where the pointer was when the tooltip showed; the tooltip is placed by it. */
    glm::ivec2 _shownPointer {0};

    /** The wait starts again, unless tooltips are off or held back after a press. */
    void resetTimer();
    /** The tooltip shown goes, and the wait starts again. */
    void remove();
    /** Holds back tooltips after a press, or lets them come again. */
    void setTemporarilyDisabled(bool disabled);
    /** Shows \p text for \p target in the panel, placed by the pointer. */
    void display(const Target &target, const std::string &text);
    /** Sizes the tooltip to \p text and places it by the pointer as it was when the tooltip showed. */
    void fit(const std::string &text);
};

} // namespace gui

} // namespace reone
