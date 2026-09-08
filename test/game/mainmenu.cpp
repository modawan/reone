/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <fstream>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/gui/mainmenu.h"
#include "reone/game/object/creature.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/camera.h"
#include "reone/graphics/model.h"
#include "reone/resource/2da.h"
#include "reone/scene/graph.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"

using namespace reone;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace testing;

namespace reone::game {
class MainMenuTestAccess {
public:
    static void bind(MainMenu &menu, std::shared_ptr<gui::IGUI> gui, std::shared_ptr<gui::Label> label) {
        menu._gui = std::move(gui);
        menu._controls.LBL_3DVIEW = std::move(label);
    }
    static void setup(MainMenu &menu) { menu.setup3DView(); }
    static std::shared_ptr<Creature> leader(MainMenu &menu) { return menu._leader; }
};

MainMenu &TestGameModule::installMainMenu(Game &game) {
    game._mainMenu = std::make_unique<MainMenu>(game, game._services);
    return *game._mainMenu;
}
} // namespace reone::game

namespace {
class MenuGraph : public SceneGraph {
public:
    using SceneGraph::SceneGraph;
    std::vector<std::weak_ptr<ModelSceneNode>> roots;
    void clear() override {
        roots.clear();
        SceneGraph::clear();
    }
    void addRoot(std::shared_ptr<ModelSceneNode> node) override {
        roots.push_back(node);
        SceneGraph::addRoot(std::move(node));
    }
};

std::shared_ptr<Model> model(const std::string &name) {
    auto root = std::make_shared<ModelNode>(0, "root", glm::vec3(0.0f), glm::quat(1, 0, 0, 0), true, nullptr);
    auto hook = std::make_shared<ModelNode>(1, "camerahook", glm::vec3(0, -5, 1), glm::quat(1, 0, 0, 0), true, root.get());
    root->addChild(hook);
    auto placement = std::make_shared<ModelNode>(2, "cutscenedummy", glm::vec3(-1, 1, 0), glm::quat(0, 0, 0, 1), true, root.get());
    root->addChild(placement);
    // Nihilus embeds a second camera inside its character subtree.
    auto closeup = std::make_shared<ModelNode>(3, "camerahook", glm::vec3(0, 0, 1.6f), glm::quat(1, 0, 0, 0), true, placement.get());
    placement->addChild(closeup);
    auto clip = std::make_shared<Animation>("evil", 16.0f, 0.0f, "root", root, std::vector<Animation::Event>());
    auto loop = std::make_shared<Animation>("default", 16.0f, 0.0f, "root", root, std::vector<Animation::Event>());
    auto result = std::make_shared<Model>(name, 0, root, std::vector<std::shared_ptr<Animation>> {clip, loop}, "", 1.0f);
    result->init();
    return result;
}

class MainMenuTest : public Test {
protected:
    void SetUp() override {
        engine.init();
        resetGraph();
        ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(mainGraph));
        ON_CALL(engine.sceneModule().graphs(), get(kSceneMainMenu)).WillByDefault(Invoke([this](const auto &) -> ISceneGraph & { return *graph; }));
        ON_CALL(engine.sceneModule().graphs(), reset(kSceneMainMenu)).WillByDefault(Invoke([this](const auto &) { resetGraph(); }));
        for (int i = 0; i < 5; ++i) {
            auto name = MenuPresentation {i, std::nullopt}.modelResRef(true);
            models.push_back(model(name));
            ON_CALL(engine.resourceModule().models(), get(name)).WillByDefault(Return(models.back()));
        }
        models.push_back(model("mainmenu"));
        ON_CALL(engine.resourceModule().models(), get("mainmenu")).WillByDefault(Return(models.back()));
        models.push_back(model("body"));
        ON_CALL(engine.resourceModule().models(), get("body")).WillByDefault(Return(models.back()));
        std::shared_ptr<TwoDA> appearance = TwoDA::Builder().columns({"modeltype", "modela", "modelb", "normalhead"})
            .row({"B", "body", "body", "-1"}).build();
        ON_CALL(engine.resourceModule().twoDas(), get("appearance")).WillByDefault(Return(appearance));
        create(GameID::TSL);
    }
    void resetGraph() {
        graph = std::make_unique<MenuGraph>(kSceneMainMenu, engine.sceneModule().renderPipelineFactory(),
            engine.options().graphics, engine.graphicsModule().services(), engine.audioModule().services(),
            engine.resourceModule().services());
    }
    void create(GameID id) {
        game = std::make_unique<Game>(id, "", engine.options(), engine.services(), console);
        menu = &TestGameModule::installMainMenu(*game);
        gui = std::make_shared<NiceMock<gui::MockGUI>>();
        label = std::make_shared<gui::Label>(*gui, engine.sceneModule().graphs(),
            engine.graphicsModule().services(), engine.resourceModule().services());
        gui::Control::Extent extent;
        extent.width = 800;
        extent.height = 600;
        label->setExtent(extent);
        MainMenuTestAccess::bind(*menu, gui, label);
    }
    TestEngine engine;
    StubConsole console;
    NiceMock<MockSceneGraph> mainGraph;
    std::vector<std::shared_ptr<Model>> models;
    std::unique_ptr<MenuGraph> graph;
    std::shared_ptr<gui::MockGUI> gui;
    std::shared_ptr<gui::Label> label;
    std::unique_ptr<Game> game;
    MainMenu *menu {nullptr};
};

TEST(MenuPresentation, selector_mapping_and_missing_state) {
    EXPECT_EQ("mainmenu01", MenuPresentation {}.modelResRef(true));
    for (int value : {-100, -1, 0, 1, 2, 3, 4, 5, 255}) {
        MenuPresentation state {value, std::nullopt};
        EXPECT_EQ("mainmenu0" + std::to_string(value >= 0 && value <= 4 ? value + 1 : 1), state.modelResRef(true));
        EXPECT_EQ("mainmenu", state.modelResRef(false));
    }
    EXPECT_EQ(0, MenuPresentation::load({}).selector);
}

TEST(MenuPresentation, round_trip_preserves_unrelated_configuration_bytes) {
    auto path = std::filesystem::temp_directory_path() / ("reone-menu-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg");
    const std::string unrelated = "# user comment\r\nwidth = 1920\r\nunknown = hello\n[extra]\nsetting=42";
    {
        std::ofstream file(path, std::ios::binary);
        file << unrelated;
    }
    EXPECT_EQ(0, MenuPresentation::load(path).selector);
    MenuPresentation state {4, CreaturePresentation {1, 42, 8, 3}};
    state.save(path);
    auto loaded = MenuPresentation::load(path);
    EXPECT_EQ(4, loaded.selector);
    ASSERT_TRUE(loaded.leader);
    EXPECT_EQ(1, loaded.leader->gender);
    EXPECT_EQ(42, loaded.leader->appearance);
    EXPECT_EQ(8, loaded.leader->bodyVariation);
    EXPECT_EQ(3, loaded.leader->textureVariation);
    loaded.save(path);
    std::ifstream input(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)), {});
    input.close(); // Release the Windows read handle before saving again.
    EXPECT_EQ(unrelated, content.substr(content.find("# user comment")));
    MenuPresentation {99, std::nullopt}.save(path);
    EXPECT_EQ(0, MenuPresentation::load(path).selector);
    EXPECT_FALSE(MenuPresentation::load(path).leader);
    std::filesystem::remove(path);
}

TEST_F(MainMenuTest, returning_to_retained_menu_captures_before_teardown_and_refreshes) {
    menu->refreshScene();
    ASSERT_EQ(1u, graph->roots.size());
    auto previous = graph->roots.front();
    for (int selector : {2, 3, 0, 99}) {
        auto runtime = game->newCreature();
        game->party().addMember(kNpcPlayer, runtime);
        game->setGlobalNumber("GBL_MAIN_SITH_LORD", selector);
        TestGameModule::setRuntimeSessionPlayable(*game, true);
        game->openMainMenu();
        EXPECT_EQ(MenuPresentation::validateSelector(selector), engine.options().game.menuPresentation.selector);
        EXPECT_TRUE(game->globalNumbers().empty());
        EXPECT_TRUE(game->party().isEmpty());
        EXPECT_FALSE(game->getObjectById(runtime->id()));
        EXPECT_TRUE(previous.expired());
        ASSERT_EQ(1u, graph->roots.size());
        EXPECT_EQ(engine.options().game.menuPresentation.modelResRef(true), graph->roots.front().lock()->model().name());
        EXPECT_FALSE(MainMenuTestAccess::leader(*menu));
        previous = graph->roots.front();
    }
}

TEST_F(MainMenuTest, leader_is_copied_to_exactly_one_presentation_and_released_on_refresh) {
    for (auto gender : {Gender::Male, Gender::Female, Gender::None}) {
        auto runtime = game->newCreature();
        runtime->setGender(gender);
        runtime->setAppearance(0);
        game->party().addMember(kNpcPlayer, runtime);
        game->setGlobalNumber("GBL_MAIN_SITH_LORD", 4);
        TestGameModule::setRuntimeSessionPlayable(*game, true);
        std::weak_ptr<Creature> oldRuntime = runtime;
        runtime.reset();
        game->openMainMenu();
        EXPECT_TRUE(oldRuntime.expired());
        auto leader = MainMenuTestAccess::leader(*menu);
        ASSERT_TRUE(leader);
        EXPECT_TRUE(leader->isPresentationOnly());
        EXPECT_FALSE(game->getObjectById(leader->id()));
        EXPECT_TRUE(game->party().isEmpty());
        EXPECT_EQ(gender, leader->gender());
        EXPECT_EQ(glm::vec3(-1, 1, 0), leader->position());
        EXPECT_NEAR(glm::pi<float>(), std::abs(leader->getFacing()), 0.0001f);
        ASSERT_EQ(2u, graph->roots.size());
        EXPECT_TRUE(graph->roots.back().lock()->isAnimationPlaying("evil"));
        std::weak_ptr<Creature> oldLeader = leader;
        auto oldModel = graph->roots.back();
        leader.reset();
        menu->refreshScene();
        EXPECT_TRUE(oldLeader.expired());
        EXPECT_TRUE(oldModel.expired());
        EXPECT_EQ(2u, graph->roots.size());
    }
    engine.options().game.menuPresentation.selector = 2;
    menu->refreshScene();
    EXPECT_FALSE(MainMenuTestAccess::leader(*menu));
    EXPECT_EQ(1u, graph->roots.size());
}

TEST_F(MainMenuTest, cold_entry_keeps_persisted_state_and_k1_keeps_its_original_model) {
    engine.options().game.menuPresentation.selector = 3;
    game->openMainMenu();
    EXPECT_EQ("mainmenu04", graph->roots.front().lock()->model().name());
    ASSERT_TRUE(graph->camera());
    auto camera = graph->camera()->get().camera();
    ASSERT_TRUE(camera);
    auto expected = glm::perspective(glm::radians(22.7259998f), 800.0f / 600.0f, 0.1f, 10000.0f);
    EXPECT_EQ(expected, camera->projection());
    EXPECT_EQ(glm::vec3(0, -5, 1), glm::vec3(graph->camera()->get().absoluteTransform()[3]));
    auto environment = graph->roots.front().lock();
    environment->update(16.1f);
    EXPECT_TRUE(environment->isAnimationPlaying("default"));
    create(GameID::KotOR);
    MainMenuTestAccess::setup(*menu);
    ASSERT_EQ(1u, graph->roots.size());
    auto malak = graph->roots.front();
    EXPECT_EQ("mainmenu", malak.lock()->model().name());
    ASSERT_TRUE(graph->camera());
    camera = graph->camera()->get().camera();
    ASSERT_TRUE(camera);
    float aspect = 800.0f / 600.0f;
    expected = glm::ortho(-aspect * 1.4f, aspect * 1.4f, -1.4f, 1.4f, kDefaultClipPlaneNear, 10.0f);
    EXPECT_EQ(expected, camera->projection());
    menu->refreshScene();
    EXPECT_EQ(malak.lock(), graph->roots.front().lock());
}

TEST_F(MainMenuTest, missing_variant_resource_drops_previous_scene_and_leader) {
    engine.options().game.menuPresentation = {4, CreaturePresentation {}};
    menu->refreshScene();
    ASSERT_EQ(2u, graph->roots.size());
    auto oldModel = graph->roots.back();
    engine.options().game.menuPresentation.selector = 3;
    ON_CALL(engine.resourceModule().models(), get("mainmenu04")).WillByDefault(Return(nullptr));
    menu->refreshScene();
    EXPECT_TRUE(graph->roots.empty());
    EXPECT_TRUE(oldModel.expired());
    EXPECT_FALSE(MainMenuTestAccess::leader(*menu));
}

TEST_F(MainMenuTest, selector_four_without_a_complete_leader_tuple_shows_environment_only) {
    engine.options().game.menuPresentation = {4, std::nullopt};
    menu->refreshScene();
    EXPECT_EQ(1u, graph->roots.size());
    EXPECT_FALSE(MainMenuTestAccess::leader(*menu));
}

TEST_F(MainMenuTest, visual_tuple_cannot_be_applied_to_a_runtime_creature) {
    EXPECT_THROW(game->newCreature()->setPresentation({}), std::logic_error);
}

TEST_F(MainMenuTest, captures_effective_body_visuals_without_copying_equipment) {
    std::shared_ptr<TwoDA> baseItems = TwoDA::Builder()
        .columns({"equipableslots", "bodyvar", "ammunitiontype"})
        .row({"2", "b", "0"}).build();
    ON_CALL(engine.resourceModule().twoDas(), get("baseitems")).WillByDefault(Return(baseItems));
    auto uti = Gff::Builder()
        .field(Gff::Field::newInt("BaseItem", 0))
        .field(Gff::Field::newByte("TextureVar", 3)).build();
    auto clothing = game->newItem(*uti, SerializedIdentityContext::templateResource("clothes"));
    auto runtime = game->newCreature();
    TestGameModule::setSnapshotEquipment(*runtime, InventorySlots::body, clothing);
    game->party().addMember(kNpcPlayer, runtime);
    game->setGlobalNumber("GBL_MAIN_SITH_LORD", 4);
    TestGameModule::setRuntimeSessionPlayable(*game, true);
    game->openMainMenu();
    const auto &snapshot = engine.options().game.menuPresentation;
    ASSERT_TRUE(snapshot.leader);
    EXPECT_EQ(1, snapshot.leader->bodyVariation);
    EXPECT_EQ(3, snapshot.leader->textureVariation);
    auto leader = MainMenuTestAccess::leader(*menu);
    ASSERT_TRUE(leader);
    EXPECT_FALSE(leader->getEquippedItem(InventorySlots::body));
    EXPECT_FALSE(game->getObjectById(clothing->id()));
    EXPECT_EQ(2u, graph->roots.size());
}

TEST_F(MainMenuTest, replacing_one_scene_releases_its_arena_without_touching_gameplay) {
    SceneGraphs scenes(engine.sceneModule().renderPipelineFactory(), engine.options().graphics,
        engine.graphicsModule().services(), engine.audioModule().services(), engine.resourceModule().services());
    scenes.reserve(kSceneMain);
    scenes.reserve(kSceneMainMenu);
    auto &main = scenes.get(kSceneMain);
    auto gameplay = main.newModel(*models.front(), ModelUsage::Creature);
    main.addRoot(gameplay);
    EXPECT_CALL(engine.graphicsModule().context(), resetReadFramebuffer()).Times(5);
    EXPECT_CALL(engine.graphicsModule().context(), resetDrawFramebuffer()).Times(5);
    std::weak_ptr<ModelSceneNode> previous;
    for (int i = 0; i < 5; ++i) {
        scenes.reset(kSceneMainMenu);
        EXPECT_TRUE(previous.expired());
        EXPECT_EQ(&main, &scenes.get(kSceneMain));
        auto node = scenes.get(kSceneMainMenu).newModel(*models.front(), ModelUsage::GUI);
        scenes.get(kSceneMainMenu).addRoot(node);
        previous = node;
    }
}
} // namespace
