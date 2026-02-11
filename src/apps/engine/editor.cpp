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

#include "editor.h"
#include "engine.h"

#include "imgui.h"

namespace reone {

// Editor::handle should take priority over ImGui event processing, so it close
// ImGui when it is in focus.
bool Editor::handle(const input::Event &event) {
    if (event.type != input::EventType::KeyUp ||
        event.key.code != input::KeyCode::F1) {
        return false;
    }
    _enabled ^= 1;
    return true;
}

void Editor::twoDa() {
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("2DA", &_showTwoDa, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    ImGui::Text("dear imgui says hello! (%s) (%d)", IMGUI_VERSION, IMGUI_VERSION_NUM);

    ImGui::End();
}

void Editor::update(float dt) {
    if (!_enabled) {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();

    if (!ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Tools")) {
            ImGui::MenuItem("2DA", nullptr, &_showTwoDa);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::MenuItem("ImGui Item Picker")) {
                ImGui::DebugStartItemPicker();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();

    if (_showTwoDa) {
        twoDa();
    }
}

void Editor::render() {
}

} // namespace reone
