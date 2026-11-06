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

#include "reone/game/gui/loadscreen.h"

#include "reone/game/game.h"
#include "reone/game/twodautil.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/resources.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

void LoadingScreen::preload(IGUI &gui) {
    GameGUI::preload(gui);
    if (_game.isTSL()) {
        gui.setResolution(800, 600);
    }
}

void LoadingScreen::onGUILoaded() {
    if (!_game.isTSL()) {
        loadBackground(BackgroundType::Load);
    }

    bindControls();

    _controls.LBL_HINT->setTextMessage("");
    _loadScreens = getRequiredTwoDA(_services.resource.twoDas, "loadscreens");
}

void LoadingScreen::setModuleImage(const std::string &moduleName) {
    static constexpr char kImageColumn[] = "bmpresref";
    static constexpr char kDefaultRow[] = "default";
    std::string image;
    const int row = _loadScreens->indexByLabel(moduleName);
    if (auto named = row != -1 ? _loadScreens->getStringOpt(row, kImageColumn) : std::nullopt) {
        image = *named;
    } else if (const std::string own = "load_" + moduleName;
               _services.resource.resources.find(ResourceId(own, ResType::Tga)) ||
               _services.resource.resources.find(ResourceId(own, ResType::Tpc))) {
        image = own;
    } else {
        image = _loadScreens->getString(_loadScreens->indexByLabel(kDefaultRow), kImageColumn);
    }
    _gui->rootControl().setBorderFill(image);
}

// The music of the loading-screen table's row named by the module; failing
// that, load_<module> when that MP3 exists; else the default row's. No module
// name gives no music.
std::string LoadingScreen::musicResRef(const std::string &moduleName) const {
    static constexpr char kMusicColumn[] = "musicresref";
    static constexpr char kDefaultRow[] = "default";
    if (moduleName.empty()) {
        return "";
    }
    const int row = _loadScreens->indexByLabel(moduleName);
    if (auto named = row != -1 ? _loadScreens->getStringOpt(row, kMusicColumn) : std::nullopt) {
        return *named;
    }
    if (const std::string own = "load_" + moduleName;
        _services.resource.audioClips.hasMp3(own)) {
        return own;
    }
    const int defaultRow = _loadScreens->indexByLabel(kDefaultRow);
    return defaultRow != -1 ? _loadScreens->getStringOpt(defaultRow, kMusicColumn).value_or("") : "";
}

void LoadingScreen::setProgress(int progress) {
    _controls.PB_PROGRESS->setValue(progress);
}

} // namespace game

} // namespace reone
