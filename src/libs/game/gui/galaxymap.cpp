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
#include "reone/script/types.h"
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

/** "You are already at that location." */
static constexpr int kAlreadyAtLocationStrRef = 125629;

namespace {

/** Holds a flag raised for as long as the call it scopes is on the stack. */
class ScopedFlag : boost::noncopyable {
public:
    ScopedFlag(bool &flag) :
        _flag(flag) {
        _flag = true;
    }

    ~ScopedFlag() {
        _flag = false;
    }

private:
    bool &_flag;
};

} // namespace

GalaxyMap::GalaxyMap(Game &game, ServicesView &services) :
    GameGUI(game, services) {

    // The two titles author separate panels; K2 has no galaxymap.
    _resRef = game.isTSL() ? "galaxymap_p" : "galaxymap";
}

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

void GalaxyMap::prepare(int initialPlanet) {
    _accepting = false;
    _hoveredPlanet = -1;
    if (_popup) {
        _popup->hide();
    }
    if (_game.isTSL()) {
        // Current location checking and the opening fallback are K2 rules; K1
        // opens on whatever the routine and the party state already say.
        applyOpeningSelection(initialPlanet);
    } else {
        _game.party().galaxyMap().trySelectPlanet(initialPlanet);
    }
    for (auto &[row, control] : _planetControls) {
        control->setVisible(_game.party().galaxyMap().available(row));
        control->setDisabled(!isSelectableOnMap(row));
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

void GalaxyMap::applyOpeningSelection(int initialPlanet) {
    auto &galaxyMap = _game.party().galaxyMap();
    // The planet the routine opens on is where the party physically is. That
    // is a different thing from the selection, which is the destination last
    // chosen and may name somewhere the party has never been.
    _locationAtOpen = initialPlanet;
    if (galaxyMap.available(galaxyMap.selectedPlanet())) {
        // A destination that is still on the map stands, whatever the routine
        // opened on.
        return;
    }
    // Nothing usable is selected. The party's own location is the first thing
    // to fall back to, then somewhere it can actually go.
    if (galaxyMap.available(initialPlanet) && galaxyMap.trySelectPlanet(initialPlanet)) {
        return;
    }
    for (int row = 0; row < galaxyMap.rowCount(); ++row) {
        if (galaxyMap.available(row) && galaxyMap.selectable(row)) {
            galaxyMap.trySelectPlanet(row);
            return;
        }
    }
    for (int row = 0; row < galaxyMap.rowCount(); ++row) {
        if (galaxyMap.available(row)) {
            galaxyMap.trySelectPlanet(row);
            return;
        }
    }
    // With no planet available at all there is nothing to fall back to.
}

int GalaxyMap::adjacentDestination(int from, int step) const {
    const auto &galaxyMap = _game.party().galaxyMap();
    int rowCount = galaxyMap.rowCount();
    if (rowCount <= 0) {
        return from;
    }
    // Every planet on the map is a stop, including the ones travel is locked
    // out of, and the walk wraps around the ends.
    for (int offset = 1; offset <= rowCount; ++offset) {
        int row = ((from + step * offset) % rowCount + rowCount) % rowCount;
        if (galaxyMap.available(row)) {
            return row;
        }
    }
    return from;
}

void GalaxyMap::selectAdjacentDestination(int step) {
    auto &galaxyMap = _game.party().galaxyMap();
    int row = adjacentDestination(galaxyMap.selectedPlanet(), step);
    if (row == galaxyMap.selectedPlanet() || !galaxyMap.trySelectPlanet(row)) {
        return;
    }
    refreshPresentation();
}

bool GalaxyMap::isSelectableOnMap(int row) const {
    const auto &galaxyMap = _game.party().galaxyMap();
    if (!galaxyMap.available(row)) {
        return false;
    }
    // Every planet on the K2 map can be picked, including the ones travel is
    // locked out of: picking one is how the player is told why. K1 authors no
    // reason to give, so there a planet it cannot travel to stays inert.
    return _game.isTSL() || galaxyMap.selectable(row);
}

void GalaxyMap::select(int row) {
    if (!isSelectableOnMap(row) || !_game.party().galaxyMap().trySelectPlanet(row)) {
        return;
    }
    refreshPresentation();
}

void GalaxyMap::refreshPresentation() {
    int row = _game.party().galaxyMap().selectedPlanet();

    if (_accept) {
        // Accept is live whenever pressing it has something to say. On K2 that
        // takes in a locked out planet and the party's own location, since the
        // message each of those gives is the whole point of pressing it.
        _accept->setDisabled(decideAccept().outcome == AcceptOutcome::Nothing);
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

int GalaxyMap::lockedOutReason(int row) const {
    // K1 planetary.2da has no locked out column, so a row never carries one.
    if (!_planetary || row < 0 || row >= _planetary->getRowCount()) {
        return -1;
    }
    return _planetary->getInt(row, "lockedoutreason", -1);
}

void GalaxyMap::showMessage(int strRef) {
    if (strRef < 0) {
        return;
    }
    auto text = _services.resource.strings.getText(strRef);
    if (text.empty()) {
        return;
    }
    if (!_popup) {
        auto popup = std::make_unique<ConfirmPopup>(_game, _services);
        try {
            popup->init();
        } catch (const std::exception &e) {
            error("Galaxy map: unable to load the message popup: " + std::string(e.what()));
            return;
        }
        _popup = std::move(popup);
    }
    _popup->show(text);
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
    if (_popup && _popup->isVisible()) {
        // The message sits over the map and takes every event until dismissed.
        if (event.type == input::EventType::KeyDown &&
            !event.key.repeat &&
            event.key.code == input::KeyCode::Escape) {
            _popup->hide();
            return true;
        }
        _popup->handle(event);
        return true;
    }
    if (event.type == input::EventType::KeyDown &&
        !event.key.repeat &&
        event.key.code == input::KeyCode::Escape) {
        close();
        return true;
    }
    return GameGUI::handle(event);
}

void GalaxyMap::update(float dt) {
    GameGUI::update(dt);
    if (_popup && _popup->isVisible()) {
        _popup->update(dt);
    }
}

void GalaxyMap::render() {
    GameGUI::render();
    if (_popup && _popup->isVisible()) {
        _popup->render();
    }
}

void GalaxyMap::close() {
    // Leaving the map keeps whatever destination was picked: the selection is
    // party state, not something this panel owns until it is accepted.
    _game.openInGame();
}

uint32_t GalaxyMap::travelScriptCaller() const {
    // K2 runs the travel script with an invalid caller; K1 runs it with none.
    return _game.isTSL() ? script::kObjectInvalid : 0;
}

void GalaxyMap::runTravelScript() {
    ScopedFlag running(_runningTravelScript);
    try {
        // K2 hands the travel script no caller of its own.
        _game.scriptRunner().run(kTravelScript, travelScriptCaller());
    } catch (const std::exception &e) {
        error(str(boost::format("Galaxy map: %s failed: %s") % kTravelScript % std::string(e.what())));
    }
}

GalaxyMap::AcceptDecision GalaxyMap::decideAccept() const {
    const auto &galaxyMap = _game.party().galaxyMap();
    int row = galaxyMap.selectedPlanet();

    if (!_game.isTSL()) {
        return {isTravelDestination(row) ? AcceptOutcome::Travel : AcceptOutcome::Nothing, -1};
    }
    if (row >= 0 && row == _locationAtOpen) {
        // The party is already there, so say so rather than travel.
        return {AcceptOutcome::Message, kAlreadyAtLocationStrRef};
    }
    if (!galaxyMap.available(row)) {
        return {AcceptOutcome::Nothing, -1};
    }
    if (!galaxyMap.selectable(row)) {
        // On the map, but travel there is locked out for a reason content gives.
        return {AcceptOutcome::Message, lockedOutReason(row)};
    }
    return {AcceptOutcome::Travel, -1};
}

void GalaxyMap::accept() {
    if (_accepting || _runningTravelScript) {
        return;
    }
    auto decision = decideAccept();
    if (decision.outcome == AcceptOutcome::Nothing) {
        return;
    }
    if (decision.outcome == AcceptOutcome::Message) {
        showMessage(decision.strRef);
        return;
    }

    _accepting = true;
    if (_game.isTSL()) {
        // K2 keeps the panel up until the travel script has been handed over.
        runTravelScript();
        close();
    } else {
        close();
        runTravelScript();
    }
}

} // namespace game

} // namespace reone
