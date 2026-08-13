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

#include "reone/gui/control/iconchain.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/control/label.h"
#include "reone/gui/gui.h"

namespace reone {

namespace game {

class Game;
struct ServicesView;

/** Columns in the feat and power selection grids. */
constexpr int kIconSelectionColumnCount = 3;

struct IconSelectionCallbacks {
    std::function<void(const std::string &)> onItemFocus;
    std::function<void()> onItemFocusCleared;
    std::function<void(const std::string &)> onItemDoubleClick;
};

/**
 * Builds the icon grid shared by the feat and power selection screens on top
 * of the authored list box: same geometry, cell art and palette in both.
 */
std::shared_ptr<gui::IconChain> addIconSelectionChain(
    gui::IGUI &gui,
    Game &game,
    ServicesView &services,
    std::string tag,
    gui::ListBox &sourceList,
    const glm::vec3 &hilightColor,
    IconSelectionCallbacks callbacks);

/**
 * Applies TSL's shared chargen description treatment: the description box's
 * right edge aligns with the remaining-selections bar's. K1 authors these
 * panels in separate columns and keeps its original description width.
 */
void styleChargenTitles(
    Game &game,
    gui::Label &remainingLabel,
    gui::ListBox &descriptionBox);

} // namespace game

} // namespace reone
