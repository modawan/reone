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
#include "reone/scene/node/dummy.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/trigger.h"
#include "reone/scene/node/walkmesh.h"
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
        if (!animation) {
            return false;
        }
        auto cut = DialogGUI::decodeCutAnimation(ordinal);
        return gui.enterMixedStunt(gui._participantByTag.at(tag), animation, cut && cut->looping);
    }

    static std::optional<DialogGUI::CutAnimation> decodeCut(int ordinal) {
        return DialogGUI::decodeCutAnimation(ordinal);
    }

    static void setOwner(DialogGUI &gui, std::shared_ptr<Object> owner) {
        gui._owner = std::move(owner);
    }

    static void updateAnimationsForEntry(DialogGUI &gui, const resource::Dialog::EntryReply &entry) {
        gui._currentEntry = &entry;
        gui.updateParticipantAnimations();
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
    // A real SceneGraph keeps every node it hands out alive in its own set, and
    // node trees rely on that: ModelSceneNode::buildNodeTree only keeps raw
    // pointers to the child nodes it asks the graph for. The factories below
    // have to retain them the same way.
    static std::vector<std::shared_ptr<scene::SceneNode>> createdNodes;
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
        ON_CALL(graph, newModel(_, _))
            .WillByDefault(Invoke([&engine](graphics::Model &model, scene::ModelUsage usage) {
                auto node = std::make_shared<scene::ModelSceneNode>(
                    model,
                    usage,
                    graph,
                    engine.services().graphics,
                    engine.services().audio,
                    engine.services().resource);
                createdNodes.push_back(node);
                node->init();
                return node;
            }));
        ON_CALL(graph, newWalkmesh(_))
            .WillByDefault(Invoke([&engine](graphics::Walkmesh &walkmesh) {
                // init() is skipped on purpose: it uploads a debug-render mesh,
                // which needs the render thread. Collision only consults the
                // node's enabled flag and transform.
                auto node = std::make_shared<scene::WalkmeshSceneNode>(
                    walkmesh,
                    graph,
                    engine.services().graphics,
                    engine.services().audio,
                    engine.services().resource);
                createdNodes.push_back(node);
                return node;
            }));
        ON_CALL(graph, newDummy(_))
            .WillByDefault(Invoke([&engine](graphics::ModelNode &modelNode) {
                auto node = std::make_shared<scene::DummySceneNode>(
                    modelNode,
                    graph,
                    engine.services().graphics,
                    engine.services().audio,
                    engine.services().resource);
                createdNodes.push_back(node);
                return node;
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

std::shared_ptr<graphics::Animation> makeAnimation(
    std::string name,
    std::vector<graphics::Animation::Event> events = {}) {
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
        std::move(events));
}

std::shared_ptr<graphics::Model> makeModel(
    std::string name,
    std::vector<std::shared_ptr<graphics::Animation>> animations,
    int classification = graphics::MdlClassification::other) {
    auto root = std::make_shared<graphics::ModelNode>(
        0,
        "root_node",
        glm::vec3(0.0f),
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        true,
        nullptr);
    return std::make_shared<graphics::Model>(
        std::move(name),
        classification,
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

TEST(Creature, should_activate_and_deactivate_lightsabers_in_both_hands_with_combat) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto bodyRoot = std::make_shared<graphics::ModelNode>(
        0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    bodyRoot->addChild(std::make_shared<graphics::ModelNode>(
        1, "rhand", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, bodyRoot.get()));
    bodyRoot->addChild(std::make_shared<graphics::ModelNode>(
        2, "lhand", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, bodyRoot.get()));
    graphics::Model body(
        "body",
        graphics::MdlClassification::character,
        bodyRoot,
        {makeAnimation("pause1", {{0.5f, "draw_weapon"}})},
        "",
        1.0f);

    auto saberAnimations = std::vector<std::shared_ptr<graphics::Animation>> {
        makeAnimation("off"),
        makeAnimation("powerup"),
        makeAnimation("powered"),
        makeAnimation("powerdown")};
    auto rightSaber = makeModel("right_saber", saberAnimations, graphics::MdlClassification::lightsaber);
    auto leftSaber = makeModel("left_saber", saberAnimations, graphics::MdlClassification::lightsaber);

    graphics::GraphicsOptions graphicsOptions;
    scene::SceneGraph graph(
        "test",
        engine.sceneModule().renderPipelineFactory(),
        graphicsOptions,
        engine.services().graphics,
        engine.services().audio,
        engine.services().resource);
    auto bodyNode = graph.newModel(body, scene::ModelUsage::Creature);
    auto rightSaberNode = graph.newModel(*rightSaber, scene::ModelUsage::Equipment);
    auto leftSaberNode = graph.newModel(*leftSaber, scene::ModelUsage::Equipment);
    bodyNode->attach("rhand", *rightSaberNode);
    bodyNode->attach("lhand", *leftSaberNode);
    ASSERT_EQ(bodyNode->getAttachment("rhand"), rightSaberNode.get());
    ASSERT_EQ(bodyNode->getAttachment("lhand"), leftSaberNode.get());

    rightSaberNode->playAnimation("off");
    leftSaberNode->playAnimation("off");
    bodyNode->playAnimation(
        "pause1",
        nullptr,
        scene::AnimationProperties::fromFlags(scene::AnimationFlags::propagate));

    EXPECT_EQ(rightSaberNode->activeAnimationName(), "off");
    EXPECT_EQ(leftSaberNode->activeAnimationName(), "off");

    TestCreature creature(1, "test", game, engine.services());
    creature.setSceneNode(bodyNode);
    bodyNode->setAnimationEventListener(creature);
    bodyNode->update(0.6f);

    EXPECT_EQ(rightSaberNode->activeAnimationName(), "powerup");
    EXPECT_EQ(leftSaberNode->activeAnimationName(), "powerup");

    creature.update(7.9f);
    EXPECT_EQ(rightSaberNode->activeAnimationName(), "powerup");
    EXPECT_EQ(leftSaberNode->activeAnimationName(), "powerup");

    creature.update(0.2f);
    EXPECT_EQ(rightSaberNode->activeAnimationName(), "powerdown");
    EXPECT_EQ(leftSaberNode->activeAnimationName(), "powerdown");

    rightSaberNode->playAnimation("off");
    leftSaberNode->playAnimation("off");
    creature.activateCombat();

    EXPECT_EQ(rightSaberNode->activeAnimationName(), "powerup");
    EXPECT_EQ(leftSaberNode->activeAnimationName(), "powerup");

    creature.deactivateCombat(8.0f);
    creature.update(8.1f);

    EXPECT_EQ(rightSaberNode->activeAnimationName(), "powerdown");
    EXPECT_EQ(leftSaberNode->activeAnimationName(), "powerdown");

    creature.activateCombat();
    creature.deactivateCombat(0.0f);

    EXPECT_FALSE(creature.isInCombat());
    EXPECT_EQ(rightSaberNode->activeAnimationName(), "powerdown");
    EXPECT_EQ(leftSaberNode->activeAnimationName(), "powerdown");
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

// Owns the graphics options the scene graph keeps a reference to.
struct DialogAnimScene {
    graphics::GraphicsOptions graphicsOptions;
    scene::SceneGraph graph;

    explicit DialogAnimScene(TestEngine &engine) :
        graph(
            "test",
            engine.sceneModule().renderPipelineFactory(),
            graphicsOptions,
            engine.services().graphics,
            engine.services().audio,
            engine.services().resource) {
    }

    std::shared_ptr<TestCreature> newCreature(
        Game &game,
        TestEngine &engine,
        uint32_t id,
        std::string tag,
        const std::shared_ptr<graphics::Model> &model) {
        auto creature = std::make_shared<TestCreature>(id, std::move(tag), game, engine.services());
        creature->setSceneNode(graph.newModel(*model, scene::ModelUsage::Creature));
        return creature;
    }
};

std::shared_ptr<scene::ModelSceneNode> modelNodeOf(const std::shared_ptr<Creature> &creature) {
    return std::static_pointer_cast<scene::ModelSceneNode>(creature->sceneNode());
}

std::shared_ptr<TwoDA> makeDialogAnimationsTable() {
    TwoDA::Builder builder;
    builder.columns({"name"});
    for (int i = 0; i < 30; ++i) {
        builder.row({""});
    }
    builder.row({"Talk_Normal"});
    return builder.build();
}

TEST(DialogGUI, should_decode_participant_animation_ordinals_by_cut_band) {
    struct Expectation {
        int ordinal;
        const char *name;
        bool looping;
    };
    const Expectation expectations[] {
        {1000, "cut001", false},
        {1011, "cut012", false},
        {1013, "cut014", false},
        {1199, "cut200", false},
        {1200, "cut001w", false},
        {1201, "cut002w", false},
        {1399, "cut200w", false},
        {1400, "cut001l", true},
        {1409, "cut010l", true},
        {1412, "cut013l", true},
        {1599, "cut200l", true},
        {1600, "cut001wl", true},
        {1601, "cut002wl", true},
        {1799, "cut200wl", true}};

    for (auto &expected : expectations) {
        auto cut = MixedStuntTestAccess::decodeCut(expected.ordinal);
        ASSERT_TRUE(cut.has_value()) << "ordinal " << expected.ordinal;
        EXPECT_EQ(cut->name, expected.name) << "ordinal " << expected.ordinal;
        EXPECT_EQ(cut->looping, expected.looping) << "ordinal " << expected.ordinal;
    }

    // Ordinals outside the known cut bands keep their own meaning and must not
    // be reinterpreted as clip names.
    for (int ordinal : {0, 35, 40, 70, 999, 1800, 2000, 9999, 10000, 10038, 10511}) {
        EXPECT_FALSE(MixedStuntTestAccess::decodeCut(ordinal).has_value()) << "ordinal " << ordinal;
    }
}

TEST(DialogGUI, should_animate_the_real_player_when_an_animated_cut_authors_no_stunt_participants) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    DialogAnimScene scene(engine);

    auto held = makeAnimation("cut010l");
    auto oneShot = makeAnimation("cut012");
    auto superModel = makeModel("supermodel", {held, oneShot});
    auto bodyModel = makeModel("player_body", {makeAnimation("cpause1")});
    bodyModel->setSuperModel(superModel);
    auto player = scene.newCreature(game, engine, 1, "player", bodyModel);
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    auto node = modelNodeOf(player);

    auto dialog = std::make_shared<Dialog>();
    dialog->animatedCutscene = true;
    DialogGUI gui(game, engine.services());
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    ASSERT_EQ(MixedStuntTestAccess::participantCount(gui), 0);

    // A held clip resolves through the supermodel and stays on the creature.
    Dialog::EntryReply inTank;
    inTank.animations.push_back({"player", 1409});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, inTank);

    ASSERT_EQ(node->animationChannels().size(), 1);
    EXPECT_EQ(node->activeAnimationName(), "cut010l");
    EXPECT_EQ(node->animationChannels().front().anim, held.get());
    EXPECT_TRUE(node->animationChannels().front().properties.flags & scene::AnimationFlags::loop);
    node->update(0.6f);
    node->update(0.6f);
    EXPECT_FALSE(node->isAnimationFinished());

    // A one-shot clip from the same band group plays once.
    Dialog::EntryReply draining;
    draining.animations.push_back({"player", 1011});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, draining);

    EXPECT_EQ(node->activeAnimationName(), "cut012");
    EXPECT_EQ(node->animationChannels().front().anim, oneShot.get());
    EXPECT_FALSE(node->animationChannels().front().properties.flags & scene::AnimationFlags::loop);
    node->update(0.6f);
    node->update(0.6f);
    EXPECT_TRUE(node->isAnimationFinished());
}

TEST(DialogGUI, should_animate_an_ordinary_participant_from_a_cut_band_ordinal) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    DialogAnimScene scene(engine);

    auto prone = makeAnimation("cut013l");
    auto rise = makeAnimation("cut014");
    auto ownerModel = makeModel("owner_body", {prone, rise});
    auto owner = scene.newCreature(game, engine, 2, "owner", ownerModel);
    auto node = modelNodeOf(owner);

    auto dialog = std::make_shared<Dialog>();
    dialog->animatedCutscene = false;
    DialogGUI gui(game, engine.services());
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    MixedStuntTestAccess::setOwner(gui, owner);

    Dialog::EntryReply lying;
    lying.animations.push_back({"owner", 1412});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, lying);
    EXPECT_EQ(node->activeAnimationName(), "cut013l");
    EXPECT_EQ(node->animationChannels().front().anim, prone.get());
    EXPECT_TRUE(node->animationChannels().front().properties.flags & scene::AnimationFlags::loop);

    Dialog::EntryReply standing;
    standing.animations.push_back({"owner", 1013});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, standing);
    EXPECT_EQ(node->activeAnimationName(), "cut014");
    EXPECT_EQ(node->animationChannels().front().anim, rise.get());
    EXPECT_FALSE(node->animationChannels().front().properties.flags & scene::AnimationFlags::loop);
}

TEST(DialogGUI, should_hold_an_authored_cut_pose_until_the_dialogue_releases_the_participant) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    DialogAnimScene scene(engine);

    auto idle = makeAnimation("cpause1");
    auto drain = makeAnimation("cut012");
    auto prone = makeAnimation("cut013l");
    auto ownerModel = makeModel("owner_body", {idle, drain, prone}, graphics::MdlClassification::character);
    auto owner = scene.newCreature(game, engine, 7, "owner", ownerModel);
    owner->resumeStateDrivenAnimation();
    auto node = modelNodeOf(owner);
    ASSERT_EQ(node->activeAnimationName(), "cpause1");

    auto dialog = std::make_shared<Dialog>();
    dialog->animatedCutscene = true;
    DialogGUI gui(game, engine.services());
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    MixedStuntTestAccess::setOwner(gui, owner);

    // A one-shot cut clip runs to its end.
    Dialog::EntryReply draining;
    draining.animations.push_back({"owner", 1011});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, draining);
    EXPECT_EQ(node->activeAnimationName(), "cut012");

    node->update(0.6f);
    owner->update(0.0f);
    node->update(0.6f);
    owner->update(0.0f);

    // Having finished, it holds its final frame rather than falling back to the
    // state-driven idle.
    ASSERT_EQ(node->animationChannels().size(), 1);
    EXPECT_TRUE(node->isAnimationFinished());
    EXPECT_EQ(node->activeAnimationName(), "cut012");
    EXPECT_EQ(node->animationChannels().front().anim, drain.get());

    // The authored sequence puts a script-only entry between the two clips. It
    // must not release the held pose.
    Dialog::EntryReply scriptOnly;
    MixedStuntTestAccess::updateAnimationsForEntry(gui, scriptOnly);
    owner->update(0.0f);
    EXPECT_EQ(node->activeAnimationName(), "cut012");
    EXPECT_EQ(node->animationChannels().front().anim, drain.get());

    // The next authored animation replaces it.
    Dialog::EntryReply lying;
    lying.animations.push_back({"owner", 1412});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, lying);
    owner->update(0.0f);
    EXPECT_EQ(node->activeAnimationName(), "cut013l");
    EXPECT_TRUE(node->animationChannels().front().properties.flags & scene::AnimationFlags::loop);

    // A looping clip is not dropped when it passes its end either.
    node->update(0.6f);
    owner->update(0.0f);
    node->update(0.6f);
    owner->update(0.0f);
    EXPECT_FALSE(node->isAnimationFinished());
    EXPECT_EQ(node->activeAnimationName(), "cut013l");

    // Finishing the dialogue hands the creature back to state-driven animation.
    MixedStuntTestAccess::finish(gui);
    EXPECT_EQ(node->activeAnimationName(), "cpause1");
}

TEST(DialogGUI, should_drive_stunt_and_ordinary_participants_from_one_animated_cut_entry) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    DialogAnimScene scene(engine);

    // Both models carry a clip of the same name, so the assertions below
    // distinguish the model each participant was driven from.
    auto stuntClip = makeAnimation("cut001w");
    auto stuntModel = makeModel("principal_stunt", {stuntClip});
    auto principalModel = makeModel("principal_body", {makeAnimation("cut001w")});
    auto principal = scene.newCreature(game, engine, 3, "owner", principalModel);
    auto extraClip = makeAnimation("cut001w");
    auto extraModel = makeModel("extra_body", {extraClip});
    auto extra = scene.newCreature(game, engine, 4, "player", extraModel);
    game.party().addMember(kNpcPlayer, extra);
    game.party().setPlayer(extra);

    EXPECT_CALL(engine.resourceModule().models(), get("principal_stunt"))
        .WillOnce(Return(stuntModel));

    auto dialog = std::make_shared<Dialog>();
    dialog->animatedCutscene = true;
    dialog->stunts.push_back({"owner", "principal_stunt"});
    DialogGUI gui(game, engine.services());
    MixedStuntTestAccess::setOwner(gui, principal);
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    ASSERT_EQ(MixedStuntTestAccess::participantCount(gui), 1);

    Dialog::EntryReply entry;
    entry.animations.push_back({"owner", 1200});
    entry.animations.push_back({"player", 1200});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, entry);

    // The stunt-bound principal is driven from the stunt model.
    auto principalNode = modelNodeOf(principal);
    ASSERT_EQ(principalNode->animationChannels().size(), 1);
    EXPECT_EQ(principalNode->animationChannels().front().anim, stuntClip.get());

    // The ordinary extra is driven from its own model in the same entry.
    auto extraNode = modelNodeOf(extra);
    ASSERT_EQ(extraNode->animationChannels().size(), 1);
    EXPECT_EQ(extraNode->animationChannels().front().anim, extraClip.get());
}

TEST(DialogGUI, should_keep_driving_fully_stunted_animated_cuts_from_the_stunt_model) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    DialogAnimScene scene(engine);

    auto first = makeAnimation("cut001w");
    auto second = makeAnimation("cut002w");
    auto stuntModel = makeModel("player_stunt", {first, second});
    auto bodyModel = makeModel("player_body", {makeAnimation("cut001w"), makeAnimation("cpause1")});
    auto player = scene.newCreature(game, engine, 5, "player", bodyModel);
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    auto node = modelNodeOf(player);

    EXPECT_CALL(engine.resourceModule().models(), get("player_stunt"))
        .WillOnce(Return(stuntModel));

    auto dialog = std::make_shared<Dialog>();
    dialog->animatedCutscene = true;
    dialog->stunts.push_back({"player", "player_stunt"});
    DialogGUI gui(game, engine.services());
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    ASSERT_EQ(MixedStuntTestAccess::participantCount(gui), 1);

    Dialog::EntryReply entry;
    entry.animations.push_back({"player", 1201});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, entry);

    ASSERT_EQ(node->animationChannels().size(), 1);
    EXPECT_EQ(node->activeAnimationName(), "cut002w");
    EXPECT_EQ(node->animationChannels().front().anim, second.get());
    EXPECT_FALSE(node->animationChannels().front().properties.flags & scene::AnimationFlags::loop);
    EXPECT_TRUE(node->animationChannels().front().properties.flags & scene::AnimationFlags::propagate);
}

TEST(DialogGUI, should_not_animate_a_stunt_participant_from_its_own_model) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    DialogAnimScene scene(engine);

    // The stunt model is the authored source, and it lacks the requested clip.
    // The creature's own model carries a clip of that name, which must not be
    // substituted for it.
    auto stuntModel = makeModel("owner_stunt", {makeAnimation("cut001w")});
    auto ownModel = makeModel("owner_body", {makeAnimation("cpause1"), makeAnimation("cut002w")},
                              graphics::MdlClassification::character);
    auto owner = scene.newCreature(game, engine, 8, "owner", ownModel);
    owner->resumeStateDrivenAnimation();
    auto node = modelNodeOf(owner);
    ASSERT_EQ(node->activeAnimationName(), "cpause1");

    EXPECT_CALL(engine.resourceModule().models(), get("owner_stunt"))
        .WillOnce(Return(stuntModel));

    auto dialog = std::make_shared<Dialog>();
    dialog->animatedCutscene = true;
    dialog->stunts.push_back({"owner", "owner_stunt"});
    DialogGUI gui(game, engine.services());
    MixedStuntTestAccess::setOwner(gui, owner);
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    ASSERT_EQ(MixedStuntTestAccess::participantCount(gui), 1);

    Dialog::EntryReply entry;
    entry.animations.push_back({"owner", 1201});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, entry);

    EXPECT_EQ(node->activeAnimationName(), "cpause1");
}

TEST(DialogGUI, should_not_resolve_dialoganimations_ordinals_against_a_stunt_model) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    DialogAnimScene scene(engine);

    // Shape of 650DAN/650kreia: a stunt-bound participant that also receives a
    // dialoganimations ordinal.
    auto stuntClip = makeAnimation("cut001w");
    auto stuntModel = makeModel("owner_stunt", {stuntClip});
    auto talk = makeAnimation("tlknorm");
    auto ownerModel = makeModel("owner_body", {talk, makeAnimation("cpause1")});
    auto owner = scene.newCreature(game, engine, 6, "owner", ownerModel);
    auto node = modelNodeOf(owner);

    EXPECT_CALL(engine.resourceModule().models(), get("owner_stunt"))
        .WillOnce(Return(stuntModel));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("dialoganimations"))
        .WillOnce(Return(makeDialogAnimationsTable()));

    auto dialog = std::make_shared<Dialog>();
    dialog->animatedCutscene = false;
    dialog->stunts.push_back({"owner", "owner_stunt"});
    DialogGUI gui(game, engine.services());
    MixedStuntTestAccess::setOwner(gui, owner);
    MixedStuntTestAccess::loadParticipants(gui, dialog);
    ASSERT_EQ(MixedStuntTestAccess::participantCount(gui), 1);

    Dialog::EntryReply entry;
    entry.animations.push_back({"owner", 10030});
    MixedStuntTestAccess::updateAnimationsForEntry(gui, entry);

    // Resolved through dialoganimations.2da on the creature's own model, and
    // the stunt model was never asked for a semantic animation name.
    ASSERT_EQ(node->animationChannels().size(), 1);
    EXPECT_EQ(node->animationChannels().front().anim, talk.get());
    EXPECT_NE(node->animationChannels().front().anim, stuntClip.get());
    EXPECT_FALSE(MixedStuntTestAccess::isActive(gui, "owner"));
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
    leader->setPosition(glm::vec3(0.5f, -1.0f, 0.0f));
    companion->setPosition(glm::vec3(-0.9f, -1.0f, 0.0f));
    npc->setPosition(glm::vec3(1.9f, -1.0f, 0.0f));
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

namespace {

// Every K1 and K2 door model carries these five animations. The three resting
// poses are zero length in the shipped models; makeAnimation gives each a
// length of one second, which is what lets the transitions be stepped.
struct DoorAnimations {
    bool opening {true};
    bool closing {true};
};

std::shared_ptr<graphics::Model> makeDoorModel(DoorAnimations present) {
    std::vector<std::shared_ptr<graphics::Animation>> animations {
        makeAnimation("closed"),
        makeAnimation("opened1"),
        makeAnimation("opened2")};
    if (present.opening) {
        animations.push_back(makeAnimation("opening1"));
    }
    if (present.closing) {
        animations.push_back(makeAnimation("closing1"));
    }
    return makeModel("testdoor", std::move(animations));
}

// A door with a live model and all three walkmeshes, authored into the given
// resting state.
std::shared_ptr<Door> makeLifecycleDoor(
    Game &game,
    TestEngine &engine,
    int openState,
    DoorAnimations present = DoorAnimations()) {

    testSceneGraph(engine);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("genericdoors"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeGenericDoorsTable()));
    EXPECT_CALL(engine.resourceModule().models(), get("testdoor"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeDoorModel(present)));
    for (auto suffix : {"testdoor0", "testdoor1", "testdoor2"}) {
        EXPECT_CALL(engine.resourceModule().walkmeshes(), get(suffix, ResType::Dwk))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeDoorWalkmesh()));
    }

    auto gff = Gff::Builder()
                   .field(Gff::Field::newByte("GenericType", 1))
                   .field(Gff::Field::newByte("Static", 0))
                   .field(Gff::Field::newByte("OpenState", static_cast<uint8_t>(openState)))
                   .build();
    auto door = game.newDoor();
    door->deserialize(*gff);
    return door;
}

std::string doorPose(const Door &door) {
    auto model = std::static_pointer_cast<scene::ModelSceneNode>(door.sceneNode());
    return model ? model->activeAnimationName() : std::string();
}

// One frame, in the order Game::update runs them: objects first, then the scene
// graph advances model animations. A door therefore notices that a transition
// animation finished on the frame after it actually did.
void stepDoor(Door &door, float dt) {
    door.update(dt);
    auto model = std::static_pointer_cast<scene::ModelSceneNode>(door.sceneNode());
    if (model) {
        model->update(dt);
    }
}

// The doorway is obstructed exactly when the closed walkmesh is live.
bool doorwayBlocks(const Door &door) {
    return door.walkmeshClosed() && door.walkmeshClosed()->isEnabled();
}

} // namespace

// A. A door authored closed keeps loading closed and blocking.
TEST(DoorLifecycle, should_load_an_authored_closed_door_closed_and_blocking) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto door = makeLifecycleDoor(game, engine, /*openState=*/0);

    EXPECT_EQ(DoorState::Closed, door->state());
    EXPECT_FALSE(door->isOpen());
    EXPECT_EQ(DoorTransition::None, door->transition());
    EXPECT_TRUE(doorwayBlocks(*door));
    EXPECT_FALSE(door->walkmeshOpen1()->isEnabled());
    EXPECT_FALSE(door->walkmeshOpen2()->isEnabled());
    EXPECT_EQ("closed", doorPose(*door));
}

// B. A door authored open loads open, non-blocking, and already in its opened
// pose - not sitting shut, and not replaying an opening it finished offscreen.
TEST(DoorLifecycle, should_load_an_authored_open1_door_open_and_passable) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto door = makeLifecycleDoor(game, engine, /*openState=*/1);

    EXPECT_EQ(DoorState::Opened1, door->state());
    EXPECT_TRUE(door->isOpen());
    EXPECT_EQ(DoorTransition::None, door->transition());
    EXPECT_FALSE(doorwayBlocks(*door));
    EXPECT_TRUE(door->walkmeshOpen1()->isEnabled());
    EXPECT_FALSE(door->walkmeshOpen2()->isEnabled());
    EXPECT_EQ("opened1", doorPose(*door));
}

// The second open side is a distinct pose and a distinct walkmesh, so it must
// not be folded into the first. K2 101PER PERDoor105 is authored this way.
TEST(DoorLifecycle, should_load_an_authored_open2_door_on_its_second_side) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto door = makeLifecycleDoor(game, engine, /*openState=*/2);

    EXPECT_EQ(DoorState::Opened2, door->state());
    EXPECT_TRUE(door->isOpen());
    EXPECT_FALSE(doorwayBlocks(*door));
    EXPECT_FALSE(door->walkmeshOpen1()->isEnabled());
    EXPECT_TRUE(door->walkmeshOpen2()->isEnabled());
    EXPECT_EQ("opened2", doorPose(*door));
}

TEST(DoorLifecycle, should_treat_an_unsupported_authored_open_state_as_closed) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto door = makeLifecycleDoor(game, engine, /*openState=*/7);

    EXPECT_EQ(DoorState::Closed, door->state());
    EXPECT_TRUE(doorwayBlocks(*door));
}

// B. A door that is swinging open is still in the doorway. Retail K2 will not
// let the player past one until the opening animation has finished.
TEST(DoorLifecycle, should_keep_blocking_the_doorway_until_the_open_completes) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/0);
    ASSERT_TRUE(doorwayBlocks(*door));

    door->open();

    // The opening has begun. The door is on its way, but it has not arrived,
    // so it is still standing where it was and still obstructs the doorway.
    EXPECT_EQ(DoorState::Closed, door->state());
    EXPECT_FALSE(door->isOpen());
    EXPECT_TRUE(door->isOpening());
    EXPECT_EQ("opening1", doorPose(*door));
    EXPECT_TRUE(doorwayBlocks(*door));
    EXPECT_FALSE(door->walkmeshOpen1()->isEnabled());
    EXPECT_FALSE(door->walkmeshOpen2()->isEnabled());

    // Part way through the opening animation: still blocking. The animation is
    // one second long, so these steps stay well inside it.
    stepDoor(*door, 0.4f);
    EXPECT_TRUE(door->isOpening());
    EXPECT_FALSE(door->isOpen());
    EXPECT_TRUE(doorwayBlocks(*door));

    stepDoor(*door, 0.4f);
    EXPECT_TRUE(door->isOpening());
    EXPECT_TRUE(doorwayBlocks(*door));

    // The step that carries the animation to its end. The door is still
    // blocking on this frame, because it polls before the model advances.
    stepDoor(*door, 0.4f);
    EXPECT_TRUE(doorwayBlocks(*door));

    // Now the door observes the finished animation and the doorway opens up.
    stepDoor(*door, 0.4f);
    EXPECT_FALSE(door->isOpening());
    EXPECT_FALSE(doorwayBlocks(*door));
    EXPECT_TRUE(door->walkmeshOpen1()->isEnabled());
    EXPECT_FALSE(door->walkmeshOpen2()->isEnabled());
    EXPECT_TRUE(door->isOpen());
    EXPECT_EQ(DoorState::Opened1, door->state());
    EXPECT_EQ("opened1", doorPose(*door));
}

// C. The mirror image. A door that is swinging shut has not sealed the doorway
// yet, which is what lets K2 103PER close TO102PER behind a walking player.
TEST(DoorLifecycle, should_not_block_the_doorway_until_the_close_completes) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/1);

    door->close();

    // The close has begun, but somebody in the doorway is not sealed in: the
    // door is still standing open until the animation says otherwise.
    EXPECT_EQ(DoorState::Opened1, door->state());
    EXPECT_TRUE(door->isOpen());
    EXPECT_TRUE(door->isClosing());
    EXPECT_EQ("closing1", doorPose(*door));
    EXPECT_FALSE(doorwayBlocks(*door));
    EXPECT_TRUE(door->walkmeshOpen1()->isEnabled());

    // Part way through the closing animation: still passable.
    stepDoor(*door, 0.4f);
    EXPECT_TRUE(door->isClosing());
    EXPECT_FALSE(doorwayBlocks(*door));

    stepDoor(*door, 0.4f);
    EXPECT_TRUE(door->isClosing());
    EXPECT_FALSE(doorwayBlocks(*door));

    // The step that carries the animation to its end.
    stepDoor(*door, 0.4f);
    EXPECT_FALSE(doorwayBlocks(*door));

    // Now the door observes the finished animation and the doorway goes solid.
    stepDoor(*door, 0.4f);
    EXPECT_FALSE(door->isClosing());
    EXPECT_TRUE(doorwayBlocks(*door));
    EXPECT_FALSE(door->walkmeshOpen1()->isEnabled());
    EXPECT_FALSE(door->isOpen());
    EXPECT_EQ(DoorState::Closed, door->state());
    EXPECT_EQ("closed", doorPose(*door));
}

// D. Closing part way through an opening. The superseded opening must never
// complete afterwards and hand the doorway over.
TEST(DoorLifecycle, should_discard_a_pending_open_when_the_door_closes_again) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/0);

    door->open();
    stepDoor(*door, 0.4f);
    ASSERT_TRUE(door->isOpening());

    door->close();
    EXPECT_FALSE(door->isOpening());
    EXPECT_TRUE(door->isClosing());
    EXPECT_TRUE(doorwayBlocks(*door));

    // Past where the original opening would have completed, and on past where
    // the close completes. The doorway is never handed over at any point.
    for (int i = 0; i < 5; ++i) {
        stepDoor(*door, 0.4f);
        EXPECT_TRUE(doorwayBlocks(*door)) << "step " << i;
        EXPECT_FALSE(door->isOpen()) << "step " << i;
    }

    EXPECT_EQ(DoorTransition::None, door->transition());
    EXPECT_EQ(DoorState::Closed, door->state());
    EXPECT_EQ("closed", doorPose(*door));
}

// E. Reopening part way through a close. The superseded close must never
// complete afterwards and seal the doorway.
TEST(DoorLifecycle, should_discard_a_pending_close_when_the_door_reopens) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/1);

    door->close();
    stepDoor(*door, 0.4f);
    ASSERT_TRUE(door->isClosing());

    door->open();
    EXPECT_FALSE(door->isClosing());
    EXPECT_TRUE(door->isOpening());
    EXPECT_FALSE(doorwayBlocks(*door));
    EXPECT_TRUE(door->isOpen());

    // Past where the original close would have completed, and on past where the
    // reopening completes. The doorway is never sealed at any point.
    for (int i = 0; i < 5; ++i) {
        stepDoor(*door, 0.4f);
        EXPECT_FALSE(doorwayBlocks(*door)) << "step " << i;
        EXPECT_TRUE(door->isOpen()) << "step " << i;
    }

    EXPECT_EQ(DoorTransition::None, door->transition());
    EXPECT_EQ(DoorState::Opened1, door->state());
    EXPECT_TRUE(door->walkmeshOpen1()->isEnabled());
    EXPECT_EQ("opened1", doorPose(*door));
}

// F. A door with nothing to animate has to arrive anyway, in both directions.
TEST(DoorLifecycle, should_open_immediately_when_there_is_no_opening_animation) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/0, DoorAnimations {false, true});
    ASSERT_TRUE(doorwayBlocks(*door));

    door->open();

    EXPECT_FALSE(door->isOpening());
    EXPECT_FALSE(doorwayBlocks(*door));
    EXPECT_TRUE(door->isOpen());
    EXPECT_EQ(DoorState::Opened1, door->state());
    EXPECT_EQ("opened1", doorPose(*door));
}

TEST(DoorLifecycle, should_block_immediately_when_there_is_no_closing_animation) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/1, DoorAnimations {true, false});
    ASSERT_FALSE(doorwayBlocks(*door));

    door->close();

    EXPECT_FALSE(door->isClosing());
    EXPECT_TRUE(doorwayBlocks(*door));
    EXPECT_FALSE(door->isOpen());
    EXPECT_EQ("closed", doorPose(*door));
}

// G. Repeated commands are stable, and in particular never invert collision for
// a frame on their way to the state the door is already in.
TEST(DoorLifecycle, should_keep_a_closed_door_blocking_when_closed_again) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/1);
    door->close();
    stepDoor(*door, 1.5f);
    stepDoor(*door, 0.1f);
    ASSERT_TRUE(doorwayBlocks(*door));

    door->close();
    EXPECT_TRUE(doorwayBlocks(*door));
    stepDoor(*door, 0.1f);

    EXPECT_TRUE(doorwayBlocks(*door));
    EXPECT_FALSE(door->isOpen());
    EXPECT_EQ(DoorTransition::None, door->transition());
    EXPECT_EQ(DoorState::Closed, door->state());
    EXPECT_EQ("closed", doorPose(*door));
}

TEST(DoorLifecycle, should_keep_an_open_door_passable_when_opened_again) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/1);
    ASSERT_FALSE(doorwayBlocks(*door));

    door->open();
    EXPECT_FALSE(doorwayBlocks(*door));
    stepDoor(*door, 0.1f);

    EXPECT_FALSE(doorwayBlocks(*door));
    EXPECT_TRUE(door->isOpen());
    EXPECT_EQ(DoorTransition::None, door->transition());
    EXPECT_EQ(DoorState::Opened1, door->state());
    EXPECT_TRUE(door->walkmeshOpen1()->isEnabled());
    EXPECT_EQ("opened1", doorPose(*door));
}

// Opening an already opening door must not rewind the leaf, which would push
// the moment the doorway frees up further and further out under a click spam.
TEST(DoorLifecycle, should_not_restart_an_opening_that_is_already_running) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/0);

    door->open();
    stepDoor(*door, 0.4f);
    door->open();
    ASSERT_TRUE(door->isOpening());

    // Three more steps carry the original animation past its one second and let
    // the door observe it. A restart would need a fourth.
    stepDoor(*door, 0.4f);
    stepDoor(*door, 0.4f);
    stepDoor(*door, 0.4f);

    EXPECT_FALSE(door->isOpening());
    EXPECT_TRUE(door->isOpen());
    EXPECT_FALSE(doorwayBlocks(*door));
}

// A door that is opening is on its way out of reach, so it must not be offered
// for interaction again while it swings.
TEST(DoorLifecycle, should_not_offer_an_opening_door_for_interaction) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto door = makeLifecycleDoor(game, engine, /*openState=*/0);
    ASSERT_TRUE(door->isSelectable());

    door->open();
    EXPECT_FALSE(door->isSelectable());

    stepDoor(*door, 0.4f);
    EXPECT_FALSE(door->isSelectable());

    stepDoor(*door, 0.4f);
    stepDoor(*door, 0.4f);
    stepDoor(*door, 0.4f);
    ASSERT_TRUE(door->isOpen());
    EXPECT_FALSE(door->isSelectable());
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

TEST(AreaReputationSearch, treats_the_creature_being_searched_around_as_the_source) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    testSceneGraph(engine);
    auto area = game.newArea();
    auto searching = game.newCreature();
    searching->setFaction(Faction::Hostile1);
    auto candidate = game.newCreature();
    candidate->setFaction(Faction::Friendly1);
    area->add(candidate);
    auto &reputes = static_cast<MockReputes &>(engine.services().game.reputes);

    // The search is centred on `searching`, so a candidate's hostility is
    // judged from that creature's point of view, not the other way round.
    EXPECT_CALL(reputes, getIsEnemy(Ref(*searching), Ref(*candidate)))
        .WillOnce(Return(true));

    Area::SearchCriteriaList criterias {
        {CreatureType::Reputation, static_cast<int>(ReputationType::Enemy)}};

    EXPECT_EQ(candidate, area->getNearestCreature(searching, criterias));
}
