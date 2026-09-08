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

#include "reone/game/gui/mainmenu.h"

#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/object/creature.h"
#include "reone/game/types.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/model.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/types.h"
#include "reone/system/logutil.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kKotorModelSize = 1.4f;

MainMenu::MainMenu(Game &game, ServicesView &services) :
    GameGUI(game, services) {

    if (_game.isTSL()) {
        _resRef = "mainmenu8x6_p";
        _musicResRef = "mus_sion";
    } else {
        _resRef = "mainmenu16x12";
        _musicResRef = "mus_theme_cult";
    }
}

void MainMenu::preload(IGUI &gui) {
    GameGUI::preload(gui);
    gui.setResolution(800, 600);
}

void MainMenu::onGUILoaded() {
    if (!_game.isTSL()) {
        loadBackground(BackgroundType::Menu);
    }

    bindControls();

    _controls.LB_MODULES->setVisible(false);
    _controls.LBL_NEWCONTENT->setVisible(false);
    _controls.LBL_BW->setVisible(false);
    _controls.LBL_LUCAS->setVisible(false);
    _controls.BTN_MOVIES->setDisabled(true);
    _controls.BTN_OPTIONS->setDisabled(true);

    if (_controls.BTN_MOREGAMES) {
        _controls.BTN_MOREGAMES->setVisible(false);
    }
    if (_controls.BTN_TSLRCM) {
        _controls.BTN_TSLRCM->setVisible(false);
    }

    // Hide warp button in developer mode
    if (!_game.options().game.developer) {
        _controls.BTN_WARP->setVisible(false);
    }

    _controls.BTN_NEWGAME->setOnClick([this]() {
        _game.startCharacterGeneration();
    });
    _controls.BTN_LOADGAME->setOnClick([this]() {
        _game.openSaveLoad(SaveLoadMode::LoadFromMainMenu);
    });
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.quit();
    });
    _controls.BTN_WARP->setOnClick([this]() {
        startModuleSelection();
    });
    _controls.LB_MODULES->setOnItemClick([this](const std::string &item) {
        onModuleSelected(item);
    });

    setup3DView();
    configureButtons();
}

void MainMenu::configureButtons() {
    setButtonColors(*_controls.BTN_EXIT);
    setButtonColors(*_controls.BTN_LOADGAME);
    setButtonColors(*_controls.BTN_MOVIES);
    setButtonColors(*_controls.BTN_NEWGAME);
    setButtonColors(*_controls.BTN_OPTIONS);

    if (_game.isTSL()) {
        setButtonColors(*_controls.BTN_MUSIC);
    }
}

void MainMenu::setButtonColors(Control &control) {
    control.setTextColor(_baseColor);
    control.setHilightColor(_hilightColor);
}

void MainMenu::setup3DView() {
    if (_game.isTSL()) {
        refreshScene();
        return;
    }

    auto &sceneGraph = _services.scene.graphs.get(kSceneMainMenu);
    const Control::Extent &extent = _controls.LBL_3DVIEW->extent();
    float aspect = extent.width / static_cast<float>(extent.height);

    SceneInitializer(sceneGraph)
        .aspect(aspect)
        .depth(kDefaultClipPlaneNear, 10.0f)
        .modelSupplier(bind(&MainMenu::getKotorModel, this, std::placeholders::_1))
        .modelScale(kKotorModelSize)
        .cameraFromModelNode("camerahook")
        .invoke();

    _controls.LBL_3DVIEW->setSceneName(kSceneMainMenu);
}

void MainMenu::refreshScene() {
    if (!_game.isTSL() || !_controls.LBL_3DVIEW) return;

    if (_controls.LB_MODULES && _controls.LB_MODULES->isVisible()) {
        _controls.LB_MODULES->setVisible(false);
        for (auto &button : {_controls.BTN_EXIT, _controls.BTN_LOADGAME,
                 _controls.BTN_MOVIES, _controls.BTN_NEWGAME, _controls.BTN_OPTIONS,
                 _controls.BTN_MUSIC}) {
            button->setVisible(true);
        }
        _controls.BTN_WARP->setVisible(_game.options().game.developer);
        _controls.LBL_GAMELOGO->setVisible(true);
    }
    _controls.LBL_3DVIEW->setVisible(true);

    // clear() drops roots but keeps the node arena. Replace only this scene so
    // repeated returns also release obsolete emitters, cameras and listeners.
    _leader.reset();
    _services.scene.graphs.reset(kSceneMainMenu);
    auto &sceneGraph = _services.scene.graphs.get(kSceneMainMenu);
    const auto &state = _game.options().game.menuPresentation;
    const auto &extent = _controls.LBL_3DVIEW->extent();
    auto model = _services.resource.models.get(state.modelResRef(true));
    if (!model) return;
    auto environment = sceneGraph.newModel(*model, ModelUsage::GUI);
    if (!environment) return;
    SceneInitializer(sceneGraph)
        .aspect(extent.width / static_cast<float>(extent.height))
        .depth(0.1f, 10000.0f)
        .perspective(glm::radians(22.7259998f))
        .modelScale(1.0f)
        .prewarmEmitters()
        .modelSupplier([environment](ISceneGraph &) { return environment; })
        .invoke();
    if (auto room = _services.resource.models.get("gui3d_room")) {
        sceneGraph.clear();
        sceneGraph.addRoot(sceneGraph.newModel(*room, ModelUsage::Room));
        sceneGraph.addRoot(environment);
    }
    // mainmenu03 also has a character-local camerahook. Select the scene's
    // direct child by number, avoiding the duplicate-name lookup entirely.
    if (auto camera = sceneGraph.camera()) {
        for (const auto &child : model->rootNode()->children()) {
            if (child->name() == "camerahook") {
                environment->getNodeByNumber(child->number())->addChild(camera->get());
                break;
            }
        }
    }
    environment->playAnimation("default", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    _controls.LBL_3DVIEW->setSceneName(kSceneMainMenu);

    if (MenuPresentation::validateSelector(state.selector) == 4 && state.leader) {
        try {
            _leader = _game.newPresentationCreature(kSceneMainMenu);
            _leader->setPresentation(*state.leader);
            auto placement = environment->getNodeByName("cutscenedummy");
            _leader->setPosition(placement ? glm::vec3(placement->absoluteTransform()[3]) : glm::vec3(0.0f, -1.0f, 0.0f));
            // The authored menu actor faces -Y, toward the camera.
            _leader->setFacing(glm::pi<float>());
            _leader->loadAppearance();
            auto node = std::static_pointer_cast<ModelSceneNode>(_leader->sceneNode());
            if (node) {
                // Resolve through the supermodel and propagate to the head,
                // without any runtime action or AI state.
                auto animation = node->model().getAnimation("evil") ? "evil" : "pause1";
                if (std::string_view(animation) != "evil") {
                    warn("Main menu leader model has no evil animation");
                }
                node->playAnimation(animation, nullptr,
                    AnimationProperties::fromFlags(AnimationFlags::loop | AnimationFlags::propagate));
                sceneGraph.addRoot(node);
            } else {
                _leader.reset();
            }
        } catch (const std::exception &ex) {
            _leader.reset();
            warn(std::string("Could not build main menu leader: ") + ex.what());
        }
    }
}

std::shared_ptr<ModelSceneNode> MainMenu::getKotorModel(ISceneGraph &sceneGraph) {
    auto model = _services.resource.models.get("mainmenu");
    if (!model) {
        return nullptr;
    }
    return sceneGraph.newModel(*model, ModelUsage::GUI);
}

void MainMenu::startModuleSelection() {
    loadModuleNames();

    _controls.LB_MODULES->setVisible(true);
    _controls.BTN_EXIT->setVisible(false);
    _controls.BTN_LOADGAME->setVisible(false);
    _controls.BTN_MOVIES->setVisible(false);
    _controls.BTN_NEWGAME->setVisible(false);
    _controls.BTN_OPTIONS->setVisible(false);
    _controls.BTN_WARP->setVisible(false);
    _controls.LBL_3DVIEW->setVisible(false);
    _controls.LBL_GAMELOGO->setVisible(false);

    if (_game.isTSL()) {
        _controls.BTN_MUSIC->setVisible(false);
    } else {
        _controls.LBL_MENUBG->setVisible(false);
    }
}

void MainMenu::loadModuleNames() {
    for (auto &module : _game.moduleNames()) {
        ListBox::Item item;
        item.tag = module;
        item.text = module;
        _controls.LB_MODULES->addItem(std::move(item));
    }
}

void MainMenu::onModuleSelected(const std::string &name) {
    _game.loadModule(name);
}

} // namespace game

} // namespace reone
