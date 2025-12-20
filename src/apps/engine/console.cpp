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

#include "console.h"

#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/fonts.h"
#include "reone/system/checkutil.h"
#include "reone/system/stringutil.h"

using namespace reone::graphics;

namespace reone {

static constexpr int kVisibleLineCount = 15;
static constexpr float kTextOffset = 3.0f;

void Console::init() {
    checkThat(!_inited, "Must not be initialized");
    _font = _resourceSvc.fonts.get("fnt_console");
    setPrompt();

    registerCommand("clear", "", [this](const auto &tokens) {
        _buffer.clear();
        _scrollOffset = 0;
        setPrompt();
    });
    registerCommand("help", "", [this](const auto &tokens) {
        printLine("Available commands:");
        for (auto &command : _commands) {
            if (!command.description.empty()) {
                printLine(str(boost::format("  %s: %s") % command.name % command.description));
            } else {
                printLine("  " + command.name);
            }
        }
    });

    _inited = true;
}

void Console::deinit() {
    if (!_inited) {
        return;
    }
    _font.reset();
    _inited = false;
}

void Console::registerCommand(std::string name, std::string description, CommandHandler handler) {
    Command command;
    command.name = std::move(name);
    command.description = std::move(description);
    command.handler = std::move(handler);
    _commands.push_back(std::move(command));

    auto &ref = _commands.back();
    _nameToCommand.insert({ref.name, ref});
}

void Console::setPrompt() {
    _buffer.seekEnd(0);
    _buffer.write("> ");
    _inputOffset = _buffer.tell();
    _input.setMinOffset(_inputOffset);
}

void Console::printLine(const std::string &text) {
    _buffer.write(text);
    _buffer.write('\n');
    _scrollOffset = 0;
}

bool Console::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyUp && (event.key.code == input::KeyCode::Backquote)) {
        _open = !_open;
        return true;
    }

    if (!_open) {
        return false;
    }

    if (_input.handle(event)) {
        return true;
    }
    switch (event.type) {
    case input::EventType::MouseWheel:
        return handleMouseWheel(event.wheel);
    case input::EventType::KeyUp:
        return handleKeyUp(event.key);
    default:
        return false;
    }
}

bool Console::handleMouseWheel(const input::MouseWheelEvent &event) {
    size_t orig = _buffer.tell();
    if (_scrollOffset) {
        _buffer.seekSet(_scrollOffset);
    } else {
        _buffer.seekEnd(0);
    }

    bool up = event.y < 0;
    if (up) {
        _buffer.seekCur(1);
        _buffer.search("\n");
    } else {
        _buffer.rsearch("\n");
    }

    // Stop scrolling at the beginning of the first line
    if (_buffer.tell() == 0) {
        _buffer.seekSet(orig);
        return true;
    }

    _scrollOffset = _buffer.tell();
    _buffer.seekSet(orig);

    // Disable scroll offset once we get back to the input line
    if (_scrollOffset == _buffer.tell()) {
        _scrollOffset = 0;
    }

    return true;
}

bool Console::handleKeyUp(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::Return: {
        _buffer.write('\n');
        executeInputText();
        return true;
    }
    case input::KeyCode::Up: {
        if (_history.empty()) {
            return true;
        }
        if (_historyIndex != 0) {
            --_historyIndex;
            _input.setText(_history[_historyIndex]);
        }
        return true;
    }
    case input::KeyCode::Down: {
        if (_history.empty()) {
            return true;
        }
        if (_historyIndex < (_history.size() - 1)) {
            ++_historyIndex;
            _input.setText(_history[_historyIndex]);
        } else {
            _input.clear();
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

void Console::executeInputText() {
    _buffer.seekEnd(0);
    size_t cmdBegin = _inputOffset;
    size_t cmdEnd = _buffer.tell();
    size_t cmdSize = cmdEnd - cmdBegin;

    std::string multiline(string_strip(_buffer.str().substr(cmdBegin, cmdSize)));
    if (!multiline.empty()) {
        _history.push_back(multiline);
        _historyIndex = _history.size();
    }

    while (cmdBegin < cmdEnd) {
        _buffer.seekSet(cmdBegin);
        std::string_view line = _buffer.readline();
        cmdBegin = _buffer.tell();

        assert(!line.empty() && "missing \n terminator");
        line = string_strip(line);
        if (line.empty()) {
            continue;
        }

        _buffer.seekEnd(0);
        execute(line);
    }
    setPrompt();
}

void Console::execute(std::string_view command) {
    game::ConsoleArgs::TokenList tokens;
    boost::split(tokens, command, boost::is_space(), boost::token_compress_on);
    if (tokens.empty()) {
        return;
    }

    auto commandByName = _nameToCommand.find(tokens[0]);
    if (commandByName == _nameToCommand.end()) {
        printLine("Unrecognized command: " + tokens[0]);
        return;
    }
    auto &handler = commandByName->second.get().handler;
    try {
        handler(std::move(tokens));
    } catch (const std::exception &ex) {
        printLine("Command failed: " + std::string(ex.what()));
    }
}

void Console::render() {
    if (!_open) {
        return;
    }
    _graphicsSvc.context.withBlendMode(BlendMode::Normal, [this]() {
        renderBackground();
        renderLines();
    });
}

void Console::renderBackground() {
    float height = kVisibleLineCount * _font->height();
    _graphicsSvc.uniforms.setGlobals([this](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f, static_cast<float>(_graphicsOpt.width),
            static_cast<float>(_graphicsOpt.height), 0.0f,
            0.0f, 100.0f);
    });
    auto transform = glm::scale(
        glm::translate(glm::vec3 {0.0f, _graphicsOpt.height - height, 0.0f}),
        glm::vec3 {_graphicsOpt.width, height, 1.0f});
    _graphicsSvc.uniforms.setLocals([this, transform](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
        locals.color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        locals.color.a = 0.5f;
    });
    _graphicsSvc.context.useProgram(_graphicsSvc.shaderRegistry.get(ShaderProgramId::mvpColor));
    _graphicsSvc.meshRegistry.get(MeshName::quad).draw(_graphicsSvc.statistic);
}

void Console::renderLines() {
    if (_buffer.empty()) {
        return;
    }

    size_t cursor = _buffer.tell();
    if (_scrollOffset) {
        _buffer.seekSet(_scrollOffset);
    } else {
        _buffer.seekEnd(0);
    }

    float height = kVisibleLineCount * _font->height();
    glm::vec3 position {kTextOffset, _graphicsOpt.height - 0.5f * _font->height(), 0.0f};
    int lineNo = 0;

    // Render an empty line at the end of the buffer, becase readlineReverse is
    // going to skip past it.
    _buffer.seekCur(-1);
    if (_buffer.read() == '\n') {
        position.y -= _font->height();
        ++lineNo;
    }

    for (; lineNo < kVisibleLineCount; ++lineNo) {
        std::string_view line = _buffer.readlineReverse();
        if (line.empty()) {
            break;
        }
        line = string_strip(line);
        _font->render(line, position, glm::vec3(1.0f), TextGravity::RightCenter);
        position.y -= _font->height();
    }
    _buffer.seekSet(cursor);
}

} // namespace reone
