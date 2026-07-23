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

#include <limits>

#include "../fixtures/engine.h"

#include "reone/game/action/closedoor.h"
#include "reone/game/action/unlockobject.h"
#include "reone/game/game.h"
#include "reone/game/gui/areatransition.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/gui/dialog.h"
#include "reone/game/gui/hud.h"
#include "reone/game/gui/statussummary.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/reputes.h"
#include "reone/game/script/routines.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/walkmesh.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/trigger.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace reone::game {

class TestStatusSummary : public StatusSummary {
public:
    using StatusSummary::StatusSummary;

    const std::string &resRef() const { return _resRef; }
    void injectGUI(std::shared_ptr<gui::IGUI> gui) { _gui = std::move(gui); }
    void setVisible(bool visible) { _visible = visible; }
    std::string formatDescription(
        StatusSummaryCategory category,
        const StatusSummaryEntry &entry,
        const std::string &authoredText) const {
        return descriptionText(category, entry, authoredText);
    }
};

class TestConversation : public Conversation {
public:
    using Conversation::Conversation;

    void applyStatusSummaryEntriesForTest(const resource::Dialog::EntryReply &node) {
        applyStatusSummaryEntries(node);
    }

private:
    void setReplyLines(std::vector<std::string>) override {}
    void setMessage(std::string) override {}
};

class MixedStuntTestAccess {
public:
    static void loadParticipants(DialogGUI &gui, const std::shared_ptr<resource::Dialog> &dialog) {
        gui._dialog = dialog;
        gui.loadStuntParticipants();
    }

    static size_t participantCount(const DialogGUI &gui) {
        return gui._participantByTag.size();
    }

    static std::shared_ptr<Creature> participantCreature(DialogGUI &gui, const std::string &tag) {
        return gui._participantByTag.at(tag).creature;
    }

    static std::shared_ptr<graphics::Model> participantModel(DialogGUI &gui, const std::string &tag) {
        return gui._participantByTag.at(tag).model;
    }

    static bool applyAnimation(DialogGUI &gui, const std::string &tag, int ordinal) {
        auto animation = gui.getStuntParticipantAnimation(tag, ordinal);
        return animation && gui.enterMixedStunt(gui._participantByTag.at(tag), animation);
    }

    static bool isActive(DialogGUI &gui, const std::string &tag) {
        return gui._participantByTag.at(tag).mixedStuntActive;
    }

    static void restoreForEntry(DialogGUI &gui, const resource::Dialog::EntryReply &entry) {
        gui._currentEntry = &entry;
        gui.restoreInactiveStuntParticipants();
    }

    static void finish(DialogGUI &gui) {
        gui.onFinish();
    }
};

} // namespace reone::game

std::pair<std::string, std::string> reone::game::TestGameModule::scheduledTransition(const Game &game) {
    return {game._nextModule, game._nextEntry};
}

namespace {

class TestAreaTransition : public AreaTransition {
public:
    using AreaTransition::AreaTransition;
    using AreaTransition::preload;
};

class PresentationLifecycleConversation : public Conversation {
public:
    using Conversation::Conversation;

    int startCount {0};
    int finishCount {0};
    int entryCount {0};

private:
    void loadEntry(int, bool) override { ++entryCount; }
    void setReplyLines(std::vector<std::string>) override {}
    void setMessage(std::string) override {}
    void onStart() override { ++startCount; }
    void onFinish() override { ++finishCount; }
};

class RoutingConversation : public Conversation {
public:
    using Conversation::Conversation;

    int presentedEntryCount {0};
    std::vector<std::string> messages;

private:
    void setReplyLines(std::vector<std::string>) override {}
    void setMessage(std::string message) override { messages.push_back(std::move(message)); }
    void onLoadEntry() override { ++presentedEntryCount; }
};

class TestCreature : public Creature {
public:
    using Creature::Creature;

    void setSceneNode(std::shared_ptr<scene::SceneNode> node) {
        _sceneNode = std::move(node);
    }
};
std::pair<std::string, std::string> scheduledTransition(TestEngine &engine, const Game &game) {
    return engine.gameModule().scheduledTransition(game);
}

std::shared_ptr<TwoDA> makeBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    builder.row({"", "", "", "", "", "", "I_Credits", "", "", "", "", ""});
    builder.row({"", "", "", "", "", "", "I_Datapad", "", "", "", "", ""});
    builder.row({"", "", "", "", "", "2", "I_Disguise", "", "", "", "-1", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> makeAppearanceTable() {
    TwoDA::Builder builder;
    builder.columns({"modeltype", "walkdist", "rundist", "footsteptype", "envmap", "race", "racetex"});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> makeReputeTable() {
    TwoDA::Builder builder;
    builder.columns({"label", "hostile_1", "friendly_1", "hostile_2", "friendly_2", "neutral"});
    builder.row({"Player", "0", "100", "0", "100", "50"});
    builder.row({"Hostile_1", "100", "0", "0", "0", "50"});
    builder.row({"Friendly_1", "0", "100", "0", "0", "50"});
    builder.row({"Hostile_2", "0", "0", "100", "0", "50"});
    builder.row({"Friendly_2", "0", "0", "0", "100", "50"});
    builder.row({"Neutral", "50", "50", "50", "50", "100"});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> makeGenericDoorsTable() {
    TwoDA::Builder builder;
    builder.columns({"modelname"});
    builder.row({""});
    builder.row({"testdoor"});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> makePlotTable() {
    TwoDA::Builder builder;
    builder.columns({"label", "xp"});
    builder.row({"journal_plot", "1000"});
    builder.row({"dialog_plot", "1000"});
    builder.row({"explicit_plot", "500"});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Gff> makeJournalWithPlotXP() {
    auto entry = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {
            Gff::Field::newDword("ID", 70),
            Gff::Field::newFloat("XP_Percentage", 0.2f)});
    auto category = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {
            Gff::Field::newCExoString("Tag", "journal_plot"),
            Gff::Field::newInt("PlotIndex", 0),
            Gff::Field::newList("EntryList", {entry})});
    return std::make_shared<Gff>(
        0xffffffff,
        std::vector<Gff::Field> {
            Gff::Field::newList("Categories", {category})});
}

scene::MockSceneGraph &testSceneGraph(TestEngine &engine) {
    static NiceMock<scene::MockSceneGraph> graph;
    static bool initialized = false;
    if (!initialized) {
        EXPECT_CALL(engine.sceneModule().graphs(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(ReturnRef(graph));
        ON_CALL(graph, newTrigger(_))
            .WillByDefault(Invoke([&engine](std::vector<glm::vec3> geometry) {
                return std::make_shared<scene::TriggerSceneNode>(
                    std::move(geometry),
                    graph,
                    engine.services().graphics,
                    engine.services().audio,
                    engine.services().resource);
            }));
        ON_CALL(graph, testWalk(_, _, _, _))
            .WillByDefault(Return(false));
        ON_CALL(graph, testElevation(_, _))
            .WillByDefault(Invoke([](const glm::vec3 &position, scene::Collision &collision) {
                collision.intersection = position;
                collision.intersection.z = 0.0f;
                collision.material = 0;
                collision.user = nullptr;
                return true;
            }));
        initialized = true;
    }
    return graph;
}

std::shared_ptr<graphics::Walkmesh> makeDoorWalkmesh() {
    auto walkmesh = std::make_shared<graphics::Walkmesh>();
    walkmesh->add(graphics::Walkmesh::Face {
        0,
        0,
        {glm::vec3(-2.0f, -0.5f, 0.0f),
         glm::vec3(2.0f, -0.5f, 3.0f),
         glm::vec3(2.0f, 0.5f, 0.0f)},
        glm::vec3(0.0f, 0.0f, 1.0f)});
    return walkmesh;
}

std::shared_ptr<Door> makeTransitionDoor(
    Game &game,
    TestEngine &engine,
    uint8_t linkedToFlags = 1,
    std::string linkedToModule = "destination_module",
    std::string linkedTo = "destination_waypoint",
    glm::vec3 position = glm::vec3(0.0f),
    float bearing = 0.0f) {

    testSceneGraph(engine);
    auto walkmesh = makeDoorWalkmesh();
    EXPECT_CALL(engine.resourceModule().twoDas(), get("genericdoors"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeGenericDoorsTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_))
        .Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().walkmeshes(), get("testdoor0", ResType::Dwk))
        .Times(AnyNumber())
        .WillRepeatedly(Return(walkmesh));

    auto gff = Gff::Builder()
                   .field(Gff::Field::newByte("GenericType", 1))
                   .field(Gff::Field::newByte("Static", 0))
                   .field(Gff::Field::newFloat("X", position.x))
                   .field(Gff::Field::newFloat("Y", position.y))
                   .field(Gff::Field::newFloat("Z", position.z))
                   .field(Gff::Field::newFloat("Bearing", bearing))
                   .field(Gff::Field::newByte("LinkedToFlags", linkedToFlags))
                   .field(Gff::Field::newResRef("LinkedToModule", std::move(linkedToModule)))
                   .field(Gff::Field::newCExoString("LinkedTo", std::move(linkedTo)))
                   .field(Gff::Field::newCExoLocString("TransitionDestin", -1, "Destination Area"))
                   .build();
    auto door = game.newDoor();
    door->deserialize(*gff);
    return door;
}

std::shared_ptr<Creature> makeMovingCreature(Game &game, TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_))
        .Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto gff = Gff::Builder()
                   .field(Gff::Field::newDword("Appearance_Type", 0))
                   .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                   .field(Gff::Field::newByte("BodyBag", 0xff))
                   .field(Gff::Field::newByte("PerceptionRange", 0xff))
                   .build();
    auto creature = game.newCreature();
    creature->deserialize(*gff);
    return creature;
}

std::shared_ptr<Gff> makeTransitionTriggerGff(
    std::string linkedToModule,
    std::string linkedTo,
    std::string onEnter = "",
    std::optional<std::string> transitionDestin = std::nullopt) {

    std::vector<std::shared_ptr<Gff>> geometry;
    for (const auto &point : std::vector<glm::vec2> {
             {-1.0f, -1.0f},
             {-1.0f, 1.0f},
             {1.0f, 1.0f},
             {1.0f, -1.0f}}) {
        geometry.push_back(
            Gff::Builder()
                .field(Gff::Field::newFloat("PointX", point.x))
                .field(Gff::Field::newFloat("PointY", point.y))
                .field(Gff::Field::newFloat("PointZ", 0.0f))
                .build());
    }

    Gff::Builder builder;
    builder.field(Gff::Field::newInt("Type", 1))
        .field(Gff::Field::newByte("LinkedToFlags", 2))
        .field(Gff::Field::newResRef("LinkedToModule", std::move(linkedToModule)))
        .field(Gff::Field::newCExoString("LinkedTo", std::move(linkedTo)))
        .field(Gff::Field::newList("Geometry", std::move(geometry)));
    if (!onEnter.empty()) {
        builder.field(Gff::Field::newResRef("ScriptOnEnter", std::move(onEnter)));
    }
    if (transitionDestin) {
        builder.field(Gff::Field::newCExoLocString("TransitionDestin", -1, std::move(*transitionDestin)));
    }
    return builder.build();
}

std::shared_ptr<script::ScriptProgram> makeStartNewModuleScript(
    std::string module,
    std::string waypoint) {

    auto program = std::make_shared<script::ScriptProgram>("override_transition");
    for (int i = 0; i < 6; ++i) {
        program->add(script::Instruction::newCONSTS(""));
    }
    program->add(script::Instruction::newCONSTS(std::move(waypoint)));
    program->add(script::Instruction::newCONSTS(std::move(module)));
    program->add(script::Instruction::newACTION(509, 8)); // StartNewModule
    program->add(script::Instruction(script::InstructionType::RETN));
    return program;
}

std::shared_ptr<Gff> makeDisguiseItemGff(int appearance) {
    auto property = Gff::Builder()
                        .field(Gff::Field::newWord("PropertyName", static_cast<int>(ItemProperty::Disguise)))
                        .field(Gff::Field::newWord("Subtype", appearance))
                        .build();
    return Gff::Builder()
        .field(Gff::Field::newInt("BaseItem", 2))
        .field(Gff::Field::newList("PropertiesList", {std::move(property)}))
        .build();
}

std::shared_ptr<Item> makeItem(Game &game, std::string tag, int baseItem, int stackSize) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", std::move(tag)))
                   .field(Gff::Field::newInt("BaseItem", baseItem))
                   .field(Gff::Field::newWord("StackSize", stackSize))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff);
    item->setDropable(true);
    return item;
}

std::shared_ptr<graphics::Animation> makeAnimation(std::string name) {
    auto root = std::make_shared<graphics::ModelNode>(
        0,
        "root_node",
        glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        false,
        nullptr);
    root->vectorTracks()[graphics::ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
    root->vectorTracks()[graphics::ControllerTypes::position].add(1.0f, glm::vec3(1.0f));
    return std::make_shared<graphics::Animation>(
        std::move(name),
        1.0f,
        0.0f,
        "root_node",
        std::move(root),
        std::vector<graphics::Animation::Event>());
}

std::shared_ptr<graphics::Model> makeModel(
    std::string name,
    std::vector<std::shared_ptr<graphics::Animation>> animations) {
    auto root = std::make_shared<graphics::ModelNode>(
        0,
        "root_node",
        glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        true,
        nullptr);
    return std::make_shared<graphics::Model>(
        std::move(name),
        0,
        std::move(root),
        std::move(animations),
        "",
        1.0f);
}

std::shared_ptr<Dialog> makeRoutingDialog() {
    auto dialog = std::make_shared<Dialog>();
    dialog->startEntries.push_back(Dialog::EntryReplyLink {0});

    Dialog::EntryReply routing;
    routing.delay = -1;
    routing.cameraId = 0;
    routing.replies.push_back(Dialog::EntryReplyLink {0});
    dialog->entries.push_back(std::move(routing));

    Dialog::EntryReply visible;
    visible.text = "visible";
    visible.delay = 30;
    dialog->entries.push_back(std::move(visible));

    Dialog::EntryReply continuation;
    continuation.entries.push_back(Dialog::EntryReplyLink {1});
    dialog->replies.push_back(std::move(continuation));
    return dialog;
}

} // namespace

TEST(Conversation, should_finish_active_presentation_before_starting_replacement) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    PresentationLifecycleConversation conversation(game, engine.services());
    auto first = std::make_shared<Dialog>();
    auto second = std::make_shared<Dialog>();
    auto firstOwner = game.newCreature();
    auto secondOwner = game.newCreature();
    first->startEntries.push_back(Dialog::EntryReplyLink {});
    second->startEntries.push_back(Dialog::EntryReplyLink {});

    conversation.start(first, firstOwner);
    EXPECT_TRUE(firstOwner->isInConversation());
    conversation.start(second, secondOwner);

    EXPECT_EQ(conversation.startCount, 2);
    EXPECT_EQ(conversation.entryCount, 2);
    EXPECT_EQ(conversation.finishCount, 1);
    EXPECT_FALSE(firstOwner->isInConversation());
    EXPECT_TRUE(secondOwner->isInConversation());

    conversation.cleanupForModuleTransition();
    EXPECT_FALSE(secondOwner->isInConversation());
}

TEST(Conversation, should_advance_script_only_auto_routing_entry_without_presenting_it) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    RoutingConversation conversation(game, engine.services());

    conversation.start(makeRoutingDialog(), nullptr);

    ASSERT_EQ(conversation.messages.size(), 2);
    EXPECT_EQ(conversation.messages[0], "");
    EXPECT_EQ(conversation.messages[1], "visible");
    EXPECT_EQ(conversation.presentedEntryCount, 1);
}

TEST(Conversation, should_present_auto_routing_entry_with_authored_presentation_data) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    std::vector<std::function<void(Dialog::EntryReply &)>> addPresentation {
        [](auto &entry) { entry.text = "text"; },
        [](auto &entry) { entry.sound = "sound"; },
        [](auto &entry) { entry.voResRef = "voice"; },
        [](auto &entry) { entry.cameraAnimation = 1200; },
        [](auto &entry) { entry.cameraId = 1; },
        [](auto &entry) { entry.cameraAngle = 1; },
        [](auto &entry) { entry.animations.push_back({"participant", 1200}); },
        [](auto &entry) { entry.delay = 0; },
    };

    for (auto &mutate : addPresentation) {
        auto dialog = makeRoutingDialog();
        mutate(dialog->entries[0]);
        RoutingConversation conversation(game, engine.services());

        conversation.start(dialog, nullptr);

        EXPECT_EQ(conversation.presentedEntryCount, 1);
        ASSERT_EQ(conversation.messages.size(), 1);
    }
}

TEST(Creature, should_hold_completed_external_animation_until_assignment_is_released) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto modelRoot = std::make_shared<graphics::ModelNode>(
        0,
        "root_node",
        glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        true,
        nullptr);
    auto idle = makeAnimation("cpause1");
    auto first = makeAnimation("first");
    auto second = makeAnimation("second");
    std::vector<std::shared_ptr<graphics::Animation>> animations {idle, first, second};
    graphics::Model model("creature", 0, modelRoot, std::move(animations), "", 1.0f);
    graphics::GraphicsOptions graphicsOptions;
    scene::SceneGraph graph(
        "test",
        engine.sceneModule().renderPipelineFactory(),
        graphicsOptions,
        engine.services().graphics,
        engine.services().audio,
        engine.services().resource);
    auto modelNode = graph.newModel(model, scene::ModelUsage::Creature);
    TestCreature creature(1, "test", game, engine.services());
    creature.setSceneNode(modelNode);
    creature.resumeStateDrivenAnimation();

    scene::AnimationProperties properties;
    properties.flags = scene::AnimationFlags::propagate;
    properties.scale = 1.0f;

    ASSERT_TRUE(creature.playExternalAnimation(first, properties));
    modelNode->update(0.4f);
    ASSERT_EQ(modelNode->animationChannels().size(), 1);
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.4f);

    ASSERT_TRUE(creature.playExternalAnimation(first, properties));
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.4f);

    modelNode->update(0.7f);
    creature.update(0.0f);
    EXPECT_EQ(modelNode->activeAnimationName(), "first");
    EXPECT_TRUE(modelNode->isAnimationFinished());
    ASSERT_EQ(modelNode->animationChannels().size(), 1);
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 1.0f);

    auto rootSceneNode = modelNode->getNodeByName("root_node");
    ASSERT_TRUE(rootSceneNode);
    EXPECT_NEAR(rootSceneNode->localTransform()[3].x, 1.0f, 1e-5);

    modelNode->update(0.5f);
    creature.update(0.0f);
    EXPECT_EQ(modelNode->activeAnimationName(), "first");
    EXPECT_TRUE(modelNode->isAnimationFinished());
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 1.0f);
    EXPECT_NEAR(rootSceneNode->localTransform()[3].x, 1.0f, 1e-5);

    ASSERT_TRUE(creature.playExternalAnimation(first, properties));
    EXPECT_EQ(modelNode->activeAnimationName(), "first");
    EXPECT_TRUE(modelNode->isAnimationFinished());
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 1.0f);

    ASSERT_TRUE(creature.playExternalAnimation(second, properties));
    EXPECT_EQ(modelNode->activeAnimationName(), "second");
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.0f);
    EXPECT_FALSE(modelNode->isAnimationFinished());

    creature.update(0.0f);
    modelNode->update(0.0f);
    EXPECT_EQ(modelNode->activeAnimationName(), "second");
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.0f);

    creature.resumeStateDrivenAnimation();
    EXPECT_EQ(modelNode->activeAnimationName(), "cpause1");
}

TEST(DialogGUI, should_prepare_the_real_player_from_a_stunt_model_without_creating_a_duplicate) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    auto originalSceneNode = player->sceneNode();
    auto stuntModel = makeModel("player_stunt", {});
    EXPECT_CALL(engine.resourceModule().models(), get("player_stunt"))
        .WillOnce(Return(stuntModel));

    auto dialog = std::make_shared<Dialog>();
    dialog->animatedCutscene = false;
    dialog->cameraModel.clear();
    dialog->stunts.push_back({"PLAYER", "player_stunt"});
    DialogGUI gui(game, engine.services());

    MixedStuntTestAccess::loadParticipants(gui, dialog);

    ASSERT_EQ(MixedStuntTestAccess::participantCount(gui), 1);
    EXPECT_EQ(MixedStuntTestAccess::participantCreature(gui, "PLAYER"), player);
    EXPECT_EQ(MixedStuntTestAccess::participantModel(gui, "PLAYER"), stuntModel);
    EXPECT_EQ(player->sceneNode(), originalSceneNode);
    EXPECT_FALSE(player->isStuntMode());
}

TEST(DialogGUI, should_hold_mixed_stunt_assignment_and_restore_on_drop_or_teardown) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto idle = makeAnimation("cpause1");
    auto first = makeAnimation("cut001w");
    auto second = makeAnimation("cut002w");
    auto stuntModel = makeModel("player_stunt", {idle, first, second});
    graphics::GraphicsOptions graphicsOptions;
    scene::SceneGraph graph(
        "test",
        engine.sceneModule().renderPipelineFactory(),
        graphicsOptions,
        engine.services().graphics,
        engine.services().audio,
        engine.services().resource);
    auto modelNode = graph.newModel(*stuntModel, scene::ModelUsage::Creature);
    auto player = std::make_shared<TestCreature>(1, "player", game, engine.services());
    player->setSceneNode(modelNode);
    player->setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    player->setFacing(0.75f);
    modelNode->setCullingEnabled(false);
    player->resumeStateDrivenAnimation();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    EXPECT_CALL(engine.resourceModule().models(), get("player_stunt"))
        .WillOnce(Return(stuntModel));

    auto dialog = std::make_shared<Dialog>();
    dialog->stunts.push_back({"PLAYER", "player_stunt"});
    DialogGUI gui(game, engine.services());
    MixedStuntTestAccess::loadParticipants(gui, dialog);

    ASSERT_TRUE(MixedStuntTestAccess::applyAnimation(gui, "PLAYER", 1200));
    EXPECT_TRUE(MixedStuntTestAccess::isActive(gui, "PLAYER"));
    EXPECT_TRUE(player->isStuntMode());
    EXPECT_TRUE(modelNode->isEnabled());
    EXPECT_FALSE(modelNode->isCulled());
    EXPECT_FALSE(modelNode->isCullingEnabled());
    EXPECT_NEAR(modelNode->localTransform()[3].x, 0.0f, 1e-5);
    EXPECT_NEAR(modelNode->localTransform()[3].y, 0.0f, 1e-5);
    EXPECT_NEAR(modelNode->localTransform()[3].z, 0.0f, 1e-5);

    modelNode->update(0.4f);
    ASSERT_EQ(modelNode->animationChannels().size(), 1);
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.4f);

    ASSERT_TRUE(MixedStuntTestAccess::applyAnimation(gui, "PLAYER", 1200));
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.4f);

    modelNode->update(0.7f);
    player->update(0.0f);
    EXPECT_TRUE(MixedStuntTestAccess::isActive(gui, "PLAYER"));
    EXPECT_TRUE(player->isStuntMode());
    EXPECT_TRUE(modelNode->isEnabled());
    EXPECT_FALSE(modelNode->isCulled());
    EXPECT_FALSE(modelNode->isCullingEnabled());
    EXPECT_EQ(modelNode->activeAnimationName(), "cut001w");
    EXPECT_TRUE(modelNode->isAnimationFinished());
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 1.0f);

    auto rootSceneNode = modelNode->getNodeByName("root_node");
    ASSERT_TRUE(rootSceneNode);
    EXPECT_NEAR(rootSceneNode->localTransform()[3].x, 1.0f, 1e-5);

    modelNode->update(0.5f);
    player->update(0.0f);
    EXPECT_EQ(modelNode->activeAnimationName(), "cut001w");
    EXPECT_TRUE(modelNode->isAnimationFinished());
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 1.0f);
    EXPECT_NEAR(rootSceneNode->localTransform()[3].x, 1.0f, 1e-5);

    ASSERT_TRUE(MixedStuntTestAccess::applyAnimation(gui, "PLAYER", 1200));
    EXPECT_EQ(modelNode->activeAnimationName(), "cut001w");
    EXPECT_TRUE(modelNode->isAnimationFinished());
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 1.0f);

    ASSERT_TRUE(MixedStuntTestAccess::applyAnimation(gui, "PLAYER", 1201));
    EXPECT_EQ(modelNode->activeAnimationName(), "cut002w");
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.0f);
    EXPECT_FALSE(modelNode->isAnimationFinished());

    modelNode->update(0.25f);
    ASSERT_TRUE(MixedStuntTestAccess::applyAnimation(gui, "PLAYER", 1201));
    EXPECT_EQ(modelNode->activeAnimationName(), "cut002w");
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.25f);

    Dialog::EntryReply droppedEntry;
    MixedStuntTestAccess::restoreForEntry(gui, droppedEntry);
    EXPECT_FALSE(MixedStuntTestAccess::isActive(gui, "PLAYER"));
    EXPECT_EQ(player->position(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(player->getFacing(), 0.75f);
    EXPECT_FALSE(modelNode->isCullingEnabled());
    EXPECT_EQ(modelNode->activeAnimationName(), "cpause1");
    EXPECT_NEAR(modelNode->localTransform()[3].x, 1.0f, 1e-5);
    EXPECT_NEAR(modelNode->localTransform()[3].y, 2.0f, 1e-5);
    EXPECT_NEAR(modelNode->localTransform()[3].z, 3.0f, 1e-5);

    ASSERT_TRUE(MixedStuntTestAccess::applyAnimation(gui, "PLAYER", 1200));
    MixedStuntTestAccess::finish(gui);
    EXPECT_EQ(MixedStuntTestAccess::participantCount(gui), 0);
    EXPECT_FALSE(player->isStuntMode());
    EXPECT_EQ(player->position(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(player->getFacing(), 0.75f);
    EXPECT_FALSE(modelNode->isCullingEnabled());
    EXPECT_EQ(modelNode->activeAnimationName(), "cpause1");
    EXPECT_NEAR(modelNode->localTransform()[3].x, 1.0f, 1e-5);
    EXPECT_NEAR(modelNode->localTransform()[3].y, 2.0f, 1e-5);
    EXPECT_NEAR(modelNode->localTransform()[3].z, 3.0f, 1e-5);
}

TEST(DialogGUI, should_leave_no_partial_mixed_stunt_state_when_inputs_are_missing) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto dialog = std::make_shared<Dialog>();
    dialog->stunts.push_back({"PLAYER", "missing_stunt"});
    DialogGUI gui(game, engine.services());

    MixedStuntTestAccess::loadParticipants(gui, dialog);
    EXPECT_EQ(MixedStuntTestAccess::participantCount(gui), 0);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    EXPECT_CALL(engine.resourceModule().models(), get("missing_stunt"))
        .WillOnce(Return(nullptr));
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    EXPECT_EQ(MixedStuntTestAccess::participantCount(gui), 0);

    auto sourceModel = makeModel("player_stunt", {makeAnimation("cut001w")});
    dialog->stunts.front().stuntModel = "player_stunt";
    EXPECT_CALL(engine.resourceModule().models(), get("player_stunt"))
        .WillOnce(Return(sourceModel));
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    ASSERT_EQ(MixedStuntTestAccess::participantCount(gui), 1);

    EXPECT_FALSE(MixedStuntTestAccess::applyAnimation(gui, "PLAYER", 1201));
    EXPECT_FALSE(MixedStuntTestAccess::isActive(gui, "PLAYER"));
    EXPECT_FALSE(MixedStuntTestAccess::applyAnimation(gui, "PLAYER", 1200));
    EXPECT_FALSE(MixedStuntTestAccess::isActive(gui, "PLAYER"));
    EXPECT_FALSE(player->isStuntMode());
}

TEST(Object, should_convert_credits_to_party_gold_when_looted_by_party_member) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    // Item deserialization incidentally looks up inventory icons; no texture
    // is needed.
    EXPECT_CALL(engine.resourceModule().textures(), get(AnyOf("ii_credits_000", "ii_datapad_000"), _))
        .Times(AnyNumber());

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    auto footlocker = game.newPlaceable();
    footlocker->addItem(makeItem(game, "g_i_credits001", 0, 5));
    footlocker->addItem(makeItem(game, "g_i_credits002", 0, 10));
    footlocker->addItem(makeItem(game, "g_i_datapad001", 1, 1));

    footlocker->moveDropableItemsTo(*player);

    EXPECT_EQ(game.party().gold(), 15);
    ASSERT_EQ(player->items().size(), 1);
    EXPECT_EQ(player->items().front()->tag(), "g_i_datapad001");
    EXPECT_EQ(player->items().front()->stackSize(), 1);
    EXPECT_TRUE(footlocker->items().empty());
}

TEST(Object, should_move_credits_as_items_when_destination_is_not_in_party) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    // Item deserialization incidentally looks up inventory icons; no texture
    // is needed.
    EXPECT_CALL(engine.resourceModule().textures(), get("ii_credits_000", _))
        .Times(AnyNumber());

    auto thug = game.newCreature();

    auto footlocker = game.newPlaceable();
    footlocker->addItem(makeItem(game, "g_i_credits001", 0, 5));

    footlocker->moveDropableItemsTo(*thug);

    EXPECT_EQ(game.party().gold(), 0);
    ASSERT_EQ(thug->items().size(), 1);
    EXPECT_EQ(thug->items().front()->tag(), "g_i_credits001");
    EXPECT_TRUE(footlocker->items().empty());
}

TEST(UnlockObjectAction, should_unlock_plot_nonlockable_door_without_opening_it) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto door = game.newDoor();
    door->setLocked(true);
    door->setPlotFlag(true);
    auto action = game.newAction<UnlockObjectAction>(door);

    action->execute(action, *door, 0.0f);

    EXPECT_FALSE(door->isLocked());
    EXPECT_FALSE(door->isOpen());
    EXPECT_TRUE(action->isCompleted());
}

TEST(UnlockObjectAction, should_unlock_plot_nonlockable_placeable_without_opening_it) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto placeable = game.newPlaceable();
    placeable->setLocked(true);
    placeable->setPlotFlag(true);
    auto action = game.newAction<UnlockObjectAction>(placeable);

    action->execute(action, *placeable, 0.0f);

    EXPECT_FALSE(placeable->isLocked());
    EXPECT_FALSE(placeable->isOpen());
    EXPECT_TRUE(action->isCompleted());
}

TEST(UnlockObjectAction, should_be_idempotent_for_unlocked_supported_targets) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto door = game.newDoor();
    auto placeable = game.newPlaceable();
    auto doorAction = game.newAction<UnlockObjectAction>(door);
    auto placeableAction = game.newAction<UnlockObjectAction>(placeable);

    doorAction->execute(doorAction, *door, 0.0f);
    placeableAction->execute(placeableAction, *placeable, 0.0f);

    EXPECT_FALSE(door->isLocked());
    EXPECT_FALSE(door->isOpen());
    EXPECT_TRUE(doorAction->isCompleted());
    EXPECT_FALSE(placeable->isLocked());
    EXPECT_FALSE(placeable->isOpen());
    EXPECT_TRUE(placeableAction->isCompleted());
}

TEST(UnlockObjectAction, should_complete_safely_for_missing_destroyed_or_unsupported_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto actor = game.newCreature();
    auto destroyed = game.newPlaceable();
    destroyed->damage(std::numeric_limits<int>::max(), actor->id());
    destroyed->setLocked(true);

    auto missingAction = game.newAction<UnlockObjectAction>(std::shared_ptr<Object>());
    auto destroyedAction = game.newAction<UnlockObjectAction>(destroyed);
    auto unsupportedAction = game.newAction<UnlockObjectAction>(actor);

    EXPECT_NO_THROW(missingAction->execute(missingAction, *actor, 0.0f));
    EXPECT_NO_THROW(destroyedAction->execute(destroyedAction, *actor, 0.0f));
    EXPECT_NO_THROW(unsupportedAction->execute(unsupportedAction, *actor, 0.0f));

    EXPECT_TRUE(missingAction->isCompleted());
    EXPECT_TRUE(destroyedAction->isCompleted());
    EXPECT_TRUE(destroyed->isLocked());
    EXPECT_TRUE(unsupportedAction->isCompleted());
}

TEST(TransitionPresentationLifecycle, should_construct_and_destroy_hud_before_gameplay_module_exists) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    ASSERT_EQ(game.module(), nullptr);
    EXPECT_NO_THROW({
        auto hud = std::make_unique<HUD>(game, engine.services());
    });
}

TEST(TransitionPresentationLifecycle, should_hide_empty_destination_before_gui_controls_are_loaded) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    AreaTransition transition(game, engine.services());

    EXPECT_NO_THROW(transition.show("Destination Area"));
    ASSERT_TRUE(transition.isVisible());

    EXPECT_NO_THROW(transition.show(""));
    EXPECT_FALSE(transition.isVisible());
}

TEST(StatusSummaryPresentation, should_tolerate_controls_not_yet_loaded_and_defer_snapshot) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    StatusSummaryAccumulator accumulator;
    accumulator.submit(StatusSummaryCategory::Journal);
    TestStatusSummary summary(game, engine.services(), accumulator);

    EXPECT_FALSE(summary.presentPending());
    EXPECT_FALSE(summary.isVisible());
    EXPECT_TRUE(accumulator.pending().entry(StatusSummaryCategory::Journal).active);
    EXPECT_FALSE(accumulator.displayed());
}

TEST(StatusSummaryPresentation, should_select_authored_resource_for_each_game) {
    TestEngine &engine = testEngine();
    StubConsole console;
    StatusSummaryAccumulator k1Accumulator;
    StatusSummaryAccumulator k2Accumulator;
    Game k1(GameID::KotOR, "", engine.options(), engine.services(), console);
    Game k2(GameID::TSL, "", engine.options(), engine.services(), console);

    TestStatusSummary k1Summary(k1, engine.services(), k1Accumulator);
    TestStatusSummary k2Summary(k2, engine.services(), k2Accumulator);

    EXPECT_EQ(k1Summary.resRef(), "statussummary");
    EXPECT_EQ(k2Summary.resRef(), "statussummary_p");
}

TEST(JournalStatusSummary, should_submit_journal_through_game_accumulator) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    game.submitStatusSummary(StatusSummaryCategory::Journal);
    game.submitStatusSummary(StatusSummaryCategory::Journal);

    auto active = game.statusSummary().pending().activeCategories();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active.front(), StatusSummaryCategory::Journal);
}

TEST(StatusSummaryInput, visible_summary_consumes_mouse_down_and_mouse_up) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    StatusSummaryAccumulator accumulator;
    TestStatusSummary summary(game, engine.services(), accumulator);
    auto gui = std::make_shared<NiceMock<gui::MockGUI>>();
    summary.injectGUI(gui);
    summary.setVisible(true);

    auto down = input::Event::newMouseButtonDown(
        {input::MouseButton::Left, true, 1, 10, 10});
    auto up = input::Event::newMouseButtonUp(
        {input::MouseButton::Left, false, 1, 10, 10});
    EXPECT_CALL(*gui, handle(_)).Times(2).WillRepeatedly(Return(false));

    EXPECT_TRUE(summary.handle(down));
    EXPECT_TRUE(summary.handle(up));
}

TEST(StatusSummaryInput, hidden_summary_does_not_consume_world_input) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    StatusSummaryAccumulator accumulator;
    TestStatusSummary summary(game, engine.services(), accumulator);
    auto gui = std::make_shared<NiceMock<gui::MockGUI>>();
    summary.injectGUI(gui);
    summary.setVisible(false);

    EXPECT_CALL(*gui, handle(_)).Times(0);
    EXPECT_FALSE(summary.handle(input::Event::newMouseButtonDown(
        {input::MouseButton::Left, true, 1, 10, 10})));
}

TEST(TransitionPresentationLayout, should_top_anchor_and_horizontally_center_authored_gui) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    TestAreaTransition presentation(game, engine.services());
    NiceMock<gui::MockGUI> gui;

    EXPECT_CALL(gui, setResolution(640, 480));
    EXPECT_CALL(gui, setScaling(gui::GUI::ScalingMode::CenterHorizontal));

    presentation.preload(gui);
}

TEST(TransitionPresentationPortals, should_expose_authored_transitions_without_touching_state) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    testSceneGraph(engine);
    auto area = game.newArea();
    auto leader = makeMovingCreature(game, engine);
    game.party().addMember(kNpcPlayer, leader);
    game.party().setPlayer(leader);

    auto trigger = game.newTrigger();
    trigger->deserialize(*makeTransitionTriggerGff(
        "authored_module",
        "authored_waypoint",
        "",
        "Authored Destination"));
    trigger->setPosition(glm::vec3(4.0f, 0.0f, 0.0f));
    area->add(trigger);
    area->add(leader);

    auto portals = area->transitionPresentationPortals();

    ASSERT_EQ(portals.size(), 1u);
    EXPECT_EQ(portals[0].objectId, trigger->id());
    EXPECT_EQ(portals[0].destination, "Authored Destination");
    ASSERT_EQ(portals[0].points.size(), 4u);
    EXPECT_NEAR(portals[0].points[0].x, 3.0f, 1e-4f);
    EXPECT_NEAR(portals[0].points[0].y, -1.0f, 1e-4f);

    // The presentation query is read-only.
    EXPECT_FALSE(trigger->isTenant(leader));
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));

    area->destroyObject(*trigger);
    area->update(0.0f);
    EXPECT_TRUE(area->transitionPresentationPortals().empty());
}

TEST(TransitionPresentationPortals, should_follow_linked_door_state_without_teleporting) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto door = makeTransitionDoor(game, engine);
    auto leader = makeMovingCreature(game, engine);
    game.party().addMember(kNpcPlayer, leader);
    game.party().setPlayer(leader);
    area->add(door);
    area->add(leader);

    EXPECT_TRUE(area->transitionPresentationPortals().empty()) << "closed door must not present";

    door->open();
    auto portals = area->transitionPresentationPortals();

    ASSERT_EQ(portals.size(), 1u);
    EXPECT_EQ(portals[0].destination, "Destination Area");
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()))
        << "opening the door alone must not schedule travel";

    door->close();
    EXPECT_TRUE(area->transitionPresentationPortals().empty());
}

TEST(TransitionPresentationPortals, should_ignore_non_transitions_and_expose_empty_destinations) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    testSceneGraph(engine);
    auto area = game.newArea();

    auto nonTransition = game.newTrigger();
    nonTransition->deserialize(*makeTransitionTriggerGff("", "", "", "Not a transition"));
    auto emptyDestination = game.newTrigger();
    emptyDestination->deserialize(*makeTransitionTriggerGff("empty_module", "empty_waypoint", "", ""));
    auto missingDestination = game.newTrigger();
    missingDestination->deserialize(*makeTransitionTriggerGff("missing_module", "missing_waypoint"));
    area->add(nonTransition);
    area->add(emptyDestination);
    area->add(missingDestination);

    auto portals = area->transitionPresentationPortals();

    ASSERT_EQ(portals.size(), 2u);
    EXPECT_TRUE(portals[0].destination.empty());
    EXPECT_TRUE(portals[1].destination.empty());
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));
}

TEST(LinkedDoorTransition, should_derive_threshold_from_closed_dwk_and_follow_door_state) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto door = makeTransitionDoor(
        game,
        engine,
        1,
        "destination_module",
        "destination_waypoint",
        glm::vec3(10.0f, 20.0f, 0.0f),
        -glm::half_pi<float>());

    area->add(door);

    auto &triggers = area->getObjectsByType(ObjectType::Trigger);
    ASSERT_EQ(triggers.size(), 1);
    auto trigger = std::static_pointer_cast<Trigger>(triggers.front());
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));
    EXPECT_TRUE(trigger->isLinkedDoorTransition());
    ASSERT_TRUE(trigger->sceneNode());
    EXPECT_EQ(trigger->sceneNode()->type(), scene::SceneNodeType::Trigger);
    EXPECT_FALSE(trigger->isActive());
    EXPECT_TRUE(door->isSelectable());
    EXPECT_EQ(trigger->linkedToModule(), "destination_module");
    EXPECT_EQ(trigger->linkedTo(), "destination_waypoint");
    EXPECT_EQ(trigger->linkedToFlags(), 1);
    EXPECT_EQ(trigger->transitionDestin(), "Destination Area");
    ASSERT_EQ(trigger->geometry().size(), 4);
    EXPECT_EQ(trigger->geometry()[0], glm::vec3(-2.0f, -0.5f, 0.0f));
    EXPECT_EQ(trigger->geometry()[2], glm::vec3(2.0f, 0.5f, 0.0f));

    door->open();

    EXPECT_TRUE(trigger->isActive());
    EXPECT_FALSE(door->isSelectable());
    EXPECT_TRUE(trigger->isIn(glm::vec2(door->position())));
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));

    door->close();

    EXPECT_FALSE(trigger->isActive());

    door->open();

    EXPECT_TRUE(trigger->isActive());
    EXPECT_FALSE(door->isSelectable());
}

TEST(LinkedDoorTransition, should_destroy_generated_threshold_with_its_source_door) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto door = makeTransitionDoor(game, engine);
    auto leader = makeMovingCreature(game, engine);
    leader->setPosition(glm::vec3(0.0f, -1.0f, 0.0f));
    game.party().addMember(kNpcPlayer, leader);
    game.party().setPlayer(leader);

    auto authored = game.newTrigger();
    authored->deserialize(*Gff::Builder().build());
    area->add(authored);
    area->add(door);
    area->add(leader);

    auto &triggers = area->getObjectsByType(ObjectType::Trigger);
    ASSERT_EQ(triggers.size(), 2);
    auto generated = std::static_pointer_cast<Trigger>(triggers.back());
    auto generatedSceneNode = std::static_pointer_cast<scene::TriggerSceneNode>(generated->sceneNode());
    ASSERT_TRUE(generatedSceneNode);
    door->open();
    generated->addTenant(leader);
    ASSERT_TRUE(generated->isActive());
    ASSERT_TRUE(generated->isTenant(leader));

    auto &sceneGraph = testSceneGraph(engine);
    EXPECT_CALL(sceneGraph, removeRoot(A<scene::TriggerSceneNode &>()))
        .WillOnce(Invoke([expected = generatedSceneNode.get()](scene::TriggerSceneNode &node) {
            EXPECT_EQ(&node, expected);
        }));

    area->destroyObject(*door);
    area->update(0.0f);

    ASSERT_EQ(triggers.size(), 1);
    EXPECT_EQ(triggers.front(), authored);
    EXPECT_FALSE(generated->isActive());
    EXPECT_FALSE(generated->isTenant(leader));
    EXPECT_TRUE(generated->linkedToModule().empty());
    EXPECT_TRUE(generated->linkedTo().empty());

    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 0.75f));
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));
}

TEST(LinkedDoorTransition, should_rearm_after_party_unload_and_exit_reentry) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto door = makeTransitionDoor(game, engine);
    auto leader = makeMovingCreature(game, engine);
    leader->setPosition(glm::vec3(0.0f, -1.0f, 0.0f));
    game.party().addMember(kNpcPlayer, leader);
    game.party().setPlayer(leader);
    area->add(door);
    area->add(leader);
    auto trigger = std::static_pointer_cast<Trigger>(area->getObjectsByType(ObjectType::Trigger).front());

    door->open();
    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 0.75f));

    EXPECT_TRUE(trigger->isTenant(leader));
    EXPECT_EQ(trigger->linkedToModule(), "destination_module");
    EXPECT_EQ(trigger->linkedTo(), "destination_waypoint");
    EXPECT_EQ(
        scheduledTransition(engine, game),
        std::make_pair(std::string("destination_module"), std::string("destination_waypoint")));

    // Remaining inside uses the existing tenant latch and does not generate a
    // second enter event while the first transition is pending.
    game.scheduleModuleTransition("sentinel_module", "sentinel_waypoint");
    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(1.0f, 0.0f), false, 0.1f));
    EXPECT_TRUE(trigger->isTenant(leader));
    EXPECT_EQ(
        scheduledTransition(engine, game),
        std::make_pair(std::string("sentinel_module"), std::string("sentinel_waypoint")));

    // Module transitions cache Area/Trigger instances. Party unload removes
    // the old tenant, and loading the party again models revisiting the module.
    area->unloadParty();
    EXPECT_FALSE(trigger->isTenant(leader));
    area->loadParty(glm::vec3(0.0f, -1.0f, 0.0f), 0.0f);
    game.scheduleModuleTransition("", "");
    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 0.75f));
    EXPECT_TRUE(trigger->isTenant(leader));
    EXPECT_EQ(
        scheduledTransition(engine, game),
        std::make_pair(std::string("destination_module"), std::string("destination_waypoint")));

    game.scheduleModuleTransition("", "");
    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 1.0f));
    trigger->update(0.0f);
    EXPECT_FALSE(trigger->isTenant(leader));
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));
    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, -1.0f), false, 0.75f));
    EXPECT_TRUE(trigger->isTenant(leader));
    EXPECT_EQ(
        scheduledTransition(engine, game),
        std::make_pair(std::string("destination_module"), std::string("destination_waypoint")));
}

TEST(LinkedDoorTransition, should_reject_npcs_and_companions) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto door = makeTransitionDoor(game, engine);
    auto leader = makeMovingCreature(game, engine);
    auto companion = makeMovingCreature(game, engine);
    auto npc = makeMovingCreature(game, engine);
    leader->setPosition(glm::vec3(0.0f, -1.0f, 0.0f));
    companion->setPosition(glm::vec3(0.0f, -1.0f, 0.0f));
    npc->setPosition(glm::vec3(0.0f, -1.0f, 0.0f));
    game.party().addMember(kNpcPlayer, leader);
    game.party().setPlayer(leader);
    game.party().addMember(0, companion);
    area->add(door);
    area->add(leader);
    area->add(companion);
    area->add(npc);
    auto trigger = std::static_pointer_cast<Trigger>(area->getObjectsByType(ObjectType::Trigger).front());
    door->open();

    ASSERT_TRUE(area->moveCreature(companion, glm::vec2(0.0f, 1.0f), false, 0.75f));
    EXPECT_FALSE(trigger->isTenant(companion));
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));

    ASSERT_TRUE(area->moveCreature(npc, glm::vec2(0.0f, 1.0f), false, 0.75f));
    EXPECT_FALSE(trigger->isTenant(npc));
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));

    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 0.75f));
    EXPECT_TRUE(trigger->isTenant(leader));
    EXPECT_EQ(
        scheduledTransition(engine, game),
        std::make_pair(std::string("destination_module"), std::string("destination_waypoint")));
}

TEST(LinkedDoorTransition, should_support_authored_cross_module_flag_values) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto flag1Door = makeTransitionDoor(game, engine, 1, "flag1_module", "flag1_waypoint");
    auto flag2Door = makeTransitionDoor(game, engine, 2, "flag2_module", "flag2_waypoint");

    area->add(flag1Door);
    area->add(flag2Door);

    auto &triggers = area->getObjectsByType(ObjectType::Trigger);
    ASSERT_EQ(triggers.size(), 2);
    auto flag1Trigger = std::static_pointer_cast<Trigger>(triggers[0]);
    auto flag2Trigger = std::static_pointer_cast<Trigger>(triggers[1]);
    EXPECT_EQ(flag1Trigger->linkedToFlags(), 1);
    EXPECT_EQ(flag1Trigger->linkedToModule(), "flag1_module");
    EXPECT_EQ(flag1Trigger->linkedTo(), "flag1_waypoint");
    EXPECT_EQ(flag2Trigger->linkedToFlags(), 2);
    EXPECT_EQ(flag2Trigger->linkedToModule(), "flag2_module");
    EXPECT_EQ(flag2Trigger->linkedTo(), "flag2_waypoint");
}

TEST(LinkedDoorTransition, should_reject_unlinked_unsupported_or_incomplete_metadata) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto unlinkedDoor = makeTransitionDoor(game, engine, 0);
    auto unsupportedDoor = makeTransitionDoor(game, engine, 3);
    auto missingModuleDoor = makeTransitionDoor(game, engine, 1, "", "destination_waypoint");
    auto missingTargetDoor = makeTransitionDoor(game, engine, 2, "destination_module", "");

    area->add(unlinkedDoor);
    area->add(unsupportedDoor);
    area->add(missingModuleDoor);
    area->add(missingTargetDoor);
    unlinkedDoor->open();

    EXPECT_TRUE(area->getObjectsByType(ObjectType::Trigger).empty());
}

TEST(LinkedDoorTransition, should_preserve_reusable_authored_type1_trigger_lifecycle) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    game.initLocalServices();
    testSceneGraph(engine);
    auto onEnter = makeStartNewModuleScript("script_module", "script_waypoint");
    EXPECT_CALL(engine.resourceModule().scripts(), get("override_transition"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(onEnter));
    auto gff = makeTransitionTriggerGff(
        "authored_module",
        "authored_waypoint",
        "override_transition");
    auto trigger = game.newTrigger();
    trigger->deserialize(*gff);
    auto area = game.newArea();
    auto npc = makeMovingCreature(game, engine);
    npc->setPosition(glm::vec3(0.0f, -2.0f, 0.0f));
    area->add(trigger);
    area->add(npc);

    EXPECT_FALSE(trigger->isLinkedDoorTransition());
    EXPECT_TRUE(trigger->isActive());
    ASSERT_TRUE(area->moveCreature(npc, glm::vec2(0.0f, 1.0f), false, 1.25f));
    EXPECT_TRUE(trigger->isTenant(npc));
    EXPECT_EQ(
        scheduledTransition(engine, game),
        std::make_pair(std::string("authored_module"), std::string("authored_waypoint")));

    ASSERT_TRUE(area->moveCreature(npc, glm::vec2(0.0f, 1.0f), false, 2.0f));
    trigger->update(0.0f);
    EXPECT_FALSE(trigger->isTenant(npc));

    ASSERT_TRUE(area->moveCreature(npc, glm::vec2(0.0f, -1.0f), false, 0.5f));
    EXPECT_TRUE(trigger->isTenant(npc));
}

TEST(LinkedDoorTransition, should_allow_normal_close_action_and_reactivate_when_reopened) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto area = game.newArea();
    auto linkedDoor = makeTransitionDoor(game, engine);
    auto leader = makeMovingCreature(game, engine);
    leader->setPosition(glm::vec3(0.0f, -1.0f, 0.0f));
    game.party().addMember(kNpcPlayer, leader);
    game.party().setPlayer(leader);
    area->add(linkedDoor);
    area->add(leader);
    auto trigger = std::static_pointer_cast<Trigger>(area->getObjectsByType(ObjectType::Trigger).front());
    linkedDoor->open();
    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 0.75f));
    ASSERT_TRUE(trigger->isTenant(leader));
    game.scheduleModuleTransition("", "");
    auto linkedClose = game.newAction<CloseDoorAction>(linkedDoor);

    linkedClose->execute(linkedClose, *linkedDoor, 0.0f);
    trigger->update(0.0f);

    EXPECT_TRUE(linkedClose->isCompleted());
    EXPECT_FALSE(linkedDoor->isOpen());
    EXPECT_FALSE(trigger->isActive());
    EXPECT_FALSE(trigger->isTenant(leader));

    linkedDoor->open();

    EXPECT_TRUE(linkedDoor->isOpen());
    EXPECT_TRUE(trigger->isActive());
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));

    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 1.0f));
    EXPECT_EQ(scheduledTransition(engine, game), std::make_pair(std::string(), std::string()));
    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, -1.0f), false, 0.75f));
    EXPECT_TRUE(trigger->isTenant(leader));
    EXPECT_EQ(
        scheduledTransition(engine, game),
        std::make_pair(std::string("destination_module"), std::string("destination_waypoint")));
}

TEST(Party, should_award_xp_to_pool_and_sync_current_members) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    auto companion = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addMember(0, companion);

    game.party().awardXP(100, XPSource::Plot);

    EXPECT_EQ(game.party().xp(), 100);
    EXPECT_EQ(player->xp(), 100);
    EXPECT_EQ(companion->xp(), 100);
    EXPECT_EQ(
        game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP).amount,
        100);
}
TEST(Party, should_set_xp_pool_and_sync_current_members) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    auto companion = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addMember(0, companion);

    game.party().setXP(250);

    EXPECT_EQ(game.party().xp(), 250);
    EXPECT_EQ(player->xp(), 250);
    EXPECT_EQ(companion->xp(), 250);
}
TEST(Party, should_reset_xp_pool_to_fresh_game_baseline) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto previousPlayer = game.newCreature();
    game.party().addMember(kNpcPlayer, previousPlayer);
    game.party().setPlayer(previousPlayer);
    game.party().setXP(5000);

    game.party().reset();

    EXPECT_EQ(game.party().xp(), 0);
    EXPECT_TRUE(game.party().isEmpty());

    auto newPlayer = game.newCreature();
    game.party().addMember(kNpcPlayer, newPlayer);
    game.party().setPlayer(newPlayer);

    EXPECT_EQ(newPlayer->xp(), 0);
}
TEST(Party, should_restore_saved_pool_after_reset_and_sync_late_member) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    game.party().setXP(5000);
    game.party().reset();

    // deserializeParty adds the player before applying PT_XP_POOL through setXP.
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setXP(750);

    auto lateCompanion = game.newCreature();
    game.party().addMember(0, lateCompanion);

    EXPECT_EQ(game.party().xp(), 750);
    EXPECT_EQ(player->xp(), 750);
    EXPECT_EQ(lateCompanion->xp(), 750);
}
TEST(Party, should_sync_member_added_after_xp_gain) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    game.party().awardXP(100, XPSource::Combat);

    auto latecomer = game.newCreature();
    game.party().addMember(0, latecomer);

    EXPECT_EQ(latecomer->xp(), 100);
    EXPECT_EQ(game.party().xp(), 100);
    EXPECT_TRUE(game.statusSummary().pending().empty());
}
TEST(Party, should_keep_non_party_creature_xp_local) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    auto thug = game.newCreature();
    thug->giveXP(50);
    game.party().awardXP(100, XPSource::Combat);

    EXPECT_EQ(thug->xp(), 50);
    EXPECT_EQ(player->xp(), 100);
    EXPECT_EQ(game.party().xp(), 100);
    EXPECT_TRUE(game.statusSummary().pending().empty());
}

TEST(XPStatusSummary, should_accumulate_each_positive_party_award_once) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    auto companion = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addMember(0, companion);

    game.party().awardXP(100, XPSource::Plot);
    game.party().awardXP(250, XPSource::Plot);

    EXPECT_EQ(game.party().xp(), 350);
    EXPECT_EQ(player->xp(), 350);
    EXPECT_EQ(companion->xp(), 350);
    const auto &plotXP = game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP);
    EXPECT_TRUE(plotXP.active);
    EXPECT_EQ(plotXP.amount, 350);
    EXPECT_EQ(game.statusSummary().pending().activeCategories().size(), 1u);
}

TEST(XPStatusSummary, should_preserve_zero_award_synchronization_without_notifying) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setXP(100);

    game.party().awardXP(0, XPSource::Plot);

    EXPECT_EQ(game.party().xp(), 100);
    EXPECT_EQ(player->xp(), 100);
    EXPECT_TRUE(game.statusSummary().pending().empty());
}

TEST(XPStatusSummary, should_preserve_negative_accounting_without_received_notification) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    auto companion = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addMember(0, companion);
    game.party().setXP(100);

    game.party().awardXP(-25, XPSource::Plot);

    EXPECT_EQ(game.party().xp(), 75);
    EXPECT_EQ(player->xp(), 75);
    EXPECT_EQ(companion->xp(), 75);
    EXPECT_TRUE(game.statusSummary().pending().empty());
}

TEST(XPStatusSummary, should_not_notify_when_party_pool_is_set_directly) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    game.party().setXP(350);

    EXPECT_EQ(game.party().xp(), 350);
    EXPECT_EQ(player->xp(), 350);
    EXPECT_TRUE(game.statusSummary().pending().empty());
}

TEST(XPStatusSummary, combat_source_updates_pool_without_plot_notification) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    game.party().awardXP(200, XPSource::Combat);

    EXPECT_EQ(200, game.party().xp());
    EXPECT_EQ(200, player->xp());
    EXPECT_TRUE(game.statusSummary().pending().empty());
}

TEST(XPStatusSummary, console_source_intentionally_uses_plot_presentation) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    game.party().awardXP(100, XPSource::Console);

    EXPECT_EQ(100, game.party().xp());
    const auto &plotXP = game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP);
    EXPECT_TRUE(plotXP.active);
    EXPECT_EQ(100, plotXP.amount);
}

TEST(ScriptedPlotXP, give_plot_xp_is_registered_for_k1_and_k2) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines k1Routines(GameID::KotOR, &game, &engine.services());
    Routines k2Routines(GameID::TSL, &game, &engine.services());

    k1Routines.init();
    k2Routines.init();

    EXPECT_EQ(714, k1Routines.getIndexByName("GivePlotXP"));
    EXPECT_EQ(714, k2Routines.getIndexByName("GivePlotXP"));
    EXPECT_EQ("GivePlotXP", k1Routines.get(714).name());
    EXPECT_EQ("GivePlotXP", k2Routines.get(714).name());
}

TEST(ScriptedPlotXP, give_plot_xp_resolves_label_case_insensitively_and_awards_percentage) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("plot"))
        .WillOnce(Return(makePlotTable()));
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;

    routines.get(714).invoke(
        {script::Variable::ofString("EXPLICIT_PLOT"), script::Variable::ofInt(20)},
        execution);

    EXPECT_EQ(100, game.party().xp());
    EXPECT_EQ(100, player->xp());
    const auto &plotXP = game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP);
    EXPECT_TRUE(plotXP.active);
    EXPECT_EQ(100, plotXP.amount);
}

TEST(ScriptedPlotXP, give_xp_to_active_party_creature_is_a_plot_award) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    script::ExecutionContext execution;

    routines.get(393).invoke(
        {script::Variable::ofObject(player->id()), script::Variable::ofInt(75)},
        execution);

    EXPECT_EQ(75, game.party().xp());
    EXPECT_EQ(
        75,
        game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP).amount);
}

TEST(ScriptedPlotXP, missing_plot_and_zero_percentage_do_not_notify) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("plot"))
        .WillOnce(Return(makePlotTable()));
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    game.awardPlotXP("missing_plot", 100);
    game.awardPlotXP("explicit_plot", 0);
    game.awardPlotXPByIndex(-1, 0.5f);

    EXPECT_EQ(0, game.party().xp());
    EXPECT_EQ(0, player->xp());
    EXPECT_TRUE(game.statusSummary().pending().empty());
}

TEST(ScriptedPlotXP, journal_dialogue_and_explicit_contributions_accumulate_in_one_pending_row) {
    TestEngine &engine = testEngine();
    StubConsole console;
    EXPECT_CALL(engine.resourceModule().gffs(), get("global", ResType::Jrl))
        .WillOnce(Return(makeJournalWithPlotXP()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("plot"))
        .Times(3)
        .WillRepeatedly(Return(makePlotTable()));
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    TestConversation conversation(game, engine.services());
    Dialog::EntryReply node;
    node.quest = "journal_plot";
    node.questEntry = 70;
    node.plotIndex = 1;
    node.plotXPPercentage = 0.15f;

    conversation.applyStatusSummaryEntriesForTest(node);
    game.awardPlotXP("explicit_plot", 20);

    EXPECT_EQ(450, game.party().xp());
    EXPECT_EQ(450, player->xp());
    const auto active = game.statusSummary().pending().activeCategories();
    EXPECT_EQ(
        active,
        (std::vector<StatusSummaryCategory> {
            StatusSummaryCategory::Journal,
            StatusSummaryCategory::PlotXP}));
    EXPECT_EQ(
        450,
        game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP).amount);
}

TEST(ScriptedPlotXP, rejected_duplicate_journal_entry_does_not_award_again) {
    TestEngine &engine = testEngine();
    StubConsole console;
    EXPECT_CALL(engine.resourceModule().gffs(), get("global", ResType::Jrl))
        .WillOnce(Return(makeJournalWithPlotXP()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("plot"))
        .WillOnce(Return(makePlotTable()));
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    EXPECT_TRUE(game.journal().addEntry("journal_plot", 70));
    EXPECT_FALSE(game.journal().addEntry("journal_plot", 70));

    EXPECT_EQ(200, game.party().xp());
    EXPECT_EQ(
        200,
        game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP).amount);
}

TEST(XPStatusSummary, should_format_authored_plot_xp_text_without_mutating_global_token) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    StatusSummaryAccumulator accumulator;
    TestStatusSummary summary(game, engine.services(), accumulator);
    StatusSummaryEntry entry;
    entry.active = true;
    entry.amount = 350;
    game.setCustomToken(0, "existing");

    EXPECT_EQ(
        summary.formatDescription(
            StatusSummaryCategory::PlotXP,
            entry,
            "Experience: <CUSTOM0>"),
        "Experience: 350");
    EXPECT_EQ(game.substituteCustomTokens("<CUSTOM0>"), "existing");
}

TEST(XPStatusSummary, should_leave_authored_journal_text_unchanged) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    StatusSummaryAccumulator accumulator;
    TestStatusSummary summary(game, engine.services(), accumulator);

    EXPECT_EQ(
        summary.formatDescription(
            StatusSummaryCategory::Journal,
            StatusSummaryEntry(),
            "Authored Journal Text"),
        "Authored Journal Text");
}

TEST(Party, should_route_item_acquired_by_companion_to_shared_player_inventory) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());

    auto player = game.newCreature();
    auto companion = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().addMember(0, companion);

    auto receiver = game.party().sharedInventoryReceiver(companion);
    ASSERT_EQ(receiver.get(), player.get());
    receiver->addItem(makeItem(game, "g_i_datapad001", 1, 1));

    ASSERT_EQ(player->items().size(), 1);
    EXPECT_EQ(player->items().front()->tag(), "g_i_datapad001");
    EXPECT_TRUE(companion->items().empty());
}

TEST(Party, should_keep_item_acquired_by_player_in_shared_inventory) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    EXPECT_EQ(game.party().sharedInventoryReceiver(player).get(), player.get());
}

TEST(Party, should_keep_item_acquired_by_non_party_creature_local) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    auto thug = game.newCreature();
    auto receiver = game.party().sharedInventoryReceiver(thug);
    ASSERT_EQ(receiver.get(), thug.get());
    receiver->addItem(makeItem(game, "g_i_datapad001", 1, 1));

    ASSERT_EQ(thug->items().size(), 1);
    EXPECT_TRUE(player->items().empty());
}

TEST(Party, should_not_share_inventory_of_non_party_placeable) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto player = game.newCreature();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);

    auto footlocker = game.newPlaceable();
    EXPECT_EQ(game.party().sharedInventoryReceiver(footlocker).get(), footlocker.get());
}

TEST(Object, should_restore_saved_appearance_after_unequipping_loaded_disguise) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _))
        .Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().models(), get(_))
        .Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto creatureGff = Gff::Builder()
                           .field(Gff::Field::newWord("Appearance_Type", 2))
                           .field(Gff::Field::newByte("PM_IsDisguised", 1))
                           .field(Gff::Field::newWord("PM_Appearance", 1))
                           .field(Gff::Field::newList("Equip_ItemList", {makeDisguiseItemGff(2)}))
                           .build();
    auto creature = game.newCreature();
    creature->deserialize(*creatureGff);

    ASSERT_EQ(creature->appearance(), 2);
    auto disguise = creature->getEquippedItem(InventorySlots::body);
    ASSERT_TRUE(disguise);

    creature->unequip(disguise);

    EXPECT_EQ(creature->appearance(), 1);
}

TEST(Reputes, should_use_authored_creature_faction_dispositions) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<MockTwoDAs> twoDas;
    ON_CALL(twoDas, get("repute")).WillByDefault(Return(makeReputeTable()));

    Reputes reputes(twoDas);
    reputes.init();

    auto friendly1 = game.newCreature();
    friendly1->setFaction(Faction::Friendly1);
    auto friendly2 = game.newCreature();
    friendly2->setFaction(Faction::Friendly2);
    auto hostile1 = game.newCreature();
    hostile1->setFaction(Faction::Hostile1);
    auto neutral = game.newCreature();
    neutral->setFaction(Faction::Neutral);

    EXPECT_TRUE(reputes.getIsEnemy(*friendly1, *friendly2));
    EXPECT_TRUE(reputes.getIsEnemy(*friendly2, *friendly1));
    EXPECT_TRUE(reputes.getIsEnemy(*friendly1, *hostile1));
    EXPECT_TRUE(reputes.getIsEnemy(*hostile1, *friendly1));
    EXPECT_FALSE(reputes.getIsEnemy(*friendly1, *neutral));
    EXPECT_FALSE(reputes.getIsEnemy(*neutral, *friendly1));
    EXPECT_TRUE(reputes.getIsNeutral(*friendly1, *neutral));
    EXPECT_TRUE(reputes.getIsNeutral(*neutral, *friendly1));
}
