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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/gui/galaxymap.h"
#include "reone/game/party.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/resource/2da.h"
#include "reone/scene/graph.h"
#include "reone/game/script/routines.h"
#include "reone/scene/node/model.h"
#include "reone/script/executioncontext.h"
#include "reone/script/types.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace reone::game {

/** Reaches the panel internals a running GUI would otherwise have to build. */
class GalaxyMapTestAccess {
public:
    static void setPlanetary(GalaxyMap &map, std::shared_ptr<TwoDA> planetary) {
        map._planetary = std::move(planetary);
    }

    static void setPlanetControl(GalaxyMap &map, int row, std::shared_ptr<gui::Button> control) {
        map._planetControls[row] = std::move(control);
    }

    static void setPlanetDisplay(GalaxyMap &map, std::shared_ptr<gui::Label> display) {
        map._planetDisplay = std::move(display);
    }

    static void setAcceptButton(GalaxyMap &map, std::shared_ptr<gui::Button> accept) {
        map._accept = std::move(accept);
    }

    static std::string planetModelResRef(const GalaxyMap &map, int row) {
        return map.planetModelResRef(row);
    }

    static const std::shared_ptr<ModelSceneNode> &planetModelNode(const GalaxyMap &map) {
        return map._planetModelNode;
    }

    static const std::string &resRef(const GalaxyMap &map) { return map._resRef; }

    static int locationAtOpen(const GalaxyMap &map) { return map._locationAtOpen; }
    static int lockedOutReason(const GalaxyMap &map, int row) { return map.lockedOutReason(row); }
    static uint32_t travelScriptCaller(const GalaxyMap &map) { return map.travelScriptCaller(); }
    static bool acceptTravels(const GalaxyMap &map) {
        return map.decideAccept().outcome == GalaxyMap::AcceptOutcome::Travel;
    }

    static bool acceptShowsMessage(const GalaxyMap &map) {
        return map.decideAccept().outcome == GalaxyMap::AcceptOutcome::Message;
    }

    static int acceptMessageStrRef(const GalaxyMap &map) { return map.decideAccept().strRef; }

    static void selectNext(GalaxyMap &map) { map.selectNextDestination(); }
    static void selectPrevious(GalaxyMap &map) { map.selectPreviousDestination(); }

    static void select(GalaxyMap &map, int row) { map.select(row); }
    static void accept(GalaxyMap &map) { map.accept(); }
    static void refreshPresentation(GalaxyMap &map) { map.refreshPresentation(); }
    static void updatePlanetControlPresentation(GalaxyMap &map) { map.updatePlanetControlPresentation(); }

    static void selectionChanged(GalaxyMap &map, const std::string &control, bool selected) {
        map.onSelectionChanged(control, selected);
    }
};

} // namespace reone::game

namespace {

constexpr char kTravelScript[] = "k_sup_galaxymap";

/**
 * A stand-in planetary.2da: a planet with a preview model, one with none, and a
 * second planet whose preview differs, so model swaps and reuse can be told
 * apart.
 */
std::shared_ptr<TwoDA> newPlanetary() {
    return TwoDA::Builder()
        .columns({"label", "name", "description", "model", "guitag"})
        .row({"Taris", "1", "2", "planet_01", "LBL_Planet_Taris"})
        .row({"Ebon_Hawk", "3", "", "", ""})
        .row({"Dantooine", "4", "5", "planet_02", "LBL_Planet_Dantooine"})
        .build();
}

/** A model carrying the one-shot preview intro, over one second. */
std::shared_ptr<Model> newPreviewModel(const std::string &name, bool withCameraHook = false) {
    auto rootNode = std::make_shared<ModelNode>(
        0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    if (withCameraHook) {
        auto hook = std::make_shared<ModelNode>(
            1, "camerahook", glm::vec3(0.0f, -5.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, rootNode.get());
        rootNode->addChild(hook);
    }
    auto animRootNode = std::make_shared<ModelNode>(
        0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
    animRootNode->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    animRootNode->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(4.0f, 0.0f, 0.0f));
    auto animation = std::make_shared<Animation>(
        "zoomin", 1.0f, 0.0f, "root_node", animRootNode, std::vector<Animation::Event>());
    auto model = std::make_shared<Model>(
        name, 0, rootNode, std::vector<std::shared_ptr<Animation>> {animation}, "", 1.0f);
    model->init();
    return model;
}

/**
 * A galaxy map hosted on its own engine, so the expectations one test sets on
 * the resource mocks never reach another.
 */
class GalaxyMapFixture : public ::testing::Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(
            GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        // Brings up the routine table and the script runner the travel dispatch
        // goes through.
        _game->initLocalServices();
        _map = std::make_unique<GalaxyMap>(*_game, _engine.services());
        GalaxyMapTestAccess::setPlanetary(*_map, newPlanetary());
        _game->party().galaxyMap().reset(GameID::KotOR, 3);
    }

    /** Attach a real scene graph, so previews are real animated scene nodes. */
    void useRealSceneGraph() {
        _sceneGraph = std::make_unique<SceneGraph>(
            "galaxymap.planet",
            _engine.sceneModule().renderPipelineFactory(),
            _engine.options().graphics,
            _engine.graphicsModule().services(),
            _engine.audioModule().services(),
            _engine.resourceModule().services());
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(*_sceneGraph));
        EXPECT_CALL(_engine.sceneModule().graphs(), get(_)).Times(testing::AnyNumber());
    }

    std::shared_ptr<gui::Button> newButton(std::string tag) {
        auto button = std::make_shared<gui::Button>(
            _gui,
            _engine.sceneModule().graphs(),
            _engine.graphicsModule().services(),
            _engine.resourceModule().services());
        button->setTag(std::move(tag));
        return button;
    }

    std::shared_ptr<gui::Label> newDisplayLabel() {
        auto label = std::make_shared<gui::Label>(
            _gui,
            _engine.sceneModule().graphs(),
            _engine.graphicsModule().services(),
            _engine.resourceModule().services());
        gui::Control::Extent extent;
        extent.width = 256;
        extent.height = 256;
        label->setExtent(std::move(extent));
        return label;
    }

    GalaxyMapState &galaxyMap() { return _game->party().galaxyMap(); }

    TestEngine _engine;
    StubConsole _console;
    gui::MockGUI _gui;
    std::unique_ptr<Game> _game;
    std::unique_ptr<GalaxyMap> _map;
    std::unique_ptr<SceneGraph> _sceneGraph;
};

} // namespace

TEST_F(GalaxyMapFixture, preview_model_comes_from_the_selected_planetary_row) {
    EXPECT_EQ("planet_01", GalaxyMapTestAccess::planetModelResRef(*_map, 0));
    EXPECT_EQ("planet_02", GalaxyMapTestAccess::planetModelResRef(*_map, 2));
}

TEST_F(GalaxyMapFixture, rows_without_a_preview_model_and_rows_off_the_table_have_none) {
    EXPECT_EQ("", GalaxyMapTestAccess::planetModelResRef(*_map, 1));
    EXPECT_EQ("", GalaxyMapTestAccess::planetModelResRef(*_map, -1));
    EXPECT_EQ("", GalaxyMapTestAccess::planetModelResRef(*_map, 3));
}

TEST_F(GalaxyMapFixture, only_available_destinations_are_shown_and_only_selectable_ones_are_live) {
    auto taris = newButton("LBL_Planet_Taris");
    auto dantooine = newButton("LBL_Planet_Dantooine");
    GalaxyMapTestAccess::setPlanetControl(*_map, 0, taris);
    GalaxyMapTestAccess::setPlanetControl(*_map, 2, dantooine);

    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    // Available but not a valid destination: shown, but not open to travel.
    galaxyMap().setAvailable(2, true);

    _map->prepare();

    EXPECT_TRUE(taris->isVisible());
    EXPECT_FALSE(taris->isDisabled());
    EXPECT_TRUE(dantooine->isVisible());
    EXPECT_TRUE(dantooine->isDisabled());
}

TEST_F(GalaxyMapFixture, a_destination_content_never_made_available_stays_hidden) {
    auto taris = newButton("LBL_Planet_Taris");
    GalaxyMapTestAccess::setPlanetControl(*_map, 0, taris);

    _map->prepare();

    EXPECT_FALSE(taris->isVisible());
    EXPECT_TRUE(taris->isDisabled());
}

TEST_F(GalaxyMapFixture, choosing_a_destination_persists_it_immediately) {
    auto taris = newButton("LBL_Planet_Taris");
    GalaxyMapTestAccess::setPlanetControl(*_map, 0, taris);
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);

    GalaxyMapTestAccess::select(*_map, 0);

    EXPECT_EQ(0, galaxyMap().selectedPlanet());
}

TEST_F(GalaxyMapFixture, an_unselectable_destination_leaves_the_previous_choice_alone) {
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    GalaxyMapTestAccess::select(*_map, 0);
    ASSERT_EQ(0, galaxyMap().selectedPlanet());

    // Shown, but locked out of travel.
    galaxyMap().setAvailable(2, true);
    GalaxyMapTestAccess::select(*_map, 2);

    EXPECT_EQ(0, galaxyMap().selectedPlanet());
}

TEST_F(GalaxyMapFixture, the_chosen_destination_carries_the_selected_state) {
    auto taris = newButton("LBL_Planet_Taris");
    auto dantooine = newButton("LBL_Planet_Dantooine");
    GalaxyMapTestAccess::setPlanetControl(*_map, 0, taris);
    GalaxyMapTestAccess::setPlanetControl(*_map, 2, dantooine);
    galaxyMap().setAvailable(0, true);
    galaxyMap().setAvailable(2, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));

    GalaxyMapTestAccess::updatePlanetControlPresentation(*_map);
    EXPECT_TRUE(taris->isSelected());
    EXPECT_FALSE(dantooine->isSelected());

    // The cursor moving over another planet lights it up without taking the
    // chosen destination's own state away.
    GalaxyMapTestAccess::selectionChanged(*_map, "LBL_Planet_Dantooine", true);
    EXPECT_TRUE(taris->isSelected());
    EXPECT_TRUE(dantooine->isSelected());

    GalaxyMapTestAccess::selectionChanged(*_map, "LBL_Planet_Dantooine", false);
    EXPECT_TRUE(taris->isSelected());
    EXPECT_FALSE(dantooine->isSelected());
}

TEST_F(GalaxyMapFixture, the_preview_follows_the_chosen_destination) {
    useRealSceneGraph();
    auto planet01 = newPreviewModel("planet_01");
    auto planet02 = newPreviewModel("planet_02");
    ON_CALL(_engine.resourceModule().models(), get("planet_01")).WillByDefault(Return(planet01));
    ON_CALL(_engine.resourceModule().models(), get("planet_02")).WillByDefault(Return(planet02));
    EXPECT_CALL(_engine.resourceModule().models(), get(_)).Times(testing::AnyNumber());
    GalaxyMapTestAccess::setPlanetDisplay(*_map, newDisplayLabel());
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    galaxyMap().setAvailable(2, true);
    galaxyMap().setSelectable(2, true);

    GalaxyMapTestAccess::select(*_map, 0);
    auto first = GalaxyMapTestAccess::planetModelNode(*_map);
    ASSERT_NE(nullptr, first);

    GalaxyMapTestAccess::select(*_map, 2);
    auto second = GalaxyMapTestAccess::planetModelNode(*_map);
    ASSERT_NE(nullptr, second);
    EXPECT_NE(first.get(), second.get());
}

TEST_F(GalaxyMapFixture, choosing_the_same_destination_again_restarts_its_preview) {
    useRealSceneGraph();
    auto planet01 = newPreviewModel("planet_01");
    ON_CALL(_engine.resourceModule().models(), get("planet_01")).WillByDefault(Return(planet01));
    EXPECT_CALL(_engine.resourceModule().models(), get(_)).Times(testing::AnyNumber());
    GalaxyMapTestAccess::setPlanetDisplay(*_map, newDisplayLabel());
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);

    GalaxyMapTestAccess::select(*_map, 0);
    auto node = GalaxyMapTestAccess::planetModelNode(*_map);
    ASSERT_NE(nullptr, node);

    // Let the one-shot intro run out, the way it does while the panel sits open.
    node->update(1.0f);
    ASSERT_TRUE(node->isAnimationFinished());

    GalaxyMapTestAccess::select(*_map, 0);

    // The same model is still the preview, and its intro is playing from the top.
    EXPECT_EQ(node.get(), GalaxyMapTestAccess::planetModelNode(*_map).get());
    ASSERT_FALSE(node->animationChannels().empty());
    EXPECT_FLOAT_EQ(0.0f, node->animationChannels().front().time);
    EXPECT_FALSE(node->isAnimationFinished());
}

TEST_F(GalaxyMapFixture, revisiting_a_destination_reuses_its_cached_preview_and_restarts_it) {
    useRealSceneGraph();
    auto planet01 = newPreviewModel("planet_01");
    auto planet02 = newPreviewModel("planet_02");
    ON_CALL(_engine.resourceModule().models(), get("planet_01")).WillByDefault(Return(planet01));
    ON_CALL(_engine.resourceModule().models(), get("planet_02")).WillByDefault(Return(planet02));
    // A revisited planet is served from the cache, so its model is fetched once.
    EXPECT_CALL(_engine.resourceModule().models(), get("planet_01")).Times(1);
    EXPECT_CALL(_engine.resourceModule().models(), get("planet_02")).Times(1);
    GalaxyMapTestAccess::setPlanetDisplay(*_map, newDisplayLabel());
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    galaxyMap().setAvailable(2, true);
    galaxyMap().setSelectable(2, true);

    GalaxyMapTestAccess::select(*_map, 0);
    auto first = GalaxyMapTestAccess::planetModelNode(*_map);
    ASSERT_NE(nullptr, first);
    first->update(1.0f);
    ASSERT_TRUE(first->isAnimationFinished());

    GalaxyMapTestAccess::select(*_map, 2);
    GalaxyMapTestAccess::select(*_map, 0);

    EXPECT_EQ(first.get(), GalaxyMapTestAccess::planetModelNode(*_map).get());
    ASSERT_FALSE(first->animationChannels().empty());
    EXPECT_FLOAT_EQ(0.0f, first->animationChannels().front().time);
    EXPECT_FALSE(first->isAnimationFinished());
}

TEST_F(GalaxyMapFixture, escape_leaves_the_map_without_travelling) {
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));
    EXPECT_CALL(_engine.resourceModule().scripts(), get(_)).Times(0);

    auto escape = input::Event::newKeyDown(
        input::KeyEvent(true, input::KeyCode::Escape, 0, false));

    EXPECT_TRUE(_map->handle(escape));

    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen());
    // Backing out is not a rollback: the destination the player picked stands.
    EXPECT_EQ(0, galaxyMap().selectedPlanet());
}

TEST_F(GalaxyMapFixture, accepting_a_destination_runs_the_travel_script_once) {
    auto taris = newButton("LBL_Planet_Taris");
    GalaxyMapTestAccess::setPlanetControl(*_map, 0, taris);
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));

    EXPECT_CALL(_engine.resourceModule().scripts(), get(kTravelScript))
        .Times(1)
        .WillRepeatedly(Return(nullptr));

    GalaxyMapTestAccess::accept(*_map);
    // A second press while the travel script is on its way must not send it again.
    GalaxyMapTestAccess::accept(*_map);

    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen());
}

TEST_F(GalaxyMapFixture, accepting_an_unselectable_destination_does_not_travel) {
    auto dantooine = newButton("LBL_Planet_Dantooine");
    GalaxyMapTestAccess::setPlanetControl(*_map, 2, dantooine);
    galaxyMap().setAvailable(2, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(2));

    EXPECT_CALL(_engine.resourceModule().scripts(), get(_)).Times(0);

    GalaxyMapTestAccess::accept(*_map);
}

TEST_F(GalaxyMapFixture, accepting_with_nothing_chosen_does_not_travel) {
    ASSERT_EQ(-1, galaxyMap().selectedPlanet());
    EXPECT_CALL(_engine.resourceModule().scripts(), get(_)).Times(0);

    GalaxyMapTestAccess::accept(*_map);
}

TEST_F(GalaxyMapFixture, accept_is_live_only_for_a_destination_that_can_be_travelled_to) {
    auto taris = newButton("LBL_Planet_Taris");
    auto accept = newButton("BTN_ACCEPT");
    GalaxyMapTestAccess::setPlanetControl(*_map, 0, taris);
    GalaxyMapTestAccess::setAcceptButton(*_map, accept);

    GalaxyMapTestAccess::refreshPresentation(*_map);
    EXPECT_TRUE(accept->isDisabled());

    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    GalaxyMapTestAccess::select(*_map, 0);
    EXPECT_FALSE(accept->isDisabled());
}

TEST_F(GalaxyMapFixture, the_show_galaxy_map_routine_reaches_the_panel_and_leaves_the_screen_alone_if_it_will_not_load) {
    _game->openInGame();
    ASSERT_EQ(Game::Screen::InGame, _game->currentScreen());

    Routines routines(GameID::KotOR, _game.get(), &_engine.services());
    routines.init();
    script::Routine &routine = routines.get(routines.getIndexByName("ShowGalaxyMap"));
    ASSERT_EQ("ShowGalaxyMap", routine.name());

    script::ExecutionContext execution;
    execution.routines = &routines;

    // The routine used to throw as unimplemented. It now reaches the panel, and
    // the GUI provider here hands back nothing, so the screen it could not
    // replace is the screen that stays.
    EXPECT_NO_THROW(routine.invoke({script::Variable::ofInt(0)}, execution));
    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen());
}

TEST_F(GalaxyMapFixture, revisiting_a_planet_does_not_stack_cameras_on_its_hook) {
    useRealSceneGraph();
    auto planet01 = newPreviewModel("planet_01", /*withCameraHook=*/true);
    auto planet02 = newPreviewModel("planet_02", /*withCameraHook=*/true);
    ON_CALL(_engine.resourceModule().models(), get("planet_01")).WillByDefault(Return(planet01));
    ON_CALL(_engine.resourceModule().models(), get("planet_02")).WillByDefault(Return(planet02));
    EXPECT_CALL(_engine.resourceModule().models(), get(_)).Times(testing::AnyNumber());
    GalaxyMapTestAccess::setPlanetDisplay(*_map, newDisplayLabel());
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    galaxyMap().setAvailable(2, true);
    galaxyMap().setSelectable(2, true);

    GalaxyMapTestAccess::select(*_map, 0);
    auto first = GalaxyMapTestAccess::planetModelNode(*_map);
    ASSERT_NE(nullptr, first);

    auto camerasOnHook = [](const std::shared_ptr<ModelSceneNode> &model) {
        auto hook = model->getNodeByName("camerahook");
        if (!hook) {
            return size_t {0};
        }
        size_t count = 0;
        for (auto *child : hook->children()) {
            if (child->type() == SceneNodeType::Camera) {
                ++count;
            }
        }
        return count;
    };
    ASSERT_EQ(1u, camerasOnHook(first));

    // Away and back: the cached preview is initialized a second time, and the
    // camera the first initialization hung off its hook must not still be there.
    GalaxyMapTestAccess::select(*_map, 2);
    GalaxyMapTestAccess::select(*_map, 0);

    EXPECT_EQ(first.get(), GalaxyMapTestAccess::planetModelNode(*_map).get());
    EXPECT_EQ(1u, camerasOnHook(first));
}

namespace {

/**
 * A stand-in K2 planetary.2da: sixteen rows, of which the first few carry
 * planets and one is locked out with a reason.
 */
std::shared_ptr<TwoDA> newTSLPlanetary() {
    auto builder = TwoDA::Builder();
    builder.columns({"label", "name", "description", "model", "guitag", "lockedoutreason"});
    builder.row({"Dantooine", "123725", "123726", "planet_06", "LBL_Planet_Dantooine", ""});
    builder.row({"Dxun", "123721", "123722", "planet_04", "LBL_Planet_Dxun", ""});
    builder.row({"Ebon_Hawk", "112771", "", "ebon_01", "LBL_EbonHawk", ""});
    builder.row({"Onderon", "123723", "123724", "planet_05", "LBL_Planet_Onderon", "136291"});
    for (int row = 4; row < 16; ++row) {
        builder.row({"Live_Planet", "", "", "", "", ""});
    }
    return builder.build();
}

/** The K2 panel, on its own engine. */
class TSLGalaxyMapFixture : public ::testing::Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(
            GameID::TSL, "", _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _map = std::make_unique<GalaxyMap>(*_game, _engine.services());
        GalaxyMapTestAccess::setPlanetary(*_map, newTSLPlanetary());
        _game->party().galaxyMap().reset(GameID::TSL, 0);
    }

    std::shared_ptr<gui::Button> newButton(std::string tag) {
        auto button = std::make_shared<gui::Button>(
            _gui,
            _engine.sceneModule().graphs(),
            _engine.graphicsModule().services(),
            _engine.resourceModule().services());
        button->setTag(std::move(tag));
        return button;
    }

    GalaxyMapState &galaxyMap() { return _game->party().galaxyMap(); }

    TestEngine _engine;
    StubConsole _console;
    gui::MockGUI _gui;
    std::unique_ptr<Game> _game;
    std::unique_ptr<GalaxyMap> _map;
};

} // namespace

TEST_F(TSLGalaxyMapFixture, the_panel_is_the_one_k2_authors) {
    EXPECT_EQ("galaxymap_p", GalaxyMapTestAccess::resRef(*_map));
}

TEST_F(GalaxyMapFixture, the_panel_is_the_one_k1_authors) {
    EXPECT_EQ("galaxymap", GalaxyMapTestAccess::resRef(*_map));
}

TEST_F(TSLGalaxyMapFixture, the_planet_list_is_always_sixteen_rows) {
    EXPECT_EQ(16, galaxyMap().rowCount());
}

TEST_F(TSLGalaxyMapFixture, the_party_is_where_the_routine_opens_not_where_the_selection_points) {
    // The "Nar Shaddaa 1st" save: the Ebon Hawk sits on Dantooine while the
    // map still carries Nar Shaddaa as the destination last chosen. The
    // routine opens on row 0, and that - not the selection - is the location.
    galaxyMap().setAvailable(0, true);
    galaxyMap().setAvailable(6, true);
    galaxyMap().setSelectable(6, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(6));

    _map->prepare(0);

    EXPECT_EQ(0, GalaxyMapTestAccess::locationAtOpen(*_map));
    // The destination the player already chose is left alone.
    EXPECT_EQ(6, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, the_chosen_destination_travels_and_the_party_location_says_so) {
    galaxyMap().setAvailable(0, true);
    galaxyMap().setAvailable(6, true);
    galaxyMap().setSelectable(6, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(6));

    _map->prepare(0);

    // Nar Shaddaa is somewhere else, so accepting it travels.
    EXPECT_TRUE(GalaxyMapTestAccess::acceptTravels(*_map));

    // Dantooine is where the party already is, whatever its own flags say.
    GalaxyMapTestAccess::select(*_map, 0);
    EXPECT_TRUE(GalaxyMapTestAccess::acceptShowsMessage(*_map));
    EXPECT_EQ(125629, GalaxyMapTestAccess::acceptMessageStrRef(*_map));
}

TEST_F(TSLGalaxyMapFixture, changing_the_selection_does_not_move_where_the_party_is) {
    for (int row : {0, 1, 6}) {
        galaxyMap().setAvailable(row, true);
        galaxyMap().setSelectable(row, true);
    }
    ASSERT_TRUE(galaxyMap().trySelectPlanet(6));

    _map->prepare(0);
    ASSERT_EQ(0, GalaxyMapTestAccess::locationAtOpen(*_map));

    GalaxyMapTestAccess::select(*_map, 1);
    EXPECT_EQ(0, GalaxyMapTestAccess::locationAtOpen(*_map));
    GalaxyMapTestAccess::select(*_map, 0);
    EXPECT_EQ(0, GalaxyMapTestAccess::locationAtOpen(*_map));
    GalaxyMapTestAccess::selectNext(*_map);
    EXPECT_EQ(0, GalaxyMapTestAccess::locationAtOpen(*_map));

    // Still the planet the routine opened on, so it still says so.
    GalaxyMapTestAccess::select(*_map, 0);
    EXPECT_EQ(125629, GalaxyMapTestAccess::acceptMessageStrRef(*_map));
}

TEST_F(TSLGalaxyMapFixture, the_routine_planet_is_taken_only_when_the_selection_is_unusable) {
    // Nothing selected, so the planet the routine names is the first fallback.
    galaxyMap().setAvailable(3, true);
    galaxyMap().setSelectable(3, true);
    galaxyMap().setAvailable(0, true);
    ASSERT_EQ(-1, galaxyMap().selectedPlanet());

    _map->prepare(0);

    EXPECT_EQ(0, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, an_unavailable_routine_planet_falls_through_to_somewhere_reachable) {
    galaxyMap().setAvailable(3, true);
    galaxyMap().setSelectable(3, true);
    ASSERT_EQ(-1, galaxyMap().selectedPlanet());

    // Row 0 is not on the map at all, so it cannot be the opening selection.
    _map->prepare(0);

    EXPECT_EQ(3, galaxyMap().selectedPlanet());
}

TEST_F(GalaxyMapFixture, k1_still_opens_on_the_planet_the_routine_names) {
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    galaxyMap().setAvailable(2, true);
    galaxyMap().setSelectable(2, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));

    _map->prepare(2);

    EXPECT_EQ(2, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, opening_records_where_the_party_already_is) {
    galaxyMap().setAvailable(2, true);
    galaxyMap().setSelectable(2, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(2));

    _map->prepare(2);

    EXPECT_EQ(2, GalaxyMapTestAccess::locationAtOpen(*_map));
    // A selection that still stands is kept.
    EXPECT_EQ(2, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, opening_without_a_usable_selection_falls_back_to_a_reachable_planet) {
    galaxyMap().setAvailable(3, true);
    galaxyMap().setAvailable(1, true);
    galaxyMap().setSelectable(1, true);
    ASSERT_EQ(-1, galaxyMap().selectedPlanet());

    _map->prepare();

    // Row 3 is available first by index, but locked out of travel; the fallback
    // prefers somewhere the party can actually go.
    EXPECT_EQ(1, galaxyMap().selectedPlanet());
    EXPECT_EQ(-1, GalaxyMapTestAccess::locationAtOpen(*_map));
}

TEST_F(TSLGalaxyMapFixture, opening_falls_back_to_a_locked_out_planet_when_that_is_all_there_is) {
    galaxyMap().setAvailable(3, true);

    _map->prepare();

    EXPECT_EQ(3, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, opening_with_no_planet_available_selects_nothing) {
    _map->prepare();

    EXPECT_EQ(-1, galaxyMap().selectedPlanet());
    EXPECT_EQ(-1, GalaxyMapTestAccess::locationAtOpen(*_map));
}

TEST_F(TSLGalaxyMapFixture, navigation_walks_the_available_planets_and_wraps_around) {
    for (int row : {0, 1, 3}) {
        galaxyMap().setAvailable(row, true);
        galaxyMap().setSelectable(row, true);
    }
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));

    GalaxyMapTestAccess::selectNext(*_map);
    EXPECT_EQ(1, galaxyMap().selectedPlanet());
    GalaxyMapTestAccess::selectNext(*_map);
    EXPECT_EQ(3, galaxyMap().selectedPlanet());
    GalaxyMapTestAccess::selectNext(*_map);
    EXPECT_EQ(0, galaxyMap().selectedPlanet());

    GalaxyMapTestAccess::selectPrevious(*_map);
    EXPECT_EQ(3, galaxyMap().selectedPlanet());
    GalaxyMapTestAccess::selectPrevious(*_map);
    EXPECT_EQ(1, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, navigation_stops_at_planets_travel_is_locked_out_of) {
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    // On the map, but not a valid destination.
    galaxyMap().setAvailable(3, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));

    GalaxyMapTestAccess::selectNext(*_map);

    EXPECT_EQ(3, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, navigation_is_safe_with_no_planet_available) {
    GalaxyMapTestAccess::selectNext(*_map);
    EXPECT_EQ(-1, galaxyMap().selectedPlanet());

    GalaxyMapTestAccess::selectPrevious(*_map);
    EXPECT_EQ(-1, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, planet_details_come_from_the_table_row) {
    EXPECT_EQ("planet_04", GalaxyMapTestAccess::planetModelResRef(*_map, 1));
    EXPECT_EQ(136291, GalaxyMapTestAccess::lockedOutReason(*_map, 3));
    // Rows content leaves open carry no reason, and neither do rows off the table.
    EXPECT_EQ(-1, GalaxyMapTestAccess::lockedOutReason(*_map, 0));
    EXPECT_EQ(-1, GalaxyMapTestAccess::lockedOutReason(*_map, 16));
}

TEST_F(TSLGalaxyMapFixture, accepting_where_the_party_already_is_says_so_and_does_not_travel) {
    galaxyMap().setAvailable(1, true);
    galaxyMap().setSelectable(1, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(1));
    // The routine opens on the planet the party is at, which here is also the
    // destination it already carries.
    _map->prepare(1);
    EXPECT_CALL(_engine.resourceModule().scripts(), get(_)).Times(0);

    EXPECT_TRUE(GalaxyMapTestAccess::acceptShowsMessage(*_map));
    EXPECT_EQ(125629, GalaxyMapTestAccess::acceptMessageStrRef(*_map));

    GalaxyMapTestAccess::accept(*_map);

    // The map stays up and the choice stands.
    EXPECT_NE(Game::Screen::InGame, _game->currentScreen());
    EXPECT_EQ(1, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, accepting_a_locked_out_planet_shows_its_reason_and_does_not_travel) {
    galaxyMap().setAvailable(1, true);
    galaxyMap().setSelectable(1, true);
    galaxyMap().setAvailable(3, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(1));
    _map->prepare();
    ASSERT_TRUE(galaxyMap().trySelectPlanet(3));
    EXPECT_CALL(_engine.resourceModule().scripts(), get(_)).Times(0);

    EXPECT_TRUE(GalaxyMapTestAccess::acceptShowsMessage(*_map));
    EXPECT_EQ(136291, GalaxyMapTestAccess::acceptMessageStrRef(*_map));

    GalaxyMapTestAccess::accept(*_map);

    EXPECT_NE(Game::Screen::InGame, _game->currentScreen());
}

TEST_F(TSLGalaxyMapFixture, accepting_a_new_reachable_planet_travels_there) {
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    galaxyMap().setAvailable(1, true);
    galaxyMap().setSelectable(1, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));
    _map->prepare();
    ASSERT_TRUE(galaxyMap().trySelectPlanet(1));

    EXPECT_CALL(_engine.resourceModule().scripts(), get(kTravelScript))
        .Times(1)
        .WillRepeatedly(Return(nullptr));

    EXPECT_TRUE(GalaxyMapTestAccess::acceptTravels(*_map));

    GalaxyMapTestAccess::accept(*_map);
    GalaxyMapTestAccess::accept(*_map);

    // K2 leaves the panel only once the travel script has been handed over.
    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen());
    EXPECT_FALSE(_map->isRunningTravelScript());
}

TEST_F(TSLGalaxyMapFixture, the_travel_script_is_handed_an_invalid_caller) {
    EXPECT_EQ(script::kObjectInvalid, GalaxyMapTestAccess::travelScriptCaller(*_map));
}

TEST_F(GalaxyMapFixture, the_k1_travel_script_is_handed_no_caller) {
    EXPECT_EQ(0u, GalaxyMapTestAccess::travelScriptCaller(*_map));
}

TEST_F(TSLGalaxyMapFixture, a_failing_travel_script_still_clears_the_running_state) {
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    galaxyMap().setAvailable(1, true);
    galaxyMap().setSelectable(1, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));
    _map->prepare();
    ASSERT_TRUE(galaxyMap().trySelectPlanet(1));

    EXPECT_CALL(_engine.resourceModule().scripts(), get(kTravelScript))
        .WillRepeatedly(testing::Throw(std::runtime_error("script blew up")));

    GalaxyMapTestAccess::accept(*_map);

    EXPECT_FALSE(_map->isRunningTravelScript());
}

TEST_F(TSLGalaxyMapFixture, escape_leaves_the_map_without_travelling_or_rolling_back) {
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    galaxyMap().setAvailable(1, true);
    galaxyMap().setSelectable(1, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));
    _map->prepare();
    ASSERT_TRUE(galaxyMap().trySelectPlanet(1));
    EXPECT_CALL(_engine.resourceModule().scripts(), get(_)).Times(0);

    auto escape = input::Event::newKeyDown(
        input::KeyEvent(true, input::KeyCode::Escape, 0, false));
    EXPECT_TRUE(_map->handle(escape));

    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen());
    EXPECT_EQ(1, galaxyMap().selectedPlanet());
}

TEST(GalaxyMapShowGuards, the_map_only_takes_over_a_screen_it_can_return_to) {
    EXPECT_TRUE(Game::canOpenGalaxyMapFrom(Game::Screen::None));
    EXPECT_TRUE(Game::canOpenGalaxyMapFrom(Game::Screen::InGame));
    EXPECT_TRUE(Game::canOpenGalaxyMapFrom(Game::Screen::InGameMenu));
    EXPECT_TRUE(Game::canOpenGalaxyMapFrom(Game::Screen::Conversation));

    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::GalaxyMap));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::MainMenu));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::Loading));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::CharacterGeneration));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::SaveLoad));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::Container));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::PartySelection));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::PazaakBoard));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::SwoopRace));
    EXPECT_FALSE(Game::canOpenGalaxyMapFrom(Game::Screen::Turret));
}

TEST_F(TSLGalaxyMapFixture, a_panel_that_will_not_load_leaves_the_screen_alone) {
    _game->openInGame();
    ASSERT_EQ(Game::Screen::InGame, _game->currentScreen());

    // The GUI provider hands back nothing, so the panel cannot be built.
    _game->openGalaxyMap(0);

    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen());
}

TEST_F(TSLGalaxyMapFixture, a_planet_travel_is_locked_out_of_can_still_be_picked_on_the_map) {
    auto onderon = std::make_shared<gui::Button>(
        _gui,
        _engine.sceneModule().graphs(),
        _engine.graphicsModule().services(),
        _engine.resourceModule().services());
    onderon->setTag("LBL_Planet_Onderon");
    GalaxyMapTestAccess::setPlanetControl(*_map, 3, onderon);
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    // On the map, but not a destination the party can travel to.
    galaxyMap().setAvailable(3, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));

    _map->prepare();

    // Picking it is the only way the player is told why it is closed, so the
    // control has to stay live.
    EXPECT_TRUE(onderon->isVisible());
    EXPECT_FALSE(onderon->isDisabled());

    GalaxyMapTestAccess::select(*_map, 3);

    EXPECT_EQ(3, galaxyMap().selectedPlanet());
    EXPECT_TRUE(GalaxyMapTestAccess::acceptShowsMessage(*_map));
    EXPECT_EQ(136291, GalaxyMapTestAccess::acceptMessageStrRef(*_map));
}

TEST_F(GalaxyMapFixture, a_k1_destination_that_cannot_be_travelled_to_stays_inert) {
    auto dantooine = newButton("LBL_Planet_Dantooine");
    GalaxyMapTestAccess::setPlanetControl(*_map, 2, dantooine);
    galaxyMap().setAvailable(2, true);

    _map->prepare();

    // K1 authors no reason to show, so there is nothing to be gained by
    // letting the player land on it.
    EXPECT_TRUE(dantooine->isVisible());
    EXPECT_TRUE(dantooine->isDisabled());

    GalaxyMapTestAccess::select(*_map, 2);
    EXPECT_EQ(-1, galaxyMap().selectedPlanet());
}

TEST_F(TSLGalaxyMapFixture, accept_stays_live_for_a_planet_that_only_has_a_reason_to_give) {
    auto accept = newButton("BTN_ACCEPT");
    GalaxyMapTestAccess::setAcceptButton(*_map, accept);
    galaxyMap().setAvailable(0, true);
    galaxyMap().setSelectable(0, true);
    galaxyMap().setAvailable(3, true);
    ASSERT_TRUE(galaxyMap().trySelectPlanet(0));
    _map->prepare();

    // The party's own location: pressing Accept is how it says so.
    EXPECT_FALSE(accept->isDisabled());

    // A planet travel is locked out of: pressing Accept is how the reason shows.
    GalaxyMapTestAccess::select(*_map, 3);
    EXPECT_FALSE(accept->isDisabled());
}

TEST_F(TSLGalaxyMapFixture, accept_is_dead_with_nothing_reachable_chosen) {
    auto accept = newButton("BTN_ACCEPT");
    GalaxyMapTestAccess::setAcceptButton(*_map, accept);

    _map->prepare();

    ASSERT_EQ(-1, galaxyMap().selectedPlanet());
    EXPECT_TRUE(accept->isDisabled());
}
