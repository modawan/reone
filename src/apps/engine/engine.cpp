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

#include "engine.h"

#include "SDL3/SDL.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"

#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/window.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/gameprobe.h"
#include "reone/system/stream/fileoutput.h"
#include "reone/system/stringutil.h"

#include <iomanip>

using namespace reone::audio;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::movie;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

static const std::string kMainThreadName {"main"};

static constexpr int kProfilerInputTimeIndex = 0;
static constexpr int kProfilerUpdateTimeIndex = 1;
static constexpr int kProfilerRenderGraphicsTimeIndex = 2;
static constexpr int kProfilerRenderAudioTimeIndex = 3;

static void imguiInit() {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetStyle().FontScaleMain = 1.5f;
}

static void imguiInitWindow(Window &w) {
    ImGui_ImplSDL3_InitForOpenGL(w.sdlWindow(), w.sdlContext());
    ImGui_ImplOpenGL3_Init();
}

static bool imguiHandle(SDL_Event &ev) {
    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplSDL3_ProcessEvent(&ev);
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

static void imguiNewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (!ImGui::GetIO().WantCaptureMouse) {
        // Switch to software cursor when it leaves ImGui windows.
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
}

static void imguiRender() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static void imguiShutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void Engine::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
    }

    if (_options.graphics.headless) {
        _options.graphics.winScale = 100;
        _options.audio.muted = true;
    }
    _window = std::make_unique<Window>(_options.graphics);
    _window->init();

    imguiInit();
    imguiInitWindow(*_window);

    _optionsView = _options.toView();
    GameProbe probe {_options.game.path};
    auto gameId = probe.probe();

    _clock = std::make_unique<Clock>();
    _clock->init();

    _systemModule = std::make_unique<SystemModule>(*_clock);
    _graphicsModule = std::make_unique<GraphicsModule>(_options.graphics);
    _audioModule = std::make_unique<AudioModule>(_options.audio);
    _movieModule = std::make_unique<MovieModule>();
    _scriptModule = std::make_unique<ScriptModule>();
    _resourceModule = std::make_unique<ResourceModule>(
        gameId,
        _options.game.path,
        _options.graphics,
        _options.audio,
        *_graphicsModule,
        *_audioModule,
        *_scriptModule);
    _sceneModule = std::make_unique<SceneModule>(
        _options.graphics,
        *_resourceModule,
        *_graphicsModule,
        *_audioModule);
    _guiModule = std::make_unique<GUIModule>(
        _options.graphics,
        *_sceneModule,
        *_graphicsModule,
        *_resourceModule);
    _gameModule = std::make_unique<GameModule>(
        gameId,
        *_optionsView,
        *_resourceModule,
        *_graphicsModule,
        *_audioModule,
        *_sceneModule,
        *_scriptModule);
    _systemModule->init();
    _graphicsModule->init();
    _audioModule->init();
    _movieModule->init();
    _scriptModule->init();
    _resourceModule->init();
    _sceneModule->init();
    _guiModule->init();
    _gameModule->init();

    _services = std::make_unique<ServicesView>(
        _gameModule->services(),
        _movieModule->services(),
        _audioModule->services(),
        _graphicsModule->services(),
        _sceneModule->services(),
        _guiModule->services(),
        _scriptModule->services(),
        _resourceModule->services(),
        _systemModule->services());

    _profiler = std::make_unique<Profiler>(
        _options.graphics,
        _services->graphics,
        _services->resource,
        _services->system);
    _profiler->init();
    _profiler->reserveThread(
        kMainThreadName,
        {glm::vec3 {0.0f, 1.0f, 1.0f},
         glm::vec3 {0.0f, 1.0f, 0.0f},
         glm::vec3 {1.0f, 0.0f, 0.0f},
         glm::vec3 {1.0f, 1.0f, 0.0f}});

    _console = std::make_unique<Console>(
        _options.graphics,
        _services->graphics,
        _services->resource);
    _console->init();
    initAutomationCommands();

    _game = std::make_unique<Game>(
        gameId,
        _options.game.path,
        *_optionsView,
        *_services,
        *_console);
    _game->init();
}

void Engine::deinit() {
    _console.reset();
    _profiler.reset();
    _game.reset();
    _services.reset();

    _gameModule.reset();
    _guiModule.reset();
    _sceneModule.reset();
    _resourceModule.reset();
    _scriptModule.reset();
    _movieModule.reset();
    _audioModule.reset();
    _graphicsModule.reset();
    _systemModule.reset();
    _clock.reset();

    _optionsView.reset();
    _window.reset();

    SDL_Quit();
}

int Engine::run() {
    auto &clock = _services->system.clock;
    // Sampled in microseconds every frame below, so the baseline has to be
    // microseconds too. Seeding it from millis() made the first frame time
    // come out as very nearly the whole clock epoch - the counter runs from
    // system boot, so it was routinely days.
    _ticks = clock.micros();

    bool quit = false;
    while (!quit) {
        processEvents(quit);
        if (quit) {
            break;
        }
        if (!_window->isInFocus() && !_options.graphics.headless) {
            std::this_thread::sleep_for(std::chrono::milliseconds {100});
            continue;
        }
        uint64_t ticks = clock.micros();
        if (_game->consumeTimingDiscontinuity()) {
            // A synchronous load blocked somewhere in the last frame. Rebase
            // onto now so that interval is not charged to the world as elapsed
            // gameplay time: this frame opens a new epoch and starts at zero.
            _ticks = ticks;
        }
        auto frameTime = (ticks - _ticks) / 10e5f;
        _ticks = ticks;
        if (_options.graphics.headless) {
            frameTime = 1.0f / 60.0f;
        }
        processScriptedCommands(quit);
        if (quit) {
            break;
        }
        _profiler->measure(kMainThreadName, kProfilerInputTimeIndex, [this, &quit]() {
            while (!_events.empty()) {
                auto event = _events.front();
                _events.pop();
                if (_profiler->handle(event)) {
                    continue;
                }
                if (_console->handle(event)) {
                    continue;
                }
                if (_game->handle(event)) {
                    if (_game->isQuitRequested()) {
                        quit = true;
                        break;
                    }
                    continue;
                }
            }
        });
        if (quit) {
            break;
        }
        _profiler->measure(kMainThreadName, kProfilerUpdateTimeIndex, [this, &frameTime]() {
            imguiNewFrame();
            _game->update(frameTime);
            bool showcur = _game->cursorType() == CursorType::None;
            bool relmouse = _game->relativeMouseMode();
            showCursor(showcur);
            setRelativeMouseMode(relmouse);
            _profiler->update(frameTime);
        });
        _profiler->measure(kMainThreadName, kProfilerRenderGraphicsTimeIndex, [this, &quit]() {
            _services->graphics.statistic.resetDrawCalls();
            if (_options.graphics.pbr) {
                _services->graphics.pbrTextures.refresh();
            }
            _services->graphics.context.clearColorDepth();
            _game->render();
            _profiler->render();
            _console->render();
            imguiRender();
            captureIfRequested(quit);
            _window->swap();
        });
        _profiler->measure(kMainThreadName, kProfilerRenderAudioTimeIndex, [this]() {
            _services->audio.mixer.render();
        });
    }

    imguiShutdown();
    return 0;
}

void Engine::initAutomationCommands() {
    _console->registerCommand("pause", "pause command-file execution for a number of rendered frames", [this](const auto &args) {
        auto frames = args.template get<int>(1);
        if (!frames || *frames < 1) {
            throw std::invalid_argument("usage: pause <frames>, where frames is positive");
        }
        _scriptPauseFrames = *frames;
    });
    _console->registerCommand("capture", "capture one or more rendered frames", [this](const auto &args) {
        auto path = args[1];
        auto count = args.template get<int>(2).value_or(1);
        if (!path || count < 1) {
            throw std::invalid_argument("usage: capture <path> [frames], where frames is positive");
        }
        _captureRequest = CaptureRequest {std::filesystem::path(std::string(*path)), count, 0};
    });
    _console->registerCommand("quit", "end the engine run", [this](const auto &) {
        _scriptQuitRequested = true;
    });

    if (_options.commandsFile.empty()) {
        return;
    }
    std::ifstream file(_options.commandsFile);
    if (!file.good()) {
        throw std::runtime_error("Failed to open commands file: " + _options.commandsFile);
    }
    for (std::string line; std::getline(file, line);) {
        std::string_view command = string_strip(line);
        if (!command.empty()) {
            _scriptedCommands.emplace_back(command);
        }
    }
}

void Engine::processScriptedCommands(bool &quit) {
    if (_scriptQuitRequested) {
        quit = true;
        return;
    }
    if (_captureRequest) {
        return;
    }
    if (_scriptPauseFrames > 0) {
        --_scriptPauseFrames;
        if (_scriptPauseFrames > 0) {
            return;
        }
    }

    while (!_scriptedCommands.empty()) {
        std::string command(std::move(_scriptedCommands.front()));
        _scriptedCommands.pop_front();
        _console->execute(command);

        if (_scriptQuitRequested) {
            quit = true;
            return;
        }
        if (_captureRequest) {
            return;
        }
        if (_scriptPauseFrames > 0) {
            return;
        }
    }
}

void Engine::captureIfRequested(bool &quit) {
    if (_captureRequest) {
        captureFrame(numberedCapturePath(*_captureRequest));
        ++_captureRequest->index;
        if (_captureRequest->index >= _captureRequest->count) {
            _captureRequest.reset();
        }
    }
}

void Engine::captureFrame(const std::filesystem::path &path) {
    auto screenshot = _services->graphics.context.captureScreen(
        _options.graphics.width, _options.graphics.height);
    auto stream = FileOutputStream(path);
    TgaWriter(screenshot).save(stream);
    info("Wrote screenshot: " + path.string());
}

std::filesystem::path Engine::numberedCapturePath(const CaptureRequest &request) const {
    if (request.count == 1) {
        return request.path;
    }
    std::ostringstream suffix;
    suffix << '-' << std::setfill('0') << std::setw(4) << (request.index + 1);
    return request.path.parent_path() /
           (request.path.stem().string() + suffix.str() + request.path.extension().string());
}

void Engine::processEvents(bool &quit) {
    std::queue<input::Event> unhandled;
    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent)) {
        if (sdlEvent.type == SDL_EVENT_QUIT) {
            quit = true;
            break;
        }
        if (_options.graphics.headless) {
            if (_window->isAssociatedWith(sdlEvent) && _window->handle(sdlEvent) && _window->isCloseRequested()) {
                quit = true;
                break;
            }
            continue;
        }
        if (imguiHandle(sdlEvent)) {
            continue;
        }
        if (!_window->isAssociatedWith(sdlEvent)) {
            imguiHandle(sdlEvent);
            continue;
        }
        if (_window->handle(sdlEvent)) {
            if (_window->isCloseRequested()) {
                quit = true;
                break;
            }
            continue;
        }
        auto event = eventFromSDLEvent(sdlEvent);
        if (!event) {
            continue;
        }
        if (_profiler->handle(*event)) {
            continue;
        }
        unhandled.push(*event);
    }
    while (!unhandled.empty()) {
        _events.push(std::move(unhandled.front()));
        unhandled.pop();
    }
}

void Engine::showCursor(bool show) {
    if (_showCursor == show) {
        return;
    }
    if (show) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
    _showCursor = show;
}

void Engine::setRelativeMouseMode(bool relative) {
    if (_relativeMouseMode == relative) {
        return;
    }
    _window->setRelativeMouseMode(relative);
    _relativeMouseMode = relative;
}

static constexpr int scaleWinCoord(int coord, int winScale) {
    return coord * 100 / winScale;
}

std::optional<input::Event> Engine::eventFromSDLEvent(const SDL_Event &sdlEvent) const {
    switch (sdlEvent.type) {
    case SDL_EVENT_KEY_DOWN:
        return input::Event::newKeyDown(input::KeyEvent {
            sdlEvent.key.down,
            static_cast<input::KeyCode>(sdlEvent.key.key),
            sdlEvent.key.mod,
            sdlEvent.key.repeat});
    case SDL_EVENT_KEY_UP:
        return input::Event::newKeyUp(input::KeyEvent {
            sdlEvent.key.down,
            static_cast<input::KeyCode>(sdlEvent.key.key),
            sdlEvent.key.mod,
            sdlEvent.key.repeat});
    case SDL_EVENT_MOUSE_MOTION:
        return input::Event::newMouseMotion(input::MouseMotionEvent {
            scaleWinCoord(sdlEvent.motion.x, _options.graphics.winScale),
            scaleWinCoord(sdlEvent.motion.y, _options.graphics.winScale),
            sdlEvent.motion.xrel,
            sdlEvent.motion.yrel});
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return input::Event::newMouseButtonDown(input::MouseButtonEvent {
            static_cast<input::MouseButton>(sdlEvent.button.button),
            sdlEvent.button.down,
            sdlEvent.button.clicks,
            scaleWinCoord(sdlEvent.button.x, _options.graphics.winScale),
            scaleWinCoord(sdlEvent.button.y, _options.graphics.winScale)});
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return input::Event::newMouseButtonUp(input::MouseButtonEvent {
            static_cast<input::MouseButton>(sdlEvent.button.button),
            sdlEvent.button.down,
            sdlEvent.button.clicks,
            scaleWinCoord(sdlEvent.button.x, _options.graphics.winScale),
            scaleWinCoord(sdlEvent.button.y, _options.graphics.winScale)});
    case SDL_EVENT_MOUSE_WHEEL: {
        return input::Event::newMouseWheel(input::MouseWheelEvent {
            sdlEvent.wheel.x,
            sdlEvent.wheel.y,
            static_cast<input::MouseWheelDirection>(sdlEvent.wheel.direction)});
    default:
        return std::nullopt;
    }
    }
}

} // namespace reone
