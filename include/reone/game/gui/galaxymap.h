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

#include "../gui.h"

namespace reone {

namespace resource {
class TwoDA;
}

namespace gui {

class Button;
class Label;

} // namespace gui

namespace scene {

class ISceneGraph;
class ModelSceneNode;

} // namespace scene

namespace game {

class GalaxyMapTestAccess;

/**
 * The galaxy map panel: the party picks its next destination here, and
 * accepting one hands over to the travel script.
 *
 * Everything the panel shows comes from planetary.2da. A row names the GUI
 * control that stands for the planet, the strings describing it, and the model
 * shown in the lower preview.
 */
class GalaxyMap : public GameGUI {
public:
    GalaxyMap(Game &game, ServicesView &services) :
        GameGUI(game, services) {
        _resRef = "galaxymap";
    }

    /** Bring the panel in line with the current planet state before it opens. */
    void prepare();

    bool handle(const input::Event &event) override;

private:
    friend class GalaxyMapTestAccess;

    std::shared_ptr<resource::TwoDA> _planetary;

    std::shared_ptr<gui::Button> _accept;
    std::shared_ptr<gui::Button> _back;
    std::shared_ptr<gui::Label> _name;
    std::shared_ptr<gui::Label> _description;
    std::shared_ptr<gui::Label> _galaxyDisplay;
    std::shared_ptr<gui::Label> _planetDisplay;

    /** Planet control per planetary.2da row, for the rows the panel authors one. */
    std::map<int, std::shared_ptr<gui::Button>> _planetControls;

    std::shared_ptr<scene::ModelSceneNode> _galaxyModelNode;
    std::shared_ptr<scene::ModelSceneNode> _planetModelNode;
    /** Preview models are kept once loaded, so revisiting a planet is free. */
    std::map<std::string, std::shared_ptr<scene::ModelSceneNode>> _planetModelNodes;
    std::string _planetModelResRef;

    int _hoveredPlanet {-1};
    bool _accepting {false};

    void onGUILoaded() override;
    void onSelectionChanged(const std::string &control, bool selected) override;

    void bindPlanetControls();
    void setupGalaxyDisplay();
    std::shared_ptr<scene::ModelSceneNode> getGalaxyModel(scene::ISceneGraph &sceneGraph);

    std::string planetModelResRef(int row) const;
    void refreshPlanetModel(int row);
    void showPlanetPreview(scene::ModelSceneNode &node);
    void detachCameras(scene::ModelSceneNode &model);

    bool isTravelDestination(int row) const;

    void select(int row);
    void refreshPresentation();
    void updatePlanetControlPresentation();

    void close();
    void accept();
};

} // namespace game

} // namespace reone
