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

#include "reone/graphics/window.h"

#include <cmath>

#include "SDL3/SDL.h"

#include "reone/system/checkutil.h"
#include "reone/system/threadutil.h"

namespace reone {

namespace graphics {

std::vector<DisplayMode> Window::displayModes() {
    std::vector<DisplayMode> result;
    int count = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &count);
    if (!modes) {
        return result;
    }
    for (int i = 0; i < count; ++i) {
        result.push_back({modes[i]->w, modes[i]->h, static_cast<int>(std::lround(modes[i]->refresh_rate))});
    }
    SDL_free(modes);
    return result;
}

DisplayMode Window::desktopMode() {
    const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (!mode) {
        return {};
    }
    return {mode->w, mode->h, static_cast<int>(std::lround(mode->refresh_rate))};
}

void Window::init() {
    checkThat(!_inited, "Must not be initialized");
    checkMainThread();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    int flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (_options.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (_options.headless) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    _window = SDL_CreateWindow(
        "reone",
        _options.width * _options.winScale / 100,
        _options.height * _options.winScale / 100,
        flags);
    if (!_window) {
        throw std::runtime_error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
    }
    _windowID = SDL_GetWindowID(_window);
    // Fullscreen with a refresh rate takes the display mode nearest the size
    // and rate; without one it keeps the desktop's mode.
    if (_options.fullscreen && _options.refreshRate > 0) {
        SDL_DisplayMode mode;
        if (SDL_GetClosestFullscreenDisplayMode(SDL_GetPrimaryDisplay(), _options.width, _options.height,
                                                static_cast<float>(_options.refreshRate), false, &mode)) {
            SDL_SetWindowFullscreenMode(_window, &mode);
        }
    }
    _context = SDL_GL_CreateContext(_window);
    if (!_context) {
        throw std::runtime_error("SDL_GL_CreateContext failed: " + std::string(SDL_GetError()));
    }
    _vsync = _options.vsync;
    SDL_GL_SetSwapInterval(_vsync ? 1 : 0);

    _inited = true;
}

void Window::deinit() {
    if (!_inited) {
        return;
    }
    SDL_GL_DestroyContext(_context);
    SDL_DestroyWindow(_window);
    _inited = false;
}

bool Window::isAssociatedWith(const SDL_Event &event) const {
    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
        return event.window.windowID == _windowID;
    }

    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        return event.key.windowID == _windowID;
    case SDL_EVENT_KEY_UP:
        return event.key.windowID == _windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return event.motion.windowID == _windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return event.button.windowID == _windowID;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.windowID == _windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.windowID == _windowID;
    default:
        return false;
    }
}

bool Window::handle(const SDL_Event &event) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (handleKeyDownEvent(event.key)) {
            return true;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        _inFocus = true;
        return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        _inFocus = false;
        return true;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        _closeRequested = true;
        return true;
    default:
        break;
    }
    return false;
}

bool Window::handleKeyDownEvent(const SDL_KeyboardEvent &event) {
    switch (event.scancode) {
    case SDL_SCANCODE_C:
        if (event.mod & SDL_KMOD_CTRL) {
            _closeRequested = true;
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

// V-Sync follows the option as the options pages change it.
void Window::swap() {
    if (_vsync != _options.vsync) {
        _vsync = _options.vsync;
        SDL_GL_SetSwapInterval(_vsync ? 1 : 0);
    }
    SDL_GL_SwapWindow(_window);
}

void Window::setRelativeMouseMode(bool isRelative) {
    SDL_SetWindowRelativeMouseMode(_window, isRelative);
}

void Window::warpMouse(int x, int y) {
    SDL_WarpMouseInWindow(_window, static_cast<float>(x), static_cast<float>(y));
}

} // namespace graphics

} // namespace reone
