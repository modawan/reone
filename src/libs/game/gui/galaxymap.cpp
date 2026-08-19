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

#include "reone/game/gui/galaxymap.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graph.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/model.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

static constexpr char kSceneGalaxy[] = "galaxymap.galaxy";
static constexpr char kScenePlanet[] = "galaxymap.planet";

static constexpr char kGalaxyModelResRef[] = "galaxy";
static constexpr char kCameraHookNodeName[] = "camerahook";

// The panel's two displays are authored as perspective views through the
// camerahook of their own model, not as flat previews.
static constexpr float kDisplayVerticalFov = glm::radians(22.7259998f);
static constexpr float kDisplayClipPlaneNear = 0.1f;
static constexpr float kDisplayClipPlaneFar = 10000.0f;

/** One-shot intro the preview model plays whenever it becomes the destination. */
static constexpr char kPreviewAnimation[] = "zoomin";

static constexpr char kTravelScript[] = "k_sup_galaxymap";

void GalaxyMap::onGUILoaded() {
    _accept = findControl<gui::Button>("BTN_ACCEPT");
    _back = findControl<gui::Button>("BTN_BACK");
    _name = findControl<gui::Label>("LBL_PLANETNAME");
    _description = findControl<gui::Label>("LBL_DESC");
    _galaxyDisplay = findControl<gui::Label>("3D_PlanetDisplay");
    _planetDisplay = findControl<gui::Label>("3D_PlanetModel");

    if (_accept) {
        _accept->setOnClick([this]() { accept(); });
    }
    if (_back) {
        _back->setOnClick([this]() { close(); });
    }
    if (_planetDisplay) {
        // The display control authors placeholder text the preview replaces.
        _planetDisplay->setTextMessage("");
        _services.scene.graphs.reserve(kScenePlanet);
    }

    _planetary = _services.resource.twoDas.get("planetary");
    bindPlanetControls();
    setupGalaxyDisplay();
}

void GalaxyMap::bindPlanetControls() {
    if (!_planetary) {
        return;
    }
    // A planetary row that names no control, or names one this panel does not
    // author, simply has no destination on the map.
    for (int row = 0; row < _planetary->getRowCount(); ++row) {
        auto tag = _planetary->getString(row, "guitag");
        if (tag.empty()) {
            continue;
        }
        auto control = std::dynamic_pointer_cast<gui::Button>(_gui->findControl(tag));
        if (!control) {
            continue;
        }
        _planetControls.emplace(row, control);
        control->setOnClick([this, row]() { select(row); });
    }
}

void GalaxyMap::setupGalaxyDisplay() {
    if (!_galaxyDisplay) {
        return;
    }
    _services.scene.graphs.reserve(kSceneGalaxy);
    auto &sceneGraph = _services.scene.graphs.get(kSceneGalaxy);
    const auto &extent = _galaxyDisplay->extent();
    float aspect = extent.width / static_cast<float>(extent.height);
    gui::SceneInitializer(sceneGraph)
        .aspect(aspect)
        .depth(kDisplayClipPlaneNear, kDisplayClipPlaneFar)
        .perspective(kDisplayVerticalFov)
        // The galaxy is a continuous field the player is dropped straight into,
        // so it opens already populated rather than filling in over its
        // particles' lifetimes.
        .prewarmEmitters()
        .modelSupplier(std::bind(&GalaxyMap::getGalaxyModel, this, std::placeholders::_1))
        .cameraFromModelNode(kCameraHookNodeName)
        .invoke();
    if (_galaxyModelNode) {
        _galaxyDisplay->setSceneName(kSceneGalaxy);
    }
}

std::shared_ptr<scene::ModelSceneNode> GalaxyMap::getGalaxyModel(scene::ISceneGraph &sceneGraph) {
    auto model = _services.resource.models.get(kGalaxyModelResRef);
    if (!model) {
        return nullptr;
    }
    _galaxyModelNode = sceneGraph.newModel(*model, scene::ModelUsage::GUI);
    return _galaxyModelNode;
}

void GalaxyMap::prepare() {
    _accepting = false;
    _hoveredPlanet = -1;
    for (auto &[row, control] : _planetControls) {
        bool available = _game.party().galaxyMap().available(row);
        control->setVisible(available);
        control->setDisabled(!available || !_game.party().galaxyMap().selectable(row));
    }
    refreshPresentation();
}

std::string GalaxyMap::planetModelResRef(int row) const {
    if (!_planetary || row < 0 || row >= _planetary->getRowCount()) {
        return "";
    }
    return _planetary->getString(row, "model");
}

void GalaxyMap::showPlanetPreview(scene::ModelSceneNode &node) {
    // The intro is a one-shot animation, so a preview that is already carrying
    // it has to be told to start over rather than merely asked to play again.
    if (!node.restartAnimation(kPreviewAnimation)) {
        node.playAnimation(kPreviewAnimation);
    }
}

void GalaxyMap::refreshPlanetModel(int row) {
    if (!_planetDisplay) {
        return;
    }

    auto resRef = planetModelResRef(row);
    if (_planetModelNode && resRef == _planetModelResRef) {
        showPlanetPreview(*_planetModelNode);
        return;
    }

    _planetModelNode.reset();
    _planetModelResRef.clear();
    _planetDisplay->setSceneName("");
    if (resRef.empty()) {
        return;
    }

    auto &sceneGraph = _services.scene.graphs.get(kScenePlanet);
    auto cached = _planetModelNodes.find(resRef);
    if (cached != _planetModelNodes.end()) {
        _planetModelNode = cached->second;
    } else {
        auto model = _services.resource.models.get(resRef);
        if (!model) {
            return;
        }
        _planetModelNode = sceneGraph.newModel(*model, scene::ModelUsage::GUI);
        _planetModelNodes.emplace(resRef, _planetModelNode);
    }
    _planetModelResRef = resRef;

    detachCameras(*_planetModelNode);

    const auto &extent = _planetDisplay->extent();
    float aspect = extent.width / static_cast<float>(extent.height);
    auto modelNode = _planetModelNode;
    gui::SceneInitializer(sceneGraph)
        .aspect(aspect)
        .depth(kDisplayClipPlaneNear, kDisplayClipPlaneFar)
        .perspective(kDisplayVerticalFov)
        .modelSupplier([modelNode](scene::ISceneGraph &) { return modelNode; })
        .cameraFromModelNode(kCameraHookNodeName)
        .invoke();

    showPlanetPreview(*_planetModelNode);
    _planetDisplay->setSceneName(kScenePlanet);
}

void GalaxyMap::detachCameras(scene::ModelSceneNode &model) {
    // Initializing a scene parents its camera to the model's hook, so a preview
    // taken from the cache is still carrying the camera from the last time it
    // was shown. Left in place, revisiting a planet would hang another camera
    // off the same hook every time.
    auto hook = model.getNodeByName(kCameraHookNodeName);
    if (!hook) {
        return;
    }
    std::vector<scene::SceneNode *> cameras;
    for (auto *child : hook->children()) {
        if (child->type() == scene::SceneNodeType::Camera) {
            cameras.push_back(child);
        }
    }
    for (auto *camera : cameras) {
        hook->removeChild(*camera);
    }
}

void GalaxyMap::select(int row) {
    auto &galaxyMap = _game.party().galaxyMap();
    if (!galaxyMap.selectable(row) || !galaxyMap.trySelectPlanet(row)) {
        return;
    }
    refreshPresentation();
}

void GalaxyMap::refreshPresentation() {
    int row = _game.party().galaxyMap().selectedPlanet();

    if (_accept) {
        _accept->setDisabled(!isTravelDestination(row));
    }
    updatePlanetControlPresentation();
    refreshPlanetModel(row);

    if (!_planetary || row < 0 || row >= _planetary->getRowCount()) {
        if (_name) {
            _name->setTextMessage("");
        }
        if (_description) {
            _description->setTextMessage("");
        }
        return;
    }
    int nameRef = _planetary->getInt(row, "name", -1);
    int descriptionRef = _planetary->getInt(row, "description", -1);
    if (_name) {
        _name->setTextMessage(nameRef >= 0 ? _services.resource.strings.getText(nameRef) : "");
    }
    if (_description) {
        _description->setTextMessage(descriptionRef >= 0 ? _services.resource.strings.getText(descriptionRef) : "");
    }
}

bool GalaxyMap::isTravelDestination(int row) const {
    const auto &galaxyMap = _game.party().galaxyMap();
    return _planetControls.count(row) > 0 && galaxyMap.available(row) && galaxyMap.selectable(row);
}

void GalaxyMap::updatePlanetControlPresentation() {
    int selectedPlanet = _game.party().galaxyMap().selectedPlanet();
    // The chosen destination keeps the authored HILIGHT state the panel gives a
    // control under the cursor, so it stays visibly picked while the cursor
    // wanders over the other planets.
    for (auto &[row, control] : _planetControls) {
        control->setSelected(row == selectedPlanet || row == _hoveredPlanet);
    }
}

void GalaxyMap::onSelectionChanged(const std::string &control, bool selected) {
    GameGUI::onSelectionChanged(control, selected);

    auto hovered = std::find_if(_planetControls.begin(), _planetControls.end(), [&control](const auto &entry) {
        return entry.second->tag() == control;
    });
    if (hovered == _planetControls.end()) {
        return;
    }
    if (selected) {
        _hoveredPlanet = hovered->first;
    } else if (_hoveredPlanet == hovered->first) {
        _hoveredPlanet = -1;
    }
    updatePlanetControlPresentation();
}

bool GalaxyMap::handle(const input::Event &event) {
    if (event.type == input::EventType::KeyDown &&
        !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        close();
        return true;
    }
    return GameGUI::handle(event);
}

void GalaxyMap::close() {
    // Leaving the map keeps whatever destination was picked: the selection is
    // party state, not something this panel owns until it is accepted.
    _game.openInGame();
}

void GalaxyMap::accept() {
    if (_accepting) {
        return;
    }
    int row = _game.party().galaxyMap().selectedPlanet();
    if (!isTravelDestination(row)) {
        return;
    }
    _accepting = true;
    close();
    try {
        _game.scriptRunner().run(kTravelScript);
    } catch (const std::exception &e) {
        error(str(boost::format("Galaxy map: %s failed: %s") % kTravelScript % std::string(e.what())));
    }
}

} // namespace game

} // namespace reone
