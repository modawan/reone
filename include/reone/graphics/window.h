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

#include <vector>

#include <glm/vec2.hpp>

#include "options.h"
#include "types.h"

namespace reone {

namespace graphics {

class Cursor;

class Window : boost::noncopyable {
public:
    /** The fullscreen modes of the main display, largest first. */
    static std::vector<DisplayMode> displayModes();
    /** The main display's desktop mode. */
    static DisplayMode desktopMode();

    Window(GraphicsOptions &options) :
        _options(options) {
    }

    ~Window() {
        deinit();
    }

    void init();
    void deinit();

    bool isAssociatedWith(const SDL_Event &event) const;
    bool handle(const SDL_Event &event);

    /** Present the frame, waiting for the display's refresh while the V-Sync option is on. */
    void swap();

    /**
     * Turns on or off the text the system translates the keys into: on, the
     * window receives it, and the system may show its input method or
     * on-screen keyboard. Whatever else turned it on or off, the window
     * follows the system's own state.
     */
    void setTextInputActive(bool active);
    bool isTextInputActive() const;

    bool isInFocus() const {
        return _inFocus;
    }

    bool isCloseRequested() const {
        return _closeRequested;
    }

    void setRelativeMouseMode(bool relative);
    /** Move the system cursor to \p x, \p y in window coordinates. */
    void warpMouse(float x, float y);

    /** The window's size in window coordinates, which the mouse is reported in. */
    glm::ivec2 size() const;
    /** The window's drawable in pixels: more than its size on a high-density display. */
    glm::ivec2 pixelSize() const;

    SDL_Window *sdlWindow() { return _window; };
    SDL_GLContext sdlContext() { return _context; };

private:
    GraphicsOptions &_options;

    bool _inited {false};

    SDL_Window *_window {nullptr};
    SDL_GLContext _context {nullptr};

    uint32_t _windowID {0};
    /** Whether presenting waits for the display's refresh, as last set. */
    bool _vsync {false};

    bool _inFocus {true};
    bool _closeRequested {false};

    bool handleKeyDownEvent(const SDL_KeyboardEvent &event);
};

} // namespace graphics

} // namespace reone
