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

#include "reone/game/camerastyles.h"

#include <algorithm>

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

using namespace reone::resource;

namespace reone {

namespace game {

void CameraStyles::init() {
    std::shared_ptr<TwoDA> twoDa(_twoDas.get("camerastyle"));
    if (!twoDa) {
        return;
    }
    // KotOR's table gives the free-look speeds in fl_tiltspeed and
    // fl_rotatespeed; TSL's gives them for the player character, in
    // fl_pc_tiltspeed and fl_pc_rotatespeed.
    const auto &columns = twoDa->columns();
    bool playerSpeeds = std::find(columns.begin(), columns.end(), "fl_pc_tiltspeed") != columns.end();
    auto tiltSpeedColumn = playerSpeeds ? "fl_pc_tiltspeed" : "fl_tiltspeed";
    auto rotateSpeedColumn = playerSpeeds ? "fl_pc_rotatespeed" : "fl_rotatespeed";
    for (int row = 0; row < twoDa->getRowCount(); ++row) {
        auto style = std::make_shared<CameraStyle>();
        style->name = twoDa->getString(row, "name");
        style->distance = twoDa->getFloat(row, "distance");
        style->pitch = twoDa->getFloat(row, "pitch");
        style->viewAngle = twoDa->getFloat(row, "viewangle");
        style->height = twoDa->getFloat(row, "height");
        style->freeLookTiltSpeed = twoDa->getFloat(row, tiltSpeedColumn);
        style->freeLookRotateSpeed = twoDa->getFloat(row, rotateSpeedColumn);
        style->freeLookUp = twoDa->getFloat(row, "fl_lookup");
        style->freeLookDown = twoDa->getFloat(row, "fl_lookdown");
        _styles.push_back(std::move(style));
    }
}

std::shared_ptr<CameraStyle> CameraStyles::get(int index) const {
    // A row outside the table has no style.
    return index >= 0 && index < static_cast<int>(_styles.size()) ? _styles[index] : nullptr;
}

std::shared_ptr<CameraStyle> CameraStyles::get(const std::string &name) const {
    for (auto &style : _styles) {
        if (style->name == name) {
            return style;
        }
    }
    return nullptr;
}

} // namespace game

} // namespace reone
