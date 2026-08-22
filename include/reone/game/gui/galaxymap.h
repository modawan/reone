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

#include "confirmpopup.h"

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
 *
 * K2 authors its own panel and adds two rules of its own: a destination can be
 * locked out with a reason to show for it, and accepting the location the party
 * is already at says so instead of travelling.
 */
class GalaxyMap : public GameGUI {
public:
    GalaxyMap(Game &game, ServicesView &services);

    /** Bring the panel in line with the current planet state before it opens. */
    void prepare(int initialPlanet = -1);

    /** Move the choice to the next or previous planet the party can reach. */
    void selectNextDestination() { selectAdjacentDestination(1); }
    void selectPreviousDestination() { selectAdjacentDestination(-1); }

    /** Whether the travel script this panel dispatched is still running. */
    bool isRunningTravelScript() const { return _runningTravelScript; }

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;

private:
    friend class GalaxyMapTestAccess;

    /** What pressing Accept on the current choice comes to. */
    enum class AcceptOutcome {
        Nothing,
        Message,
        Travel
    };

    struct AcceptDecision {
        AcceptOutcome outcome {AcceptOutcome::Nothing};
        /** String to show when the outcome is a message, -1 when there is none. */
        int strRef {-1};
    };

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

    /** Where the party was when the panel opened, for K2 location checking. */
    int _locationAtOpen {-1};
    bool _runningTravelScript {false};
    std::unique_ptr<ConfirmPopup> _popup;

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
    bool isSelectableOnMap(int row) const;
    int lockedOutReason(int row) const;

    void applyOpeningSelection(int initialPlanet);
    int adjacentDestination(int from, int step) const;
    void selectAdjacentDestination(int step);

    AcceptDecision decideAccept() const;
    void showMessage(int strRef);

    uint32_t travelScriptCaller() const;
    void runTravelScript();

    void select(int row);
    void refreshPresentation();
    void updatePlanetControlPresentation();

    void close();
    void accept();
};

} // namespace game

} // namespace reone
