/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/resource/id.h"

#include <map>
#include <string>
#include <vector>

namespace reone {

namespace input {
struct Event;
}

class Engine;

// 2DA Table windows
class TableFilter {
public:
    void setupRowIndexFilter();
    void setupColumnFilters();

    bool nextRow();
    bool nextColumn(std::string_view column);

    void setNumColumns(size_t num) { _columnFilters.resize(num); }

private:
    std::string _rowIndexStr;
    std::optional<size_t> _rowIndex;
    std::vector<std::string> _columnFilters;

    size_t _nextRowIndex {0};
    size_t _nextColumnIndex {0};
};

class Editor {
public:
    Editor(Engine &engine) :
        _engine(engine) {};
    bool handle(const input::Event &event);
    void update(float dt);
    void render();

private:
    // 2DA List window
    void twoDa();
    bool _showTwoDa {false};

    struct TwoDaTableContext {
        bool show {false};
        TableFilter filter;
    };

    void twoDaRes(const resource::ResourceId &res, TwoDaTableContext &context);
    std::map<resource::ResourceId, TwoDaTableContext> _twoDaContext;

    // DearImGui Demo showcasing different widgets.
    void imGuiDemo();
    bool _showImGuiDemo {false};

private:
    Engine &_engine;
    bool _enabled {false};
};

} // namespace reone
