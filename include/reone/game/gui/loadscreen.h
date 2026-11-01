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

#include "reone/gui/control/label.h"
#include "reone/gui/control/progressbar.h"

#include "../gui.h"

namespace reone {

namespace resource {
class TwoDA;
}

namespace game {

class LoadingScreen : public GameGUI {
public:
    LoadingScreen(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = guiResRef("loadscreen");
    }

    /**
     * Shows the loading picture of the module named \p moduleName: the one its
     * row of the loading-screen table names, else its own load_ picture when
     * there is one, else the one the table's default row names.
     */
    void setModuleImage(const std::string &moduleName);
    /**
     * The music the loading screen of the module named \p moduleName plays:
     * the one its row of the loading-screen table names, else its own load_
     * track when there is one, else the one the table's default row names.
     */
    std::string musicResRef(const std::string &moduleName) const;
    void setProgress(int progress);

private:
    struct Controls {
        std::shared_ptr<gui::Label> LBL_HINT;
        std::shared_ptr<gui::Label> LBL_LOADING;
        std::shared_ptr<gui::Label> LBL_LOGO;
        std::shared_ptr<gui::ProgressBar> PB_PROGRESS;
    };

    Controls _controls;
    /** The loading-screen table, read when the screen is loaded. */
    std::shared_ptr<resource::TwoDA> _loadScreens;

protected:
    void preload(gui::IGUI &gui) override;

private:
    void onGUILoaded() override;

    void bindControls() {
        _controls.LBL_HINT = findControl<gui::Label>("LBL_HINT");
        _controls.LBL_LOADING = findControl<gui::Label>("LBL_LOADING");
        _controls.LBL_LOGO = findControl<gui::Label>("LBL_LOGO");
        _controls.PB_PROGRESS = findControl<gui::ProgressBar>("PB_PROGRESS");
    }
};

} // namespace game

} // namespace reone
