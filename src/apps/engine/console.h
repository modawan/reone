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

#include "reone/game/console.h"
#include "reone/graphics/font.h"
#include "reone/graphics/options.h"
#include "reone/graphics/types.h"
#include "reone/gui/textinput.h"
#include "reone/input/event.h"
#include "reone/system/textbuffer.h"

namespace reone {

namespace graphics {

struct GraphicsOptions;
struct GraphicsServices;

} // namespace graphics

namespace resource {

struct ResourceServices;

}

class Console : public game::IConsole, boost::noncopyable {
public:
    Console(graphics::GraphicsOptions &graphicsOpt,
            graphics::GraphicsServices &graphicsSvc,
            resource::ResourceServices &resourceSvc) :
        _graphicsOpt(graphicsOpt),
        _graphicsSvc(graphicsSvc),
        _resourceSvc(resourceSvc),
        _input(_buffer, gui::TextInputFlags::console) {
        _input.setPasteAllowed(true);
    }

    ~Console() {
        deinit();
    }

    void init();
    void deinit();

    /**
     * Takes the console key (isConsoleKey) wherever it is pressed, opening or
     * closing the console, and while the console is open its input. Alt+`
     * runs the next batch of commands.
     *
     * @return whether the event was taken
     */
    bool handle(const input::Event &event);
    void render();
    bool isOpen() const { return _open; }

    void registerCommand(std::string name, std::string description, CommandHandler handler) override;
    void printLine(const std::string &text) override;

    void execute(std::string_view command);
    void executeNextBatch();

private:
    struct Command {
        std::string name;
        std::string description;
        CommandHandler handler;
    };

    graphics::GraphicsOptions &_graphicsOpt;
    graphics::GraphicsServices &_graphicsSvc;
    resource::ResourceServices &_resourceSvc;

    bool _inited {false};

    std::shared_ptr<graphics::Font> _font;
    bool _open {false};
    TextBuffer _buffer;
    size_t _inputOffset {0};
    gui::TextInput _input;
    size_t _scrollOffset {0};
    std::vector<std::string> _history;
    size_t _historyIndex {0};

    enum class State {
        Execute,
        Batch,
    };
    State _state {State::Execute};

    using Batch = std::vector<std::string>;
    std::deque<Batch> _batches;

    // Commands

    std::list<Command>
        _commands;
    std::unordered_map<std::string, std::reference_wrapper<Command>> _nameToCommand;

    // END Commands

    /**
     * Whether the event is the console key, which opens and closes the console
     * and is never typed, by the language of the talk table: Control+C in
     * Polish, otherwise its character typed, ° in German and ` in every other.
     */
    bool isConsoleKey(const input::Event &event) const;

    bool handleMouseWheel(const input::MouseWheelEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);

    void executeInputText();
    void setPrompt();

    void renderBackground();
    void renderLines();
};

} // namespace reone
