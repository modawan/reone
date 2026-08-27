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
#include "reone/game/action/movetopoint.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/unlockobject.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/gui/areatransition.h"
#include "reone/game/gui/actionbar.h"
#include "reone/game/gui/chargen/iconselection.h"
#include "reone/game/gui/ingame.h"
#include "reone/game/gui/loadscreen.h"
#include "reone/game/gui/mainmenu.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/gui/dialog.h"
#include "reone/game/gui/hud.h"
#include "reone/game/gui/statussummary.h"
#include "reone/game/object/area.h"
#include "reone/game/object/camera/animated.h"
#include "reone/game/object/camera/dialog.h"
#include "reone/game/object/camera/firstperson.h"
#include "reone/game/object/camera/static.h"
#include "reone/game/object/camera/thirdperson.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/player.h"
#include "reone/game/reputes.h"
#include "reone/game/room.h"
#include "reone/game/script/routines.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/camera/perspective.h"
#include "reone/graphics/font.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/walkmesh.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/control/label.h"
#include "reone/gui/gui.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/dummy.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/trigger.h"
#include "reone/scene/node/walkmesh.h"
#include "reone/system/exception/validation.h"
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

    void captureDescription(
        StatusSummaryCategory category,
        std::shared_ptr<gui::Label> description) {
        auto &row = _rows[static_cast<size_t>(category)];
        row.description = std::move(description);
        row.descriptionExtent = row.description->extent();
        row.authoredText = row.description->text().text;
    }

    std::string formatCapturedDescription(
        StatusSummaryCategory category,
        const StatusSummaryEntry &entry) const {
        return descriptionText(
            category,
            entry,
            _rows[static_cast<size_t>(category)].authoredText);
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

class TestScaledControl : public gui::Control {
public:
    TestScaledControl(gui::IGUI &gui, TestEngine &engine) :
        Control(
            gui,
            gui::ControlType::Panel,
            engine.services().scene.graphs,
            engine.services().graphics,
            engine.services().resource) {
    }

    void setAuthoredGeometry(Extent extent, int borderDimension, int hilightDimension) {
        _authoredExtent = extent;
        _extent = extent;
        _authoredBorderDimension = borderDimension;
        _authoredHilightDimension = hilightDimension;
        _border = std::make_shared<Border>();
        _border->dimension = borderDimension;
        _hilight = std::make_shared<Border>();
        _hilight->dimension = hilightDimension;
    }
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

    static AnimationType dialogAnimationType(DialogGUI &gui, int ordinal) {
        return gui.getDialogAnimationType(ordinal);
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

namespace reone::game {

class ActionBarTestAccess {
public:
    static void addAttackAction(ActionBar &bar, const std::shared_ptr<gui::Button> &button) {
        ActionBar::Slot slot;
        slot.slot.actions.emplace_back(ActionType::AttackObject);
        slot.button = button;
        bar._slots.push_back(std::move(slot));
    }
};

class HUDTestAccess {
public:
    static void setHealthControl(HUD &hud,
                                 std::shared_ptr<gui::IGUI> gui,
                                 std::shared_ptr<gui::Label> control) {
        hud._gui = std::move(gui);
        hud._controls.LBL_BACK1 = std::move(control);
    }

    static void setCapturePresentation(HUD &hud, bool presentation, bool combat, bool transition) {
        hud._capturePresentation = presentation;
        hud._captureCombatPresentation = combat;
        hud._captureTransitionPresentation = transition;
    }

    static std::tuple<bool, bool, bool> capturePresentation(const HUD &hud) {
        return {hud._capturePresentation, hud._captureCombatPresentation, hud._captureTransitionPresentation};
    }

};

} // namespace reone::game

std::pair<std::string, std::string> reone::game::TestGameModule::scheduledTransition(const Game &game) {
    return {game._nextModule, game._nextEntry};
}

void reone::game::TestGameModule::setActiveModuleArea(Game &game, std::shared_ptr<Area> area) {
    game._module = game.newModule();
    game._module->_area = std::move(area);
}

std::pair<glm::vec3, float> reone::game::TestGameModule::resolveModuleEntry(
    Module &module,
    std::string entry,
    const glm::vec3 &defaultPosition,
    float defaultFacing) {

    module._info.entryPosition = defaultPosition;
    module._info.entryFacing = defaultFacing;
    glm::vec3 position;
    float facing = 0.0f;
    module.getEntryPoint(entry, position, facing);
    return {position, facing};
}

void reone::game::TestGameModule::deserializeInventory(Game &game, Gff &gff) {
    game.deserializeInventory(gff);
}

namespace {

class TestAreaTransition : public AreaTransition {
public:
    using AreaTransition::AreaTransition;
    using AreaTransition::preload;
};

class TestInGameMenu : public InGameMenu {
public:
    using InGameMenu::InGameMenu;
    using InGameMenu::preload;
};

class TestLoadingScreen : public LoadingScreen {
public:
    using LoadingScreen::LoadingScreen;
    using LoadingScreen::preload;
};

class TestMainMenu : public MainMenu {
public:
    using MainMenu::MainMenu;
    using MainMenu::preload;
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

    void setHitPointsForTest(int hitPoints) {
        _hitPoints = hitPoints;
        _maxHitPoints = hitPoints;
        _currentHitPoints = hitPoints;
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

std::shared_ptr<TwoDA> makeLightsaberBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= 12; ++i) {
        if (i == 1) {
            builder.row({"1.5", "2", "2", "2", "4", "48", "w_stunbaton", "1", "1", "1", "-1", ""});
        } else if (i == 3) {
            builder.row({"1.5", "2", "2", "2", "6", "48", "w_vbroswrd", "1", "1", "2", "-1", ""});
        } else if (i == 8) {
            builder.row({"1.5", "2", "2", "2", "8", "48", "w_lghtsbr", "1", "1", "2", "-1", ""});
        } else if (i == 12) {
            builder.row({"23", "2", "2", "2", "6", "48", "w_blstrpstl", "1", "4", "4", "-1", ""});
        } else {
            builder.row({"", "", "", "", "", "", "", "", "", "", "", ""});
        }
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> makeAppearanceTable() {
    TwoDA::Builder builder;
    builder.columns({"modeltype", "walkdist", "rundist", "footsteptype", "envmap", "race", "racetex"});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    // Row 3 is a body model rather than a creature model, so tests can cover
    // the humanoid side of the animation naming split.
    builder.row({"B", "1", "1", "-1", "", "", ""});
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

// Obstruction reported by the shared testWalk stub. Null by default, so walking
// is unobstructed unless a test opts in through ScopedWalkObstruction.
scene::IUser *&walkObstruction() {
    static scene::IUser *obstruction = nullptr;
    return obstruction;
}

class ScopedWalkObstruction {
public:
    explicit ScopedWalkObstruction(scene::IUser &obstruction) {
        walkObstruction() = &obstruction;
    }

    ~ScopedWalkObstruction() {
        walkObstruction() = nullptr;
    }
};

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
        ON_CALL(graph, newCamera())
            .WillByDefault(Invoke([&engine]() {
                auto node = std::make_shared<scene::CameraSceneNode>(
                    graph,
                    engine.services().graphics,
                    engine.services().audio,
                    engine.services().resource);
                createdNodes.push_back(node);
                return node;
            }));
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
            .WillByDefault(Invoke([](const glm::vec3 &origin,
                                     const glm::vec3 &dest,
                                     const scene::IUser *excludeUser,
                                     scene::Collision &collision) {
                auto *obstruction = walkObstruction();
                if (!obstruction || obstruction == excludeUser) {
                    return false;
                }
                collision.user = obstruction;
                collision.intersection = dest;
                collision.normal = glm::vec3(0.0f, -1.0f, 0.0f);
                collision.material = 0;
                return true;
            }));
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
    walkmesh->vertices = {
        glm::vec3(-2.0f, -0.5f, 0.0f),
        glm::vec3(2.0f, -0.5f, 3.0f),
        glm::vec3(2.0f, 0.5f, 0.0f)};
    walkmesh->normals = {
        glm::vec3(0.0f, 0.0f, 1.0f),
    };
    walkmesh->faces = {{0, 1, 2}};
    walkmesh->materials = {0};
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

std::shared_ptr<Waypoint> addEntryWaypoint(
    Game &game,
    const std::shared_ptr<Area> &area,
    std::string tag,
    const glm::vec3 &position,
    float facing = 0.0f) {

    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", std::move(tag)))
                   .field(Gff::Field::newFloat("XPosition", position.x))
                   .field(Gff::Field::newFloat("YPosition", position.y))
                   .field(Gff::Field::newFloat("ZPosition", position.z))
                   .field(Gff::Field::newFloat("XOrientation", -glm::sin(facing)))
                   .field(Gff::Field::newFloat("YOrientation", glm::cos(facing)))
                   .build();
    auto waypoint = game.newWaypoint();
    waypoint->deserialize(*gff);
    area->add(waypoint);
    return waypoint;
}

// Door without linked-module metadata, so it blocks and opens without also
// generating a transition trigger.
std::shared_ptr<Door> makePlainDoor(
    Game &game,
    TestEngine &engine,
    bool locked = false,
    std::string onOpen = "",
    glm::vec3 position = glm::vec3(0.0f)) {

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
    EXPECT_CALL(engine.resourceModule().walkmeshes(), get("testdoor1", ResType::Dwk))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeDoorWalkmesh()));

    auto gff = Gff::Builder()
                   .field(Gff::Field::newByte("GenericType", 1))
                   .field(Gff::Field::newByte("Static", 0))
                   .field(Gff::Field::newByte("Locked", locked ? 1 : 0))
                   .field(Gff::Field::newShort("HP", 20))
                   .field(Gff::Field::newResRef("OnOpen", std::move(onOpen)))
                   .field(Gff::Field::newFloat("X", position.x))
                   .field(Gff::Field::newFloat("Y", position.y))
                   .field(Gff::Field::newFloat("Z", position.z))
                   .build();
    auto door = game.newDoor();
    door->deserialize(*gff);
    return door;
}

std::shared_ptr<Creature> makeMovingCreature(
    Game &game,
    TestEngine &engine,
    std::string onBlocked = "") {

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
                   .field(Gff::Field::newResRef("ScriptOnBlocked", std::move(onBlocked)))
                   .build();
    auto creature = game.newCreature();
    creature->deserialize(*gff);
    return creature;
}

// Navigation-driven step towards dest. Goes through Creature::advanceOnPath, so
// it exercises the same path AI, scripts and actions take, unlike direct player
// locomotion which calls Area::moveCreature.
void navigationStep(Creature &creature, const glm::vec3 &dest, float dt = 1.0f) {
    glm::vec3 dir = glm::normalize(dest - creature.position());
    creature.advanceOnPath(dest, dir, /*run=*/false, /*distance=*/0.1f, dt);
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

TEST(Creature, restores_the_creation_script_flag_only_from_a_saved_record) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits),
                getTextureByAppearance(_))
        .Times(AnyNumber());

    auto record = [](std::optional<uint8_t> creationScriptFired) {
        Gff::Builder builder;
        builder.field(Gff::Field::newDword("Appearance_Type", 0))
            .field(Gff::Field::newWord("SoundSetFile", 0xffff))
            .field(Gff::Field::newByte("BodyBag", 0xff))
            .field(Gff::Field::newByte("PerceptionRange", 0xff));
        if (creationScriptFired) {
            builder.field(Gff::Field::newByte(
                "CreatnScrptFird", *creationScriptFired));
        }
        return builder.build();
    };

    // A blueprint says nothing about creation, so the creature still owes its
    // OnSpawn. A save record is authoritative either way.
    auto blueprint = game.newCreature();
    blueprint->deserialize(*record(std::nullopt));
    EXPECT_FALSE(blueprint->spawnScriptFired());

    auto restoredSpawned = game.newCreature();
    restoredSpawned->deserialize(*record(1));
    EXPECT_TRUE(restoredSpawned->spawnScriptFired());

    auto restoredUnspawned = game.newCreature();
    restoredUnspawned->deserialize(*record(0));
    EXPECT_FALSE(restoredUnspawned->spawnScriptFired());
}

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

TEST(EquipmentStack, reequips_a_restored_offhand_lightsaber_after_inventory_merge) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeLightsaberBaseItemsTable()));

    auto actor = game.newCreature();
    auto mainHand = makeItem(game, "g_w_lghtsbr01", 8, 1);
    auto offHand = makeItem(game, "g_w_lghtsbr01", 8, 1);
    auto inventorySaber = makeItem(game, "g_w_lghtsbr01", 8, 1);
    ASSERT_TRUE(mainHand->blueprintResRef().empty());
    ASSERT_TRUE(offHand->blueprintResRef().empty());
    ASSERT_TRUE(inventorySaber->blueprintResRef().empty());

    ASSERT_TRUE(actor->equip(InventorySlots::rightWeapon, mainHand));
    ASSERT_TRUE(actor->equip(InventorySlots::leftWeapon, offHand));
    actor->addItem(inventorySaber);

    actor->unequip(offHand);
    actor->addItem(offHand);

    ASSERT_EQ(1u, actor->items().size());
    ASSERT_EQ(inventorySaber, actor->items().front());
    ASSERT_EQ(2, inventorySaber->stackSize());
    ASSERT_EQ(actor->id(), inventorySaber->owner());
    ASSERT_EQ(mainHand, actor->getEquippedItem(InventorySlots::rightWeapon));
    ASSERT_FALSE(actor->getEquippedItem(InventorySlots::leftWeapon));

    auto decision = evaluateEquipmentCandidate(
        *actor, InventorySlots::leftWeapon, inventorySaber.get());
    ASSERT_TRUE(decision.valid);
    ASSERT_EQ(EquipmentCandidateAction::Equip, decision.action);

    auto candidate = takeEquipmentCandidate(game, *actor, inventorySaber);
    ASSERT_TRUE(candidate);
    EXPECT_NE(inventorySaber, candidate);
    EXPECT_EQ(1, inventorySaber->stackSize());
    EXPECT_EQ(8, candidate->baseItemType());
    EXPECT_EQ(1, candidate->stackSize());

    ASSERT_TRUE(actor->equip(InventorySlots::leftWeapon, candidate));
    EXPECT_EQ(mainHand, actor->getEquippedItem(InventorySlots::rightWeapon));
    EXPECT_EQ(candidate, actor->getEquippedItem(InventorySlots::leftWeapon));
    EXPECT_EQ(actor->id(), candidate->owner());
    EXPECT_EQ(1u, actor->items().size());
    EXPECT_EQ(1, actor->items().front()->stackSize());
}

TEST(EquipmentCompatibility, accepts_lightsaber_and_vibroblade_in_either_hand_order) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeLightsaberBaseItemsTable()));

    auto saberMainActor = game.newCreature();
    auto mainSaber = makeItem(game, "g_w_lghtsbr01", 8, 1);
    auto offVibroblade = makeItem(game, "g_w_vbroswrd01", 3, 1);
    ASSERT_TRUE(saberMainActor->equip(InventorySlots::rightWeapon, mainSaber));
    auto vibrobladeDecision = evaluateEquipmentCandidate(
        *saberMainActor, InventorySlots::leftWeapon, offVibroblade.get());
    ASSERT_TRUE(vibrobladeDecision.valid);
    ASSERT_EQ(EquipmentCandidateAction::Equip, vibrobladeDecision.action);
    ASSERT_TRUE(saberMainActor->equip(InventorySlots::leftWeapon, offVibroblade));

    auto vibrobladeMainActor = game.newCreature();
    auto mainVibroblade = makeItem(game, "g_w_vbroswrd01", 3, 1);
    auto offSaber = makeItem(game, "g_w_lghtsbr01", 8, 1);
    ASSERT_TRUE(vibrobladeMainActor->equip(InventorySlots::rightWeapon, mainVibroblade));
    auto saberDecision = evaluateEquipmentCandidate(
        *vibrobladeMainActor, InventorySlots::leftWeapon, offSaber.get());
    ASSERT_TRUE(saberDecision.valid);
    ASSERT_EQ(EquipmentCandidateAction::Equip, saberDecision.action);
    ASSERT_TRUE(vibrobladeMainActor->equip(InventorySlots::leftWeapon, offSaber));
}

TEST(EquipmentCompatibility, rejects_retail_incompatible_melee_and_ranged_weapons) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeLightsaberBaseItemsTable()));

    auto saberMainActor = game.newCreature();
    auto mainSaber = makeItem(game, "g_w_lghtsbr01", 8, 1);
    auto offBlaster = makeItem(game, "g_w_blstrpstl001", 12, 1);
    ASSERT_TRUE(saberMainActor->equip(InventorySlots::rightWeapon, mainSaber));
    auto blasterDecision = evaluateEquipmentCandidate(
        *saberMainActor, InventorySlots::leftWeapon, offBlaster.get());
    ASSERT_FALSE(blasterDecision.valid);
    ASSERT_EQ(EquipmentCandidateAction::Reject, blasterDecision.action);
    ASSERT_EQ(EquipmentCandidateReason::IncompatibleWithMainHand, blasterDecision.reason);

    auto blasterMainActor = game.newCreature();
    auto mainBlaster = makeItem(game, "g_w_blstrpstl001", 12, 1);
    auto offSaber = makeItem(game, "g_w_lghtsbr01", 8, 1);
    ASSERT_TRUE(blasterMainActor->equip(InventorySlots::rightWeapon, mainBlaster));
    auto saberDecision = evaluateEquipmentCandidate(
        *blasterMainActor, InventorySlots::leftWeapon, offSaber.get());
    ASSERT_FALSE(saberDecision.valid);
    ASSERT_EQ(EquipmentCandidateAction::Reject, saberDecision.action);
    ASSERT_EQ(EquipmentCandidateReason::IncompatibleWithMainHand, saberDecision.reason);
}

TEST(EquipmentStack, equips_a_restored_stun_baton_stack_on_an_empty_party_member) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeLightsaberBaseItemsTable()));

    auto sharedInventory = game.newCreature();
    auto bastila = game.newCreature();
    auto batonStack = makeItem(game, "ptar_shockstick", 1, 3);
    ASSERT_TRUE(batonStack->blueprintResRef().empty());
    sharedInventory->addItem(batonStack);

    auto decision = evaluateEquipmentCandidate(
        *bastila, InventorySlots::rightWeapon, batonStack.get());
    ASSERT_TRUE(decision.valid);
    ASSERT_EQ(EquipmentCandidateAction::Equip, decision.action);
    ASSERT_FALSE(bastila->getEquippedItem(InventorySlots::leftWeapon));

    auto candidate = takeEquipmentCandidate(game, *sharedInventory, batonStack);
    ASSERT_TRUE(candidate);
    EXPECT_EQ(1, candidate->baseItemType());
    EXPECT_EQ(2, batonStack->stackSize());
    ASSERT_TRUE(bastila->equip(InventorySlots::rightWeapon, candidate));
    EXPECT_EQ(candidate, bastila->getEquippedItem(InventorySlots::rightWeapon));
    EXPECT_FALSE(bastila->getEquippedItem(InventorySlots::leftWeapon));
    EXPECT_EQ(bastila->id(), candidate->owner());
    EXPECT_EQ(sharedInventory->id(), batonStack->owner());
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

TEST(Creature, should_publish_external_animation_before_pending_state_refresh) {
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
    auto cut = makeAnimation("cut003w");
    graphics::Model model(
        "creature", 0, modelRoot,
        std::vector<std::shared_ptr<graphics::Animation>> {idle}, "", 1.0f);
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

    // New/reconstructed models still owe their ordinary state refresh here.
    // The real Game 5 path assigns the external stunt clip before that refresh
    // receives a creature update on current upstream.
    scene::AnimationProperties properties;
    properties.flags = scene::AnimationFlags::propagate;
    properties.scale = 1.0f;
    ASSERT_TRUE(creature.playExternalAnimation(cut, properties));

    creature.update(0.0f);
    modelNode->update(0.25f);

    ASSERT_EQ(modelNode->animationChannels().size(), 1);
    EXPECT_EQ(modelNode->activeAnimationName(), "cut003w");
    EXPECT_FLOAT_EQ(modelNode->animationChannels().front().time, 0.25f);
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
    for (int i = 0; i < 227; ++i) {
        std::string name;
        switch (i) {
        case 30:
            name = "Talk_Normal";
            break;
        case 35:
            name = "Bow";
            break;
        case 40:
            name = "Talk_Forceful";
            break;
        case 44:
            name = "Victory";
            break;
        case 70:
            name = "Inject";
            break;
        }
        builder.row({name});
    }
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

TEST(DialogGUI, should_decode_direct_k1_dialog_animation_rows) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    DialogGUI gui(game, engine.services());
    auto animations = makeDialogAnimationsTable();

    EXPECT_CALL(engine.resourceModule().twoDas(), get("dialoganimations"))
        .Times(4)
        .WillRepeatedly(Return(animations));

    EXPECT_EQ(AnimationType::FireForgetBow, MixedStuntTestAccess::dialogAnimationType(gui, 35));
    EXPECT_EQ(AnimationType::LoopingTalkForceful, MixedStuntTestAccess::dialogAnimationType(gui, 40));
    EXPECT_EQ(AnimationType::FireForgetVictory1, MixedStuntTestAccess::dialogAnimationType(gui, 44));
    EXPECT_EQ(AnimationType::FireForgetInject, MixedStuntTestAccess::dialogAnimationType(gui, 70));
}

TEST(DialogGUI, should_preserve_offset_dialog_animation_rows_and_k2_rejection) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game k1Game(GameID::KotOR, "", engine.options(), engine.services(), console);
    Game k2Game(GameID::TSL, "", engine.options(), engine.services(), console);
    DialogGUI k1Gui(k1Game, engine.services());
    DialogGUI k2Gui(k2Game, engine.services());
    auto animations = makeDialogAnimationsTable();

    EXPECT_CALL(engine.resourceModule().twoDas(), get("dialoganimations"))
        .Times(2)
        .WillRepeatedly(Return(animations));

    EXPECT_EQ(AnimationType::FireForgetBow, MixedStuntTestAccess::dialogAnimationType(k1Gui, 10035));
    EXPECT_EQ(AnimationType::FireForgetBow, MixedStuntTestAccess::dialogAnimationType(k2Gui, 10035));
    EXPECT_EQ(AnimationType::Invalid, MixedStuntTestAccess::dialogAnimationType(k2Gui, 35));
}

TEST(DialogGUI, should_reject_invalid_and_unresolved_k1_dialog_animation_rows) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    DialogGUI gui(game, engine.services());
    auto animations = makeDialogAnimationsTable();

    EXPECT_CALL(engine.resourceModule().twoDas(), get("dialoganimations"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(animations));

    EXPECT_EQ(AnimationType::Invalid, MixedStuntTestAccess::dialogAnimationType(gui, 0));
    EXPECT_EQ(AnimationType::Invalid, MixedStuntTestAccess::dialogAnimationType(gui, -1));
    EXPECT_EQ(AnimationType::Invalid, MixedStuntTestAccess::dialogAnimationType(gui, 227));
    EXPECT_EQ(AnimationType::Invalid, MixedStuntTestAccess::dialogAnimationType(gui, 10227));
    EXPECT_EQ(AnimationType::Invalid, MixedStuntTestAccess::dialogAnimationType(gui, 2000));
}

TEST(DialogGUI, should_preserve_cut_band_precedence_for_k1_direct_rows) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    DialogGUI gui(game, engine.services());
    Dialog::EntryReply entry;
    entry.animations.push_back({"owner", 1000});

    EXPECT_CALL(engine.resourceModule().twoDas(), get("dialoganimations")).Times(0);

    MixedStuntTestAccess::updateAnimationsForEntry(gui, entry);
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

    // The game-GUI base applies the scaled-mode default first; the
    // transition presentation then overrides it with its own anchoring.
    EXPECT_CALL(gui, setScaling(gui::GUI::ScalingMode::Scaled));
    EXPECT_CALL(gui, setResolution(640, 480));
    EXPECT_CALL(gui, setScaling(gui::GUI::ScalingMode::ScaledTopCenter));

    presentation.preload(gui);
}

TEST(TransitionPresentationLayout, should_uniformly_scale_and_top_anchor_the_authored_canvas) {
    TestEngine &engine = testEngine();
    graphics::GraphicsOptions options;
    options.width = 3440;
    options.height = 1440;
    gui::GUI guiInstance(options, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    guiInstance.setResolution(640, 480);
    guiInstance.setScaling(gui::GUI::ScalingMode::ScaledTopCenter);

    auto rootExtent = Gff::Builder()
                          .field(Gff::Field::newInt("LEFT", 0))
                          .field(Gff::Field::newInt("TOP", 0))
                          .field(Gff::Field::newInt("WIDTH", 640))
                          .field(Gff::Field::newInt("HEIGHT", 480))
                          .build();
    auto root = Gff::Builder()
                    .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(gui::ControlType::Panel)))
                    .field(Gff::Field::newCExoString("TAG", "ROOT"))
                    .field(Gff::Field::newStruct("EXTENT", std::move(rootExtent)))
                    .field(Gff::Field::newList("CONTROLS", {}))
                    .build();

    guiInstance.load(*root);

    EXPECT_FLOAT_EQ(guiInstance.scale(), 3.0f);
    EXPECT_EQ(guiInstance.rootOffset(), glm::ivec2(760, 0));
    EXPECT_EQ(guiInstance.rootControl().extent().width, 1920);
    EXPECT_EQ(guiInstance.rootControl().extent().height, 1440);
}

// Every game GUI must receive the scaling default from the base preload. The
// in-game menu forgot to chain it, which left its top navigation icon strip
// unscaled and floating over the correctly scaled subscreens - the regression
// these lock out. They assert the chaining, not which mode the default is.

TEST(GameGUIScalingDefault, should_apply_default_scaling_to_in_game_menu) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    TestInGameMenu menu(game, engine.services());
    NiceMock<gui::MockGUI> gui;

    EXPECT_CALL(gui, setScaling(gui::GUI::ScalingMode::Scaled));

    menu.preload(gui);
}

TEST(GameGUIScalingDefault, should_apply_default_scaling_to_loading_screen) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    TestLoadingScreen screen(game, engine.services());
    NiceMock<gui::MockGUI> gui;

    EXPECT_CALL(gui, setScaling(gui::GUI::ScalingMode::Scaled));

    screen.preload(gui);
}

TEST(GameGUIScalingDefault, should_apply_default_scaling_to_main_menu) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    TestMainMenu menu(game, engine.services());
    NiceMock<gui::MockGUI> gui;

    EXPECT_CALL(gui, setScaling(gui::GUI::ScalingMode::Scaled));
    EXPECT_CALL(gui, setResolution(800, 600));

    menu.preload(gui);
}

TEST(GUIControlLayout, should_scale_text_uniformly_when_geometry_uses_different_axis_factors) {
    TestEngine &engine = testEngine();
    graphics::GraphicsOptions options;
    options.width = 1920;
    options.height = 1080;
    options.guiTextScale = 1.25f;
    gui::GUI guiInstance(options, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    guiInstance.setResolution(640, 480);
    TestScaledControl control(guiInstance, engine);
    control.setAuthoredGeometry({20, 30, 200, 80}, 8, 8);

    control.stretch(3.0f, 2.25f);

    EXPECT_EQ(control.extent().left, 60);
    EXPECT_EQ(control.extent().top, 67);
    EXPECT_EQ(control.extent().width, 600);
    EXPECT_EQ(control.extent().height, 180);
    EXPECT_FLOAT_EQ(control.scale(), 2.8125f);
}

TEST(GalleryHUDPresentation, should_clear_capture_flags_without_a_loaded_gui) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    HUD hud(game, engine.services());
    HUDTestAccess::setCapturePresentation(hud, true, true, true);

    hud.clearCapturePresentation();

    EXPECT_EQ(HUDTestAccess::capturePresentation(hud), std::make_tuple(false, false, false));
}

TEST(GUIInputCoordinates, should_hit_screen_positioned_controls_without_authored_root_offset) {
    TestEngine &engine = testEngine();
    graphics::GraphicsOptions options;
    options.width = 1920;
    options.height = 1080;
    gui::GUI guiInstance(options, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    guiInstance.setResolution(640, 480);
    guiInstance.setScaling(gui::GUI::ScalingMode::PositionRelativeToCenter);

    auto rootExtent = Gff::Builder()
                          .field(Gff::Field::newInt("LEFT", -320))
                          .field(Gff::Field::newInt("TOP", -240))
                          .field(Gff::Field::newInt("WIDTH", 640))
                          .field(Gff::Field::newInt("HEIGHT", 480))
                          .build();
    auto root = Gff::Builder()
                    .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(gui::ControlType::Panel)))
                    .field(Gff::Field::newCExoString("TAG", "ROOT"))
                    .field(Gff::Field::newStruct("EXTENT", std::move(rootExtent)))
                    .field(Gff::Field::newList("CONTROLS", {}))
                    .build();
    guiInstance.load(*root);

    bool clicked = false;
    auto button = std::make_shared<gui::Button>(
        guiInstance,
        engine.services().scene.graphs,
        engine.services().graphics,
        engine.services().resource);
    button->setTag("REPLY");
    button->setExtent({400, 800, 400, 100});
    button->setOnClick([&clicked]() { clicked = true; });
    guiInstance.addControlToFront(button, gui::IGUI::ControlCoordinates::Screen);

    guiInstance.handle(input::Event::newMouseMotion({500, 850, 0.0f, 0.0f}));
    EXPECT_TRUE(button->isSelected());
    guiInstance.handle(input::Event::newMouseButtonDown(
        {input::MouseButton::Left, true, 1, 500, 850}));
    EXPECT_TRUE(guiInstance.handle(input::Event::newMouseButtonUp(
        {input::MouseButton::Left, false, 1, 500, 850})));
    EXPECT_TRUE(clicked);
}

TEST(CharGenDescriptionLayout, should_preserve_k1_authored_description_width) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<gui::MockGUI> gui;
    gui::Label remaining(gui, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    gui::ListBox description(gui, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    remaining.setExtent({20, 20, 200, 40});
    description.setExtent({300, 80, 260, 300});

    styleChargenTitles(game, remaining, description);

    EXPECT_EQ(description.extent().left, 300);
    EXPECT_EQ(description.extent().width, 260);
}

TEST(CharGenDescriptionLayout, should_align_tsl_description_with_remaining_panel) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    NiceMock<gui::MockGUI> gui;
    gui::Label remaining(gui, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    gui::ListBox description(gui, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    remaining.setExtent({300, 20, 320, 40});
    description.setExtent({340, 80, 240, 300});

    styleChargenTitles(game, remaining, description);

    EXPECT_EQ(description.extent().left, 340);
    EXPECT_EQ(description.extent().width, 280);
}

TEST(GUIScaledBorders, should_scale_border_and_hilight_dimensions_equally) {
    TestEngine &engine = testEngine();
    graphics::GraphicsOptions options;
    options.width = 1920;
    options.height = 1080;
    options.guiBorderScale = 1.25f;
    gui::GUI guiInstance(options, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    guiInstance.setResolution(800, 600);
    guiInstance.setScaling(gui::GUI::ScalingMode::Scaled);
    TestScaledControl protoRow(guiInstance, engine);
    protoRow.setAuthoredGeometry({100, 100, 200, 40}, 8, 8);

    protoRow.stretch(guiInstance.scale(), guiInstance.scale());

    EXPECT_EQ(protoRow.extent().width, 360);
    EXPECT_EQ(protoRow.extent().height, 72);
    EXPECT_EQ(protoRow.border().dimension, 18);
    EXPECT_EQ(protoRow.hilight().dimension, 18);
}

TEST(GUIListScale, should_change_row_density_without_changing_the_list_viewport) {
    TestEngine &engine = testEngine();
    graphics::GraphicsOptions options;
    options.guiListScale = 0.5f;
    options.guiTextScale = 0.75f;
    gui::GUI guiInstance(options, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);

    resource::generated::GUI_CONTROLS definition;
    definition.CONTROLTYPE = static_cast<int>(gui::ControlType::ListBox);
    definition.EXTENT = {120, 20, 30, 100};
    definition.PADDING = 4;
    resource::generated::GUI_CONTROLS_PROTOITEM proto;
    proto.CONTROLTYPE = static_cast<int>(gui::ControlType::Button);
    proto.EXTENT = {20, 0, 0, 80};
    proto.BORDER.DIMENSION = 2;
    proto.HILIGHT = resource::generated::GUI_BORDER {};
    proto.HILIGHT->DIMENSION = 2;
    definition.PROTOITEM = std::move(proto);

    gui::ListBox listBox(guiInstance, engine.services().scene.graphs, engine.services().graphics, engine.services().resource);
    listBox.load(definition, false);
    for (int i = 0; i < 10; ++i) {
        gui::ListBox::Item item;
        item.tag = std::to_string(i);
        listBox.addItem(std::move(item));
    }

    listBox.stretch(2.0f, 2.0f, gui::Control::kStretchAll);

    EXPECT_EQ(listBox.extent().width, 200);
    EXPECT_EQ(listBox.extent().height, 240);
    EXPECT_EQ(listBox.protoItem().extent().width, 160);
    EXPECT_EQ(listBox.protoItem().extent().height, 20);
    // The 50% row density changes row art and pitch, but text follows the
    // regular GUI layout scale and its own text setting.
    EXPECT_FLOAT_EQ(listBox.protoItem().scale(), 1.5f);
    EXPECT_EQ(listBox.protoItem().border().dimension, 2);
    listBox.handleMouseMotion(0, 119);
    EXPECT_EQ(listBox.selectedItemIndex(), 4);
}

TEST(GUIExternalRendererGeometry, should_scale_action_icons_to_their_button_rect) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto gui = std::make_shared<NiceMock<gui::MockGUI>>();
    auto button = std::make_shared<gui::Button>(
        *gui,
        engine.services().scene.graphs,
        engine.services().graphics,
        engine.services().resource);
    button->setExtent({900, 360, 72, 72});
    ActionBar actionBar(game, engine.services());
    ActionBarTestAccess::addAttackAction(actionBar, button);
    auto icon = std::make_shared<graphics::Texture>("action", graphics::TextureType::TwoDim, graphics::Texture::Properties {});
    ON_CALL(engine.resourceModule().textures(), get(_, _)).WillByDefault(Return(icon));

    glm::mat4 model;
    ON_CALL(engine.graphicsModule().uniforms(), setLocals(_))
        .WillByDefault(Invoke([&](const std::function<void(graphics::LocalUniforms &)> &setter) {
            graphics::LocalUniforms locals;
            setter(locals);
            model = locals.model;
            throw std::runtime_error("captured action icon geometry");
        }));

    EXPECT_THROW(actionBar.render(1.8f), std::runtime_error);
    EXPECT_FLOAT_EQ(model[0][0], 46.8f);
    EXPECT_FLOAT_EQ(model[1][1], 46.8f);
    EXPECT_FLOAT_EQ(model[3][0], 912.6f);
    EXPECT_FLOAT_EQ(model[3][1], 372.6f);
}

TEST(CameraProjection, should_follow_a_resolution_change_after_all_camera_types_load) {
    TestEngine &engine = testEngine();
    auto originalGraphicsOptions = engine.options().graphics;
    engine.options().graphics.width = 800;
    engine.options().graphics.height = 600;
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto &graph = testSceneGraph(engine);
    ON_CALL(graph, newCamera())
        .WillByDefault(Invoke([&]() {
            return std::make_shared<scene::CameraSceneNode>(
                graph,
                engine.services().graphics,
                engine.services().audio,
                engine.services().resource);
        }));

    auto firstPerson = game.newFirstPersonCamera(glm::radians(75.0f));
    auto thirdPerson = game.newThirdPersonCamera({"", 3.2f, 83.0f, 0.45f, 55.0f});
    auto dialog = game.newDialogCamera({"", 3.2f, 83.0f, 0.45f, 55.0f});
    auto animated = game.newAnimatedCamera();
    auto stationary = game.newStaticCamera();

    firstPerson->load();
    thirdPerson->load();
    dialog->load();
    animated->load();
    auto staticCameraGff = Gff::Builder()
                               .field(Gff::Field::newInt("CameraID", 1))
                               .field(Gff::Field::newFloat("FieldOfView", 55.0f))
                               .field(Gff::Field::newFloat("Height", 0.0f))
                               .field(Gff::Field::newVector("Position", glm::vec3(0.0f)))
                               .field(Gff::Field::newOrientation("Orientation", glm::quat(1.0f, 0.0f, 0.0f, 0.0f)))
                               .field(Gff::Field::newFloat("Pitch", 0.0f))
                               .build();
    stationary->deserialize(*staticCameraGff);

    engine.options().graphics.width = 1920;
    engine.options().graphics.height = 1080;

    std::array<Camera *, 5> cameras {firstPerson.get(), thirdPerson.get(), dialog.get(), animated.get(), stationary.get()};
    for (Camera *camera : cameras) {
        camera->update(0.0f);
        auto projection = std::dynamic_pointer_cast<graphics::PerspectiveCamera>(camera->cameraSceneNode()->camera());
        ASSERT_TRUE(projection);
        EXPECT_FLOAT_EQ(projection->aspect(), 16.0f / 9.0f);
    }

    engine.options().graphics = originalGraphicsOptions;
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

// Ordinary LinkedToModule triggers, the kind that carry an area transition
// without a linked door. Retail only lets the player character or the current
// party leader move the party between modules, so a follower crossing a
// reciprocal transition on arrival must not send everyone straight back.
struct ModuleTransitionActivatorFixture : TestWithParam<GameID> {
    ModuleTransitionActivatorFixture() :
        game(GetParam(), "", engine.options(), engine.services(), console) {
    }

    void SetUp() override {
        testSceneGraph(engine);
        area = game.newArea();
        trigger = game.newTrigger();
        trigger->deserialize(*makeTransitionTriggerGff("ebo_m12aa", "K_EBN_RAMP_ENTRANCE"));
        trigger->setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        area->add(trigger);
    }

    /** A creature standing just short of the trigger's southern edge. */
    std::shared_ptr<Creature> creatureBelowTrigger() {
        auto creature = makeMovingCreature(game, engine);
        creature->setPosition(glm::vec3(0.0f, -1.5f, 0.0f));
        area->add(creature);
        return creature;
    }

    /** Walk north, crossing into the trigger polygon. */
    bool stepIn(const std::shared_ptr<Creature> &creature) {
        return area->moveCreature(creature, glm::vec2(0.0f, 1.0f), false, 0.75f);
    }

    std::pair<std::string, std::string> scheduled() const {
        return scheduledTransition(engine, game);
    }

    static std::pair<std::string, std::string> none() {
        return {std::string(), std::string()};
    }

    static std::pair<std::string, std::string> hawk() {
        return {std::string("ebo_m12aa"), std::string("k_ebn_ramp_entrance")};
    }

    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Trigger> trigger;
};

TEST_P(ModuleTransitionActivatorFixture, player_character_may_transition) {
    auto player = creatureBelowTrigger();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setActualPlayer(player);

    ASSERT_TRUE(stepIn(player));

    EXPECT_EQ(scheduled(), hawk());
}

// Controlling a companion makes it the leader. It must keep the ability to
// transition, and nothing here may narrow that to the canonical PC.
TEST_P(ModuleTransitionActivatorFixture, controlled_companion_may_transition) {
    auto player = makeMovingCreature(game, engine);
    player->setPosition(glm::vec3(5.0f, -5.0f, 0.0f));
    area->add(player);
    auto companion = creatureBelowTrigger();
    game.party().addMember(kNpcPlayer, player);
    game.party().setActualPlayer(player);
    game.party().setControlledMember(0, companion);

    ASSERT_EQ(game.party().getLeader(), companion);
    ASSERT_TRUE(stepIn(companion));

    EXPECT_EQ(scheduled(), hawk());
}

// The canonical PC keeps its own right to transition even while a companion
// is the one being controlled.
TEST_P(ModuleTransitionActivatorFixture, player_character_may_transition_while_a_companion_leads) {
    auto player = creatureBelowTrigger();
    auto companion = makeMovingCreature(game, engine);
    companion->setPosition(glm::vec3(5.0f, -5.0f, 0.0f));
    area->add(companion);
    game.party().addMember(kNpcPlayer, player);
    game.party().setActualPlayer(player);
    game.party().setControlledMember(0, companion);

    ASSERT_NE(game.party().getLeader(), player);
    ASSERT_TRUE(stepIn(player));

    EXPECT_EQ(scheduled(), hawk());
}

TEST_P(ModuleTransitionActivatorFixture, following_companion_may_not_transition) {
    auto player = makeMovingCreature(game, engine);
    player->setPosition(glm::vec3(5.0f, -5.0f, 0.0f));
    area->add(player);
    auto follower = creatureBelowTrigger();
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setActualPlayer(player);
    game.party().addMember(0, follower);

    ASSERT_TRUE(stepIn(follower));

    EXPECT_EQ(scheduled(), none())
        << "a following companion must not move the whole party between modules";
}

TEST_P(ModuleTransitionActivatorFixture, non_party_creature_may_not_transition) {
    auto player = makeMovingCreature(game, engine);
    player->setPosition(glm::vec3(5.0f, -5.0f, 0.0f));
    area->add(player);
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setActualPlayer(player);
    auto ambient = creatureBelowTrigger();

    ASSERT_TRUE(stepIn(ambient));

    EXPECT_EQ(scheduled(), none());
}

// The reproduction shape: arriving on Dantooine, a follower crosses the
// reciprocal Hawk transition while the player stands clear.
TEST_P(ModuleTransitionActivatorFixture, follower_does_not_bounce_the_party_back_on_arrival) {
    auto player = makeMovingCreature(game, engine);
    player->setPosition(glm::vec3(-4.0f, -4.0f, 0.0f));
    area->add(player);
    auto follower = makeMovingCreature(game, engine);
    follower->setPosition(glm::vec3(0.5f, -1.5f, 0.0f));
    area->add(follower);
    game.party().addMember(kNpcPlayer, player);
    game.party().setPlayer(player);
    game.party().setActualPlayer(player);
    game.party().addMember(0, follower);

    ASSERT_TRUE(stepIn(follower));
    EXPECT_EQ(scheduled(), none());

    // Deliberately walking the player in later still works normally: the
    // restriction is on who may activate, not on the trigger itself.
    player->setPosition(glm::vec3(-0.5f, -1.5f, 0.0f));
    ASSERT_TRUE(stepIn(player));
    EXPECT_EQ(scheduled(), hawk());
}

INSTANTIATE_TEST_SUITE_P(
    BothGames,
    ModuleTransitionActivatorFixture,
    ::testing::Values(GameID::KotOR, GameID::TSL),
    [](const ::testing::TestParamInfo<GameID> &info) {
        return info.param == GameID::TSL ? "TSL" : "KotOR";
    });

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

TEST(TransitionEntryResolution, matches_odyssey_entry_tags_case_insensitively) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    testSceneGraph(engine);
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    auto module = game.module();
    ASSERT_TRUE(module);

    const glm::vec3 defaultPosition(101.0f, 102.0f, 103.0f);
    const float defaultFacing = 1.25f;
    addEntryWaypoint(game, area, "from_106per2", {-20.45f, 62.86f, 11.89f}, 0.25f);
    addEntryWaypoint(game, area, "from_103per2", {-33.20f, 73.82f, 0.85f}, 0.5f);
    addEntryWaypoint(game, area, "same_case", {4.0f, 5.0f, 6.0f}, 0.75f);
    addEntryWaypoint(game, area, "My_Entry", {7.0f, 8.0f, 9.0f}, 1.0f);
    addEntryWaypoint(game, area, "FROM15", {10.0f, 11.0f, 12.0f}, 1.125f);

    auto into103 = TestGameModule::resolveModuleEntry(
        *module, "From_106PER2", defaultPosition, defaultFacing);
    EXPECT_EQ(into103.first, glm::vec3(-20.45f, 62.86f, 11.89f));
    EXPECT_FLOAT_EQ(into103.second, 0.25f);

    auto into106 = TestGameModule::resolveModuleEntry(
        *module, "FROM_103PER2", defaultPosition, defaultFacing);
    EXPECT_EQ(into106.first, glm::vec3(-33.20f, 73.82f, 0.85f));
    EXPECT_FLOAT_EQ(into106.second, 0.5f);

    auto sameCase = TestGameModule::resolveModuleEntry(
        *module, "same_case", defaultPosition, defaultFacing);
    EXPECT_EQ(sameCase.first, glm::vec3(4.0f, 5.0f, 6.0f));

    auto modCase = TestGameModule::resolveModuleEntry(
        *module, "my_entry", defaultPosition, defaultFacing);
    EXPECT_EQ(modCase.first, glm::vec3(7.0f, 8.0f, 9.0f));

    auto k1Case = TestGameModule::resolveModuleEntry(
        *module, "from15", defaultPosition, defaultFacing);
    EXPECT_EQ(k1Case.first, glm::vec3(10.0f, 11.0f, 12.0f));

    auto absent = TestGameModule::resolveModuleEntry(
        *module, "genuinely_absent", defaultPosition, defaultFacing);
    EXPECT_EQ(absent.first, defaultPosition);
    EXPECT_FLOAT_EQ(absent.second, defaultFacing);

    addEntryWaypoint(game, area, "EntryFoo", {1.0f, 2.0f, 3.0f});
    addEntryWaypoint(game, area, "entryfoo", {4.0f, 5.0f, 6.0f});

    auto resolved = TestGameModule::resolveModuleEntry(
        *module, "ENTRYFOO", defaultPosition, defaultFacing);
    EXPECT_EQ(resolved.first, glm::vec3(1.0f, 2.0f, 3.0f));
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
    // The subject here is the trigger's own enter/exit/re-enter lifecycle, so
    // the mover is the party leader, who may activate a module transition.
    auto leader = makeMovingCreature(game, engine);
    leader->setPosition(glm::vec3(0.0f, -2.0f, 0.0f));
    game.party().addMember(kNpcPlayer, leader);
    game.party().setPlayer(leader);
    game.party().setActualPlayer(leader);
    area->add(trigger);
    area->add(leader);

    EXPECT_FALSE(trigger->isLinkedDoorTransition());
    EXPECT_TRUE(trigger->isActive());
    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 1.25f));
    EXPECT_TRUE(trigger->isTenant(leader));
    EXPECT_EQ(
        scheduledTransition(engine, game),
        std::make_pair(std::string("authored_module"), std::string("authored_waypoint")));

    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 2.0f));
    trigger->update(0.0f);
    EXPECT_FALSE(trigger->isTenant(leader));

    ASSERT_TRUE(area->moveCreature(leader, glm::vec2(0.0f, -1.0f), false, 0.5f));
    EXPECT_TRUE(trigger->isTenant(leader));
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

TEST(XPStatusSummary, cached_gui_preserves_text_across_retirement_and_load_publication) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    NiceMock<gui::MockGUI> gui;
    auto description = std::make_shared<gui::Label>(
        gui,
        engine.services().scene.graphs,
        engine.services().graphics,
        engine.services().resource);
    description->setTextMessage("Experience: <CUSTOM0>");

    game.submitStatusSummary(StatusSummaryCategory::PlotXP, 350);
    TestStatusSummary newGameSummary(game, engine.services(), game.statusSummary());
    newGameSummary.captureDescription(StatusSummaryCategory::PlotXP, description);
    EXPECT_EQ(
        "Experience: 350",
        newGameSummary.formatCapturedDescription(
            StatusSummaryCategory::PlotXP,
            game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP)));

    newGameSummary.reset();
    EXPECT_EQ("Experience: <CUSTOM0>", description->text().text);

    game.statusSummary().reset();
    game.submitStatusSummary(StatusSummaryCategory::PlotXP, 350);
    TestStatusSummary loadedGameSummary(game, engine.services(), game.statusSummary());
    loadedGameSummary.captureDescription(StatusSummaryCategory::PlotXP, description);
    EXPECT_EQ(
        "Experience: 350",
        loadedGameSummary.formatCapturedDescription(
            StatusSummaryCategory::PlotXP,
            game.statusSummary().pending().entry(StatusSummaryCategory::PlotXP)));
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

namespace {

// Script execution counts, keyed by resref. Static so the stub registered on the
// shared scripts mock stays valid past the test that installed it.
std::map<std::string, int> &scriptRunCounts() {
    static std::map<std::string, int> counts;
    return counts;
}

struct BlockedDoorFixtureBase {
    TestEngine &engine;
    StubConsole console;
    Game game;
    std::shared_ptr<Area> area;

    BlockedDoorFixtureBase() :
        engine(testEngine()),
        game(GameID::KotOR, "", engine.options(), engine.services(), console) {

        game.initLocalServices();
        area = game.newArea();
        TestGameModule::setActiveModuleArea(game, area);
    }

    std::shared_ptr<Creature> addCreature(std::string onBlocked) {
        auto creature = makeMovingCreature(game, engine, std::move(onBlocked));
        creature->setPosition(glm::vec3(0.0f));
        area->add(creature);
        return creature;
    }

    // Start counting executions of a script. The script itself resolves to
    // nothing, which is enough to observe dispatch.
    void countScriptRuns(const std::string &resRef) {
        scriptRunCounts()[resRef] = 0;
        EXPECT_CALL(engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](const std::string &key) {
                ++scriptRunCounts()[key];
                return std::shared_ptr<script::ScriptProgram>();
            }));
    }

    int scriptRuns(const std::string &resRef) const {
        return scriptRunCounts()[resRef];
    }
};

// A door that swaps its collision the instant it is told to open: makePlainDoor
// gives it no model, so there is no opening animation to wait on.
struct BlockedDoorFixture : BlockedDoorFixtureBase {
    std::shared_ptr<Door> door;

    explicit BlockedDoorFixture(bool locked = false, std::string onOpen = "") {
        door = makePlainDoor(game, engine, locked, std::move(onOpen));
        area->add(door);
    }
};

// A door that actually swings. makeLifecycleDoor gives it a real opening1
// animation, so it spends several frames in the doorway on its way open, which
// is the case the blocked event has to sit through without repeating.
struct SwingingDoorFixture : BlockedDoorFixtureBase {
    std::shared_ptr<Door> door;

    SwingingDoorFixture() {
        door = makeLifecycleDoor(game, engine, /*openState=*/0);
        area->add(door);
    }

    // Keep the walk obstruction in step with what the door's own collision is
    // doing, so navigation meets exactly the doorway the door presents.
    void syncObstruction() {
        if (doorwayBlocks(*door)) {
            if (!_obstruction) {
                _obstruction.emplace(*door);
            }
        } else {
            _obstruction.reset();
        }
    }

private:
    std::optional<ScopedWalkObstruction> _obstruction;
};

const glm::vec3 kFarDestination {0.0f, 10.0f, 0.0f};

} // namespace

TEST(CreatureBlockedByDoor, should_record_the_door_that_obstructs_navigation) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.countScriptRuns("k_def_blocked01");

    EXPECT_EQ(script::kObjectInvalid, npc->blockingDoorId());

    ScopedWalkObstruction obstruction(*fixture.door);
    navigationStep(*npc, kFarDestination);

    EXPECT_EQ(fixture.door->id(), npc->blockingDoorId());
    EXPECT_EQ(1, fixture.scriptRuns("k_def_blocked01"));
}

TEST(CreatureBlockedByDoor, should_dispatch_authored_blocked_script_once_per_continuous_obstruction) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");

    fixture.countScriptRuns("k_def_blocked01");

    ScopedWalkObstruction obstruction(*fixture.door);
    for (int i = 0; i < 10; ++i) {
        navigationStep(*npc, kFarDestination);
    }

    // Ten obstructed steps against the same door, one dispatch.
    EXPECT_EQ(1, fixture.scriptRuns("k_def_blocked01"));
}

TEST(CreatureBlockedByDoor, should_rearm_blocked_script_after_unobstructed_movement) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.countScriptRuns("k_def_blocked01");

    {
        ScopedWalkObstruction obstruction(*fixture.door);
        navigationStep(*npc, kFarDestination);
        navigationStep(*npc, kFarDestination);
    }
    EXPECT_EQ(1, fixture.scriptRuns("k_def_blocked01"));

    // An unobstructed step re-arms.
    navigationStep(*npc, kFarDestination);
    EXPECT_EQ(script::kObjectInvalid, npc->blockingDoorId());

    ScopedWalkObstruction obstruction(*fixture.door);
    navigationStep(*npc, kFarDestination);
    EXPECT_EQ(fixture.door->id(), npc->blockingDoorId());
    EXPECT_EQ(2, fixture.scriptRuns("k_def_blocked01"));
}

// A door keeps filling the doorway for the whole of its opening animation, so a
// creature walking into one it has already reported keeps meeting the same
// blocker for several frames. That is one obstruction, not one per frame.
TEST(CreatureBlockedByDoor, should_not_repeat_while_the_door_it_reported_is_opening) {
    SwingingDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.countScriptRuns("k_def_blocked01");

    fixture.syncObstruction();
    ASSERT_TRUE(doorwayBlocks(*fixture.door));

    navigationStep(*npc, kFarDestination);
    EXPECT_EQ(fixture.door->id(), npc->blockingDoorId());
    EXPECT_EQ(1, fixture.scriptRuns("k_def_blocked01"));

    // What the authored AI does in response to that one report.
    fixture.door->open();
    ASSERT_TRUE(fixture.door->isOpening());
    ASSERT_TRUE(doorwayBlocks(*fixture.door));

    // Frames inside the one-second opening animation. The door is on its way
    // but has not arrived, so it is still standing in the doorway.
    for (int frame = 0; frame < 3; ++frame) {
        stepDoor(*fixture.door, 0.4f);
        fixture.syncObstruction();
        ASSERT_TRUE(doorwayBlocks(*fixture.door)) << "frame " << frame;
        navigationStep(*npc, kFarDestination);
        EXPECT_TRUE(fixture.door->isOpening()) << "frame " << frame;
        EXPECT_EQ(fixture.door->id(), npc->blockingDoorId()) << "frame " << frame;
    }
    EXPECT_EQ(1, fixture.scriptRuns("k_def_blocked01"));

    // The transition arrives and the doorway opens up.
    stepDoor(*fixture.door, 0.4f);
    fixture.syncObstruction();
    ASSERT_FALSE(fixture.door->isOpening());
    ASSERT_FALSE(doorwayBlocks(*fixture.door));
    ASSERT_TRUE(fixture.door->isOpen());

    // The movement that was blocked all along now makes progress, and the
    // blocked state clears so this door can report again another time.
    float before = npc->position().y;
    navigationStep(*npc, kFarDestination);
    EXPECT_GT(npc->position().y, before);
    EXPECT_EQ(script::kObjectInvalid, npc->blockingDoorId());
    EXPECT_EQ(1, fixture.scriptRuns("k_def_blocked01"));
}

// A different door taking over the doorway is a new obstruction, and reports in
// its own right even though the creature never got an unobstructed step.
TEST(CreatureBlockedByDoor, should_report_another_door_that_takes_over_the_obstruction) {
    BlockedDoorFixture fixture;
    auto other = makePlainDoor(fixture.game, fixture.engine, false, "", glm::vec3(0.0f, 2.0f, 0.0f));
    fixture.area->add(other);
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.countScriptRuns("k_def_blocked01");

    {
        ScopedWalkObstruction obstruction(*fixture.door);
        navigationStep(*npc, kFarDestination);
    }
    EXPECT_EQ(fixture.door->id(), npc->blockingDoorId());
    EXPECT_EQ(1, fixture.scriptRuns("k_def_blocked01"));

    {
        ScopedWalkObstruction obstruction(*other);
        navigationStep(*npc, kFarDestination);
    }
    EXPECT_EQ(other->id(), npc->blockingDoorId());
    EXPECT_EQ(2, fixture.scriptRuns("k_def_blocked01"));
}

TEST(CreatureBlockedByDoor, should_run_the_script_authored_on_each_creature) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    auto companion = fixture.addCreature("k_hen_blocked01");
    fixture.game.party().addMember(0, companion);

    // Each creature runs what its own template names. The engine picks neither
    // the script nor the 1009/2009 event number those scripts pass on.
    fixture.countScriptRuns("k_def_blocked01");
    fixture.countScriptRuns("k_hen_blocked01");

    ScopedWalkObstruction obstruction(*fixture.door);
    navigationStep(*npc, kFarDestination);
    navigationStep(*companion, kFarDestination);

    EXPECT_EQ(1, fixture.scriptRuns("k_def_blocked01"));
    EXPECT_EQ(1, fixture.scriptRuns("k_hen_blocked01"));
}

TEST(CreatureBlockedByDoor, should_not_dispatch_for_directly_controlled_player_locomotion) {
    BlockedDoorFixture fixture;
    auto leader = fixture.addCreature("k_hen_blocked01");
    fixture.game.party().addMember(kNpcPlayer, leader);
    fixture.game.party().setPlayer(leader);

    // Player::update drives the leader through Area::moveCreature directly and
    // never through navigation, so no blocked event is raised.
    fixture.countScriptRuns("k_hen_blocked01");

    ScopedWalkObstruction obstruction(*fixture.door);
    EXPECT_FALSE(fixture.area->moveCreature(leader, glm::vec2(0.0f, 1.0f), false, 1.0f));

    EXPECT_EQ(0, fixture.scriptRuns("k_hen_blocked01"));

    // The collision layer still records what obstructed the leader.
    EXPECT_EQ(fixture.door->id(), leader->blockingDoorId());
}

TEST(CreatureBlockedByDoor, should_ignore_obstructions_that_are_not_doors) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.countScriptRuns("k_def_blocked01");

    Room room("testroom", glm::vec3(0.0f), nullptr, nullptr, nullptr);
    ScopedWalkObstruction obstruction(room);
    navigationStep(*npc, kFarDestination);

    EXPECT_EQ(script::kObjectInvalid, npc->blockingDoorId());
    EXPECT_EQ(0, fixture.scriptRuns("k_def_blocked01"));
}

// Invoke GetBlockingDoor with the arguments a blocked-event run would carry.
uint32_t callGetBlockingDoor(Routines &routines, const script::ExecutionContext &execution) {
    script::ExecutionContext copy(execution);
    return routines.get(336).invoke({}, copy).objectId;
}

script::ExecutionContext blockedEventContext(uint32_t callerId, uint32_t blockingDoorId) {
    script::ExecutionContext execution;
    execution.args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(callerId));
    execution.args.emplace_back(script::ArgKind::BlockingDoor, script::Variable::ofObject(blockingDoorId));
    return execution;
}

TEST(BlockingDoorRoutines, get_blocking_door_reports_the_door_captured_by_the_event) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();

    auto execution = blockedEventContext(npc->id(), fixture.door->id());

    EXPECT_EQ(fixture.door->id(), callGetBlockingDoor(routines, execution));
}

TEST(BlockingDoorRoutines, get_blocking_door_reports_invalid_without_a_captured_door) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();

    // A run that is not a blocked event carries no such argument, whoever the
    // caller is.
    script::ExecutionContext execution;
    execution.args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(npc->id()));

    EXPECT_EQ(script::kObjectInvalid, callGetBlockingDoor(routines, execution));
}

TEST(BlockingDoorRoutines, get_blocking_door_keeps_the_captured_door_when_the_obstruction_moves_on) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.countScriptRuns("k_def_blocked01");
    auto other = makePlainDoor(fixture.game, fixture.engine);
    fixture.area->add(other);
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();

    // Blocked by the first door, and a continuation of that run holds it.
    {
        ScopedWalkObstruction obstruction(*fixture.door);
        navigationStep(*npc, kFarDestination);
    }
    ASSERT_EQ(fixture.door->id(), npc->blockingDoorId());
    auto execution = blockedEventContext(npc->id(), fixture.door->id());

    // The creature then runs into a different door entirely.
    {
        ScopedWalkObstruction obstruction(*other);
        navigationStep(*npc, kFarDestination);
    }
    ASSERT_EQ(other->id(), npc->blockingDoorId());

    // The continuation still speaks for the event it came from.
    EXPECT_EQ(fixture.door->id(), callGetBlockingDoor(routines, execution));
}

TEST(BlockingDoorRoutines, a_captured_door_that_is_destroyed_is_reported_but_not_valid) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();

    auto execution = blockedEventContext(npc->id(), fixture.door->id());
    TestGameModule::removeObject(fixture.game, fixture.door->id());

    // GetBlockingDoor keeps no validity policy of its own: the captured id comes
    // back, and it is GetIsObjectValid that reports the object has gone.
    EXPECT_EQ(fixture.door->id(), callGetBlockingDoor(routines, execution));

    script::ExecutionContext validityCtx(execution);
    std::vector<script::Variable> validityArgs {script::Variable::ofObject(fixture.door->id())};
    EXPECT_EQ(0, routines.get(42).invoke(validityArgs, validityCtx).intValue);
}

TEST(BlockingDoorRoutines, the_authored_blocked_script_can_act_on_the_door_that_blocked_it) {
    BlockedDoorFixture fixture;
    auto npc = fixture.addCreature("k_def_blocked01");
    ASSERT_FALSE(fixture.door->isLocked());

    // An OnBlocked script that locks whatever door GetBlockingDoor hands it.
    // SetLocked takes the object on top of the flag, matching how the shipped
    // scripts push it.
    auto program = std::make_shared<script::ScriptProgram>("k_def_blocked01");
    program->add(script::Instruction::newCONSTI(1));
    program->add(script::Instruction::newACTION(336, 0));
    program->add(script::Instruction::newACTION(324, 2));
    program->add(script::Instruction(script::InstructionType::RETN));
    EXPECT_CALL(fixture.engine.resourceModule().scripts(), get("k_def_blocked01"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(program));

    {
        ScopedWalkObstruction obstruction(*fixture.door);
        navigationStep(*npc, kFarDestination);
    }

    // The door reached the script, so the argument survived dispatch.
    EXPECT_TRUE(fixture.door->isLocked());
}

TEST(BlockingDoorRoutines, unlocked_door_is_openable_and_door_action_open_opens_it) {
    BlockedDoorFixture fixture(false, "door_on_open");
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.countScriptRuns("door_on_open");
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    script::ExecutionContext execution;
    execution.args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(npc->id()));

    auto possible = routines.get(337).invoke(
        {script::Variable::ofObject(fixture.door->id()),
         script::Variable::ofInt(static_cast<int>(DoorAction::Open))},
        execution);
    EXPECT_EQ(1, possible.intValue);

    ASSERT_FALSE(fixture.door->isOpen());

    routines.get(338).invoke(
        {script::Variable::ofObject(fixture.door->id()),
         script::Variable::ofInt(static_cast<int>(DoorAction::Open))},
        execution);

    // Opened, and the OnOpen event fired, exactly as for any other open.
    EXPECT_TRUE(fixture.door->isOpen());
    EXPECT_EQ(1, fixture.scriptRuns("door_on_open"));
}

TEST(BlockingDoorRoutines, door_action_open_leaves_the_same_state_as_open_door_action) {
    BlockedDoorFixture routineFixture;
    auto routineNpc = routineFixture.addCreature("k_def_blocked01");
    Routines routines(GameID::KotOR, &routineFixture.game, &routineFixture.engine.services());
    routines.init();
    script::ExecutionContext execution;
    execution.args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(routineNpc->id()));

    routines.get(338).invoke(
        {script::Variable::ofObject(routineFixture.door->id()),
         script::Variable::ofInt(static_cast<int>(DoorAction::Open))},
        execution);

    BlockedDoorFixture actionFixture;
    auto actionNpc = actionFixture.addCreature("k_def_blocked01");
    actionNpc->setPosition(actionFixture.door->position());
    auto action = actionFixture.game.newAction<OpenDoorAction>(actionFixture.door);
    action->execute(action, *actionNpc, 0.0f);

    // The routine and the action leave a door in the same state, because both
    // go through Door::open.
    EXPECT_EQ(actionFixture.door->isOpen(), routineFixture.door->isOpen());
    EXPECT_EQ(actionFixture.door->isLocked(), routineFixture.door->isLocked());
    EXPECT_EQ(actionFixture.door->isSelectable(), routineFixture.door->isSelectable());
    EXPECT_TRUE(routineFixture.door->isOpen());
}

TEST(BlockingDoorRoutines, locked_door_is_not_openable_and_door_action_open_leaves_it_shut) {
    BlockedDoorFixture fixture(true);
    auto npc = fixture.addCreature("k_def_blocked01");
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    script::ExecutionContext execution;
    execution.args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(npc->id()));

    auto possible = routines.get(337).invoke(
        {script::Variable::ofObject(fixture.door->id()),
         script::Variable::ofInt(static_cast<int>(DoorAction::Open))},
        execution);
    EXPECT_EQ(0, possible.intValue);

    routines.get(338).invoke(
        {script::Variable::ofObject(fixture.door->id()),
         script::Variable::ofInt(static_cast<int>(DoorAction::Open))},
        execution);

    EXPECT_FALSE(fixture.door->isOpen());
    EXPECT_TRUE(fixture.door->isLocked());
}

TEST(BlockingDoorRoutines, bash_follows_shared_eligibility_and_keeps_the_blocked_action) {
    BlockedDoorFixture fixture(true);
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.door->setCurrentHitPoints(20);
    auto &reputes = static_cast<MockReputes &>(fixture.engine.services().game.reputes);
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    script::ExecutionContext execution;
    execution.args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(npc->id()));

    auto bashPossible = [&]() {
        return routines.get(337)
            .invoke(
                {script::Variable::ofObject(fixture.door->id()),
                 script::Variable::ofInt(static_cast<int>(DoorAction::Bash))},
                execution)
            .intValue;
    };

    // Not an enemy of the door: the same answer the player context action gives.
    EXPECT_CALL(reputes, getIsEnemy(A<Faction>(), A<Faction>()))
        .WillRepeatedly(Return(false));
    EXPECT_EQ(0, bashPossible());

    Mock::VerifyAndClearExpectations(&reputes);
    EXPECT_CALL(reputes, getIsEnemy(A<Faction>(), A<Faction>()))
        .WillRepeatedly(Return(true));
    EXPECT_EQ(1, bashPossible());

    // A plot door is never bashable, so the AI cannot get through it at all.
    fixture.door->setPlotFlag(true);
    EXPECT_EQ(0, bashPossible());
    fixture.door->setPlotFlag(false);

    // The movement the creature was blocked on stays queued behind the bash.
    auto movement = fixture.game.newAction<MoveToPointAction>(kFarDestination);
    npc->addAction(movement);

    routines.get(338).invoke(
        {script::Variable::ofObject(fixture.door->id()),
         script::Variable::ofInt(static_cast<int>(DoorAction::Bash))},
        execution);

    ASSERT_EQ(2, npc->actions().size());
    EXPECT_EQ(ActionType::AttackObject, npc->actions().front()->type());
    EXPECT_EQ(movement, npc->actions().back());
}

TEST(BlockingDoorRoutines, unsupported_door_actions_are_impossible_and_do_nothing) {
    BlockedDoorFixture fixture(true);
    auto npc = fixture.addCreature("k_def_blocked01");
    Routines routines(GameID::KotOR, &fixture.game, &fixture.engine.services());
    routines.init();
    script::ExecutionContext execution;
    execution.args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(npc->id()));

    for (auto action : {DoorAction::Unlock, DoorAction::Ignore, DoorAction::Knock}) {
        auto possible = routines.get(337).invoke(
            {script::Variable::ofObject(fixture.door->id()),
             script::Variable::ofInt(static_cast<int>(action))},
            execution);
        EXPECT_EQ(0, possible.intValue);

        routines.get(338).invoke(
            {script::Variable::ofObject(fixture.door->id()),
             script::Variable::ofInt(static_cast<int>(action))},
            execution);
    }

    EXPECT_FALSE(fixture.door->isOpen());
    EXPECT_TRUE(fixture.door->isLocked());
    EXPECT_TRUE(npc->actions().empty());
}

TEST(BlockingDoorRoutines, are_registered_for_both_kotor_and_tsl) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines k1Routines(GameID::KotOR, &game, &engine.services());
    Routines k2Routines(GameID::TSL, &game, &engine.services());
    k1Routines.init();
    k2Routines.init();

    for (auto *routines : {&k1Routines, &k2Routines}) {
        EXPECT_EQ("GetBlockingDoor", routines->get(336).name());
        EXPECT_EQ("GetIsDoorActionPossible", routines->get(337).name());
        EXPECT_EQ("DoDoorAction", routines->get(338).name());
        EXPECT_EQ(336, routines->getIndexByName("GetBlockingDoor"));
        EXPECT_EQ(337, routines->getIndexByName("GetIsDoorActionPossible"));
        EXPECT_EQ(338, routines->getIndexByName("DoDoorAction"));
    }
}

TEST(OpenDoorAction, still_opens_an_unlocked_door_and_fires_on_open) {
    BlockedDoorFixture fixture(false, "door_on_open");
    auto npc = fixture.addCreature("k_def_blocked01");
    fixture.countScriptRuns("door_on_open");
    npc->setPosition(fixture.door->position());

    auto action = fixture.game.newAction<OpenDoorAction>(fixture.door);
    action->execute(action, *npc, 0.0f);

    EXPECT_TRUE(action->isCompleted());
    EXPECT_TRUE(fixture.door->isOpen());
    EXPECT_EQ(1, fixture.scriptRuns("door_on_open"));
}

TEST(OpenDoorAction, still_refuses_a_locked_door_and_fires_on_fail_to_open) {
    BlockedDoorFixture fixture(true);
    auto npc = fixture.addCreature("k_def_blocked01");
    auto gff = Gff::Builder()
                   .field(Gff::Field::newResRef("OnFailToOpen", "door_on_fail"))
                   .build();
    fixture.door->deserialize(*gff);
    fixture.countScriptRuns("door_on_fail");
    npc->setPosition(fixture.door->position());

    auto action = fixture.game.newAction<OpenDoorAction>(fixture.door);
    action->execute(action, *npc, 0.0f);

    EXPECT_TRUE(action->isCompleted());
    EXPECT_FALSE(fixture.door->isOpen());
    EXPECT_TRUE(fixture.door->isLocked());
    EXPECT_EQ(1, fixture.scriptRuns("door_on_fail"));
}

namespace {

// Locomotion is what an overlay has to survive, and Creature drives it with
// loopBlend, so the base channel below the overlay is a blended looping one.
constexpr int kLocomotionFlags = scene::AnimationFlags::loopBlend | scene::AnimationFlags::propagate;

struct OverlayFixture {
    graphics::GraphicsOptions graphicsOptions;
    std::shared_ptr<graphics::Model> model;
    std::unique_ptr<scene::SceneGraph> graph;
    std::shared_ptr<scene::ModelSceneNode> node;

    std::vector<std::string> channelNames() const {
        std::vector<std::string> names;
        for (auto &channel : node->animationChannels()) {
            names.push_back(channel.anim->name());
        }
        return names;
    }
};

// A creature with a live model carrying both spellings of the dive roll plus a
// run clip to act as locomotion underneath it.
void setUpOverlay(OverlayFixture &fixture, TestEngine &engine) {
    fixture.model = makeModel(
        "body",
        {makeAnimation("run"),
         makeAnimation("pause1"),
         makeAnimation("diveroll"),
         makeAnimation("cdiveroll")});
    fixture.graph = std::make_unique<scene::SceneGraph>(
        "test",
        engine.sceneModule().renderPipelineFactory(),
        fixture.graphicsOptions,
        engine.services().graphics,
        engine.services().audio,
        engine.services().resource);
    fixture.node = fixture.graph->newModel(*fixture.model, scene::ModelUsage::Creature);
}

// A creature whose appearance row makes it a body model, i.e. the humanoid
// naming branch the player character takes.
void makeHumanoid(TestCreature &creature, TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto gff = Gff::Builder()
                   .field(Gff::Field::newDword("Appearance_Type", 3))
                   .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                   .field(Gff::Field::newByte("BodyBag", 0xff))
                   .field(Gff::Field::newByte("PerceptionRange", 0xff))
                   .build();
    creature.deserialize(*gff);
    ASSERT_EQ(Creature::ModelType::Character, creature.modelType());
}

} // namespace

// A. The dive roll resolves through the ordinary animation naming path, on both
// sides of the creature/body model split.
TEST(OverlayAnimation, should_resolve_the_dive_roll_for_a_body_model) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    OverlayFixture fixture;
    setUpOverlay(fixture, engine);
    TestCreature creature(1, "test", game, engine.services());
    makeHumanoid(creature, engine);
    creature.setSceneNode(fixture.node);

    creature.playOverlayAnimation(AnimationType::FireForgetDiveRoll);

    EXPECT_EQ("diveroll", fixture.node->activeAnimationName());
}

TEST(OverlayAnimation, should_resolve_the_dive_roll_for_a_creature_model) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    OverlayFixture fixture;
    setUpOverlay(fixture, engine);
    TestCreature creature(1, "test", game, engine.services());
    ASSERT_EQ(Creature::ModelType::Creature, creature.modelType());
    creature.setSceneNode(fixture.node);

    creature.playOverlayAnimation(AnimationType::FireForgetDiveRoll);

    EXPECT_EQ("cdiveroll", fixture.node->activeAnimationName());
}

// C and D. The overlay plays while the creature is running, and the locomotion
// channel underneath survives it. The other playAnimation overloads refuse
// outright while a creature is moving, which is exactly what an overlay must
// not do.
TEST(OverlayAnimation, should_layer_over_locomotion_without_displacing_it) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    OverlayFixture fixture;
    setUpOverlay(fixture, engine);
    TestCreature creature(1, "test", game, engine.services());
    creature.setSceneNode(fixture.node);
    creature.setMovementType(Creature::MovementType::Run);
    fixture.node->playAnimation(
        "run", nullptr, scene::AnimationProperties::fromFlags(kLocomotionFlags));
    ASSERT_EQ("run", fixture.node->activeAnimationName());

    creature.playOverlayAnimation(AnimationType::FireForgetDiveRoll);

    // The overlay is on top, and the run it was layered over is still there.
    EXPECT_EQ("cdiveroll", fixture.node->activeAnimationName());
    EXPECT_THAT(fixture.channelNames(), ElementsAre("cdiveroll", "run"));
}

// A normal animation request is still refused while moving, so the overlay path
// is doing something the ordinary one cannot.
TEST(OverlayAnimation, should_still_refuse_a_plain_animation_while_moving) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    OverlayFixture fixture;
    setUpOverlay(fixture, engine);
    TestCreature creature(1, "test", game, engine.services());
    creature.setSceneNode(fixture.node);
    creature.setMovementType(Creature::MovementType::Run);
    fixture.node->playAnimation(
        "run", nullptr, scene::AnimationProperties::fromFlags(kLocomotionFlags));

    creature.playAnimation("cdiveroll");

    EXPECT_EQ("run", fixture.node->activeAnimationName());
}

// C. Queued actions are untouched: the routine places nothing on the queue and
// completes nothing already on it.
TEST(OverlayAnimation, should_leave_the_action_queue_alone) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    OverlayFixture fixture;
    setUpOverlay(fixture, engine);
    TestCreature creature(1, "test", game, engine.services());
    creature.setSceneNode(fixture.node);
    creature.setMovementType(Creature::MovementType::Run);
    auto pending = game.newAction<PlayAnimationAction>(AnimationType::LoopingPause, 1.0f, 0.0f);
    creature.addAction(pending);
    ASSERT_EQ(1u, creature.actions().size());

    creature.playOverlayAnimation(AnimationType::FireForgetDiveRoll);

    EXPECT_EQ(1u, creature.actions().size());
    EXPECT_EQ(pending, creature.getCurrentAction());
    EXPECT_FALSE(pending->isCompleted());
}

// F. Fire and forget: the layer expires on its own and the locomotion beneath
// it becomes current again.
TEST(OverlayAnimation, should_expire_and_hand_back_to_locomotion) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    OverlayFixture fixture;
    setUpOverlay(fixture, engine);
    TestCreature creature(1, "test", game, engine.services());
    creature.setSceneNode(fixture.node);
    creature.setMovementType(Creature::MovementType::Run);
    fixture.node->playAnimation(
        "run", nullptr, scene::AnimationProperties::fromFlags(kLocomotionFlags));
    creature.playOverlayAnimation(AnimationType::FireForgetDiveRoll);
    ASSERT_EQ("cdiveroll", fixture.node->activeAnimationName());

    // Part way through, the layer is still on top.
    fixture.node->update(0.5f);
    EXPECT_EQ("cdiveroll", fixture.node->activeAnimationName());

    // The clip is one second long; once past it the layer is dropped.
    fixture.node->update(0.6f);
    fixture.node->update(0.1f);

    EXPECT_EQ("run", fixture.node->activeAnimationName());
    EXPECT_THAT(fixture.channelNames(), ElementsAre("run"));
}

// E. An animation with no mapping is a no-op rather than a corruption.
TEST(OverlayAnimation, should_ignore_an_animation_it_cannot_name) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    OverlayFixture fixture;
    setUpOverlay(fixture, engine);
    TestCreature creature(1, "test", game, engine.services());
    creature.setSceneNode(fixture.node);
    fixture.node->playAnimation(
        "run", nullptr, scene::AnimationProperties::fromFlags(kLocomotionFlags));

    creature.playOverlayAnimation(AnimationType::FireForgetScream);

    EXPECT_EQ("run", fixture.node->activeAnimationName());
    EXPECT_THAT(fixture.channelNames(), ElementsAre("run"));
}

TEST(OverlayAnimation, should_ignore_an_overlay_on_a_creature_with_no_model) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    TestCreature creature(1, "test", game, engine.services());
    ASSERT_FALSE(creature.sceneNode());

    // Nothing to animate, so this must simply do nothing.
    creature.playOverlayAnimation(AnimationType::FireForgetDiveRoll);

    EXPECT_FALSE(creature.sceneNode());
}

// B and E. Routine 854 resolves its target and drives the overlay, and a
// non-creature target is reported rather than crashing.
TEST(OverlayAnimation, routine_854_plays_the_overlay_on_its_target) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    Routines routines(GameID::TSL, &game, &engine.services());
    routines.init();
    auto &routine = routines.get(854);
    ASSERT_EQ("PlayOverlayAnimation", routine.name());

    // Registered with the game so the routine can resolve it by id, and given a
    // model so the overlay it plays is observable.
    OverlayFixture fixture;
    setUpOverlay(fixture, engine);
    auto creature = game.newObject<TestCreature>("test", game, engine.services());
    creature->setSceneNode(fixture.node);
    fixture.node->playAnimation(
        "run", nullptr, scene::AnimationProperties::fromFlags(kLocomotionFlags));
    auto placeable = game.newPlaceable();
    script::ExecutionContext ctx;

    routine.invoke(
        {script::Variable::ofObject(creature->id()), script::Variable::ofInt(123)}, ctx);

    // The requested animation reached the creature and was layered on.
    EXPECT_EQ("cdiveroll", fixture.node->activeAnimationName());
    EXPECT_THAT(fixture.channelNames(), ElementsAre("cdiveroll", "run"));

    // A non-creature target is rejected by argument checking, which the routine
    // framework turns into a logged failure and a default return value, leaving
    // the animation state alone.
    auto result = routine.invoke(
        {script::Variable::ofObject(placeable->id()), script::Variable::ofInt(123)}, ctx);
    EXPECT_EQ(script::VariableType::Void, result.type);
    EXPECT_EQ("cdiveroll", fixture.node->activeAnimationName());
}

TEST(SavedRuntimeState, restores_explicit_object_identity_and_allocator_cursors) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto ifo = Gff::Builder()
                   .field(Gff::Field::newDword("Mod_NextObjId0", 700))
                   .field(Gff::Field::newDword64("Mod_Effect_NxtId", 900))
                   .build();
    game.prepareSavedRuntimeNamespace(*ifo);

    auto saved = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 650))
                     .build();
    auto item = game.newItem(*saved);
    item->deserializeRuntimeState(*saved);

    EXPECT_EQ(650u, item->id());
    EXPECT_EQ(item, game.getObjectById(650));
    EXPECT_EQ(700u, game.newItem()->id());
    EXPECT_EQ(900u, game.nextEffectId());
}

TEST(SavedRuntimeState, accepts_retail_zero_object_cursor_but_rejects_reserved_nonzero_cursor) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto unavailableCursor = Gff::Builder()
                                 .field(Gff::Field::newDword("Mod_NextObjId0", 0))
                                 .build();
    EXPECT_NO_THROW(game.prepareSavedRuntimeNamespace(*unavailableCursor));
    EXPECT_EQ(2u, game.newItem()->id());

    auto reservedCursor = Gff::Builder()
                              .field(Gff::Field::newDword("Mod_NextObjId0", 1))
                              .build();
    EXPECT_THROW(game.prepareSavedRuntimeNamespace(*reservedCursor), ValidationException);
}

TEST(SavedRuntimeState, accepts_retail_low_ids_but_rejects_invalid_and_duplicate_object_ids) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto reserved = Gff::Builder()
                        .field(Gff::Field::newDword("ObjectId", 1))
                        .build();
    auto invalid = Gff::Builder()
                       .field(Gff::Field::newDword("ObjectId", std::numeric_limits<uint32_t>::max()))
                       .build();
    auto saved = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 42))
                     .build();

    EXPECT_NO_THROW(game.newItem(*reserved));
    EXPECT_THROW(game.newItem(*reserved), ValidationException);
    EXPECT_THROW(game.newItem(*invalid), ValidationException);
    EXPECT_NO_THROW(game.newItem(*saved));
    EXPECT_THROW(game.newItem(*saved), ValidationException);
}

TEST(SavedRuntimeState, gives_structural_module_a_transient_runtime_identity) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto module = game.newSavedModule();
    auto area = game.newSavedArea(0);

    EXPECT_EQ(2u, module->id());
    EXPECT_EQ(0u, area->id());
    EXPECT_EQ(module, game.getObjectById(module->id()));
    EXPECT_EQ(2u, engine.gameModule().objectRegistrySize(game));
}

TEST(SavedRuntimeState, reserves_the_actual_retail_graph_before_owner_local_allocations) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto area = Gff::Builder()
                    .field(Gff::Field::newDword("ObjectId", 0))
                    .build();
    auto ifo = Gff::Builder()
                   .field(Gff::Field::newDword("Mod_NextObjId0", 0))
                   .field(Gff::Field::newList("Mod_Area_list", {area}))
                   .build();
    auto trigger = Gff::Builder()
                       .field(Gff::Field::newDword("ObjectId", 1))
                       .build();
    auto placeable = Gff::Builder()
                         .field(Gff::Field::newDword("ObjectId", 50))
                         .build();
    auto git = Gff::Builder()
                   .field(Gff::Field::newList("Trigger List", {trigger}))
                   .field(Gff::Field::newList("Placeable List", {placeable}))
                   .build();

    game.prepareSavedRuntimeNamespace(*ifo);
    game.reserveSavedObjectIds(*git);

    for (uint32_t expected = 2; expected < 50; ++expected) {
        EXPECT_EQ(expected, game.newItem()->id());
    }
    EXPECT_EQ(51u, game.newItem()->id());

    auto module = game.newSavedModule();
    auto savedArea = game.newSavedArea(0);
    auto savedTrigger = game.newTrigger(*trigger);
    auto savedPlaceable = game.newPlaceable(*placeable);

    EXPECT_EQ(52u, module->id());
    EXPECT_EQ(0u, savedArea->id());
    EXPECT_EQ(1u, savedTrigger->id());
    EXPECT_EQ(50u, savedPlaceable->id());
    EXPECT_EQ(module, game.getObjectById(module->id()));
    EXPECT_EQ(53u, engine.gameModule().objectRegistrySize(game));
    EXPECT_THROW(game.newPlaceable(*placeable), ValidationException);
}

TEST(SavedRuntimeState, restores_swvar_boolean_and_numeric_locals) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto bit0 = Gff::Builder()
                    .field(Gff::Field::newDword("Variable", 1u << 31))
                    .build();
    auto bit1 = Gff::Builder()
                    .field(Gff::Field::newDword("Variable", 1u << 2))
                    .build();
    auto byte0 = Gff::Builder()
                     .field(Gff::Field::newByte("Variable", 0))
                     .build();
    auto byte1 = Gff::Builder()
                     .field(Gff::Field::newByte("Variable", 173))
                     .build();
    auto variables = Gff::Builder()
                         .field(Gff::Field::newList("BitArray", {bit0, bit1}))
                         .field(Gff::Field::newList("ByteArray", {byte0, byte1}))
                         .build();
    auto saved = Gff::Builder()
                     .field(Gff::Field::newStruct("SWVarTable", variables))
                     .build();

    auto item = game.newItem();
    item->deserializeRuntimeState(*saved);

    EXPECT_TRUE(item->getLocalBoolean(31));
    EXPECT_TRUE(item->getLocalBoolean(34));
    EXPECT_FALSE(item->getLocalBoolean(30));
    EXPECT_EQ(173, item->getLocalNumber(1));
    EXPECT_EQ(0, item->getLocalNumber(0));
}

TEST(SavedRuntimeState, resolves_references_only_after_saved_graph_construction) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto sourceGff = Gff::Builder()
                         .field(Gff::Field::newDword("ObjectId", 80))
                         .field(Gff::Field::newDword("CreatorId", 81))
                         .field(Gff::Field::newDword("TargetId", 999))
                         .build();
    auto targetGff = Gff::Builder()
                         .field(Gff::Field::newDword("ObjectId", 81))
                         .build();
    auto source = game.newItem(*sourceGff);
    source->deserializeRuntimeState(*sourceGff);
    EXPECT_FALSE(source->savedReference("CreatorId"));

    auto target = game.newItem(*targetGff);
    target->deserializeRuntimeState(*targetGff);
    game.resolveSavedObjectReferences();

    EXPECT_EQ(target, source->savedReference("CreatorId"));
    EXPECT_FALSE(source->savedReference("TargetId"));
}

TEST(SavedRuntimeState, preserves_and_binds_saved_encounter_runtime_state) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);

    auto areaObject = Gff::Builder()
                          .field(Gff::Field::newDword("AreaObject", 90))
                          .build();
    auto saved = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 89))
                     .field(Gff::Field::newInt("AreaListMaxSize", 8))
                     .field(Gff::Field::newInt("AreaListSize", 1))
                     .field(Gff::Field::newList("AreaList", {areaObject}))
                     .field(Gff::Field::newFloat("AreaPoints", 3.5f))
                     .field(Gff::Field::newInt("CurrentSpawns", 2))
                     .field(Gff::Field::newInt("CustomScriptId", 17))
                     .field(Gff::Field::newByte("Exhausted", 1))
                     .field(Gff::Field::newDword("HeartbeatDay", 4))
                     .field(Gff::Field::newDword("HeartbeatTime", 5))
                     .field(Gff::Field::newDword("LastEntered", 6))
                     .field(Gff::Field::newDword("LastLeft", 7))
                     .field(Gff::Field::newDword("LastSpawnDay", 8))
                     .field(Gff::Field::newDword("LastSpawnTime", 9))
                     .field(Gff::Field::newInt("NumberSpawned", 10))
                     .field(Gff::Field::newFloat("SpawnPoolActive", 11.5f))
                     .field(Gff::Field::newByte("Started", 1))
                     .build();

    auto encounter = game.newEncounter(*saved);
    encounter->deserialize(*saved);
    auto targetGff = Gff::Builder()
                         .field(Gff::Field::newDword("ObjectId", 90))
                         .build();
    auto target = game.newItem(*targetGff);
    game.resolveSavedObjectReferences();

    const auto &state = encounter->savedRuntimeState();
    EXPECT_EQ(8, state.areaListMaxSize);
    EXPECT_EQ(1, state.areaListSize);
    EXPECT_EQ(std::vector<uint32_t>({90}), state.areaObjectIds);
    EXPECT_FLOAT_EQ(3.5f, state.areaPoints);
    EXPECT_EQ(2, state.currentSpawns);
    EXPECT_EQ(17, state.customScriptId);
    EXPECT_TRUE(state.exhausted);
    EXPECT_EQ(4u, state.heartbeatDay);
    EXPECT_EQ(5u, state.heartbeatTime);
    EXPECT_EQ(6u, state.lastEntered);
    EXPECT_EQ(7u, state.lastLeft);
    EXPECT_EQ(8u, state.lastSpawnDay);
    EXPECT_EQ(9u, state.lastSpawnTime);
    EXPECT_EQ(10, state.numberSpawned);
    EXPECT_FLOAT_EQ(11.5f, state.spawnPoolActive);
    EXPECT_TRUE(state.started);
    EXPECT_EQ(target, encounter->savedAreaObject(0));
}

TEST(SavedRuntimeState, restores_saved_creature_death_from_current_hit_points) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto saved = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 82))
                     .field(Gff::Field::newShort("CurrentHitPoints", 0))
                     .field(Gff::Field::newDword("Appearance_Type", 0))
                     .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                     .field(Gff::Field::newByte("BodyBag", 0xff))
                     .field(Gff::Field::newByte("PerceptionRange", 0xff))
                     .build();
    auto creature = game.newCreature(*saved);
    creature->deserialize(*saved);

    EXPECT_EQ(0, creature->currentHitPoints());
    EXPECT_TRUE(creature->isDead());
}

TEST(SavedRuntimeState, restores_retail_player_death_threshold_without_clamping_saved_hp) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto makeSavedPlayer = [](int16_t hitPoints, uint32_t objectId) {
        return Gff::Builder()
            .field(Gff::Field::newDword("ObjectId", objectId))
            .field(Gff::Field::newByte("IsPC", 1))
            .field(Gff::Field::newShort("CurrentHitPoints", hitPoints))
            .field(Gff::Field::newDword("Appearance_Type", 0))
            .field(Gff::Field::newWord("SoundSetFile", 0xffff))
            .field(Gff::Field::newByte("BodyBag", 0xff))
            .field(Gff::Field::newByte("PerceptionRange", 0xff))
            .build();
    };

    auto incapacitatedState = makeSavedPlayer(0, 0x7fffffff);
    auto incapacitated = game.newCreature(*incapacitatedState);
    incapacitated->deserialize(*incapacitatedState);
    EXPECT_EQ(0, incapacitated->currentHitPoints());
    EXPECT_TRUE(incapacitated->isPC());
    EXPECT_FALSE(incapacitated->isDead());

    incapacitated->setMaxHitPoints(36);
    incapacitated->restorePrimaryPlayerHitPoints();
    EXPECT_EQ(36, incapacitated->currentHitPoints());
    EXPECT_FALSE(incapacitated->isDead());

    auto deadState = makeSavedPlayer(-10, 0x7ffffffe);
    auto dead = game.newCreature(*deadState);
    dead->deserialize(*deadState);
    EXPECT_EQ(-10, dead->currentHitPoints());
    EXPECT_TRUE(dead->isDead());
}


TEST(SavedRuntimeState, k1_zero_hp_pc_is_dead_until_primary_player_publication) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto saved = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 0x7ffffffe))
                     .field(Gff::Field::newByte("IsPC", 1))
                     .field(Gff::Field::newShort("MaxHitPoints", 12))
                     .field(Gff::Field::newShort("CurrentHitPoints", 0))
                     .field(Gff::Field::newDword("Appearance_Type", 0))
                     .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                     .field(Gff::Field::newByte("BodyBag", 0xff))
                     .field(Gff::Field::newByte("PerceptionRange", 0xff))
                     .build();
    auto player = game.newCreature(*saved);
    player->deserialize(*saved);

    // K1 does not share K2's -10 incapacitation threshold. The saved creature
    // remains exact until the coordinator identifies it as the primary player.
    EXPECT_EQ(0, player->currentHitPoints());
    EXPECT_TRUE(player->isDead());

    player->restorePrimaryPlayerHitPoints();

    EXPECT_EQ(12, player->currentHitPoints());
    EXPECT_FALSE(player->isDead());
}
TEST(SavedRuntimeState, keeps_min_one_hp_creature_alive_when_saved_at_zero) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto saved = Gff::Builder()
                     .field(Gff::Field::newDword("ObjectId", 83))
                     .field(Gff::Field::newShort("CurrentHitPoints", 0))
                     .field(Gff::Field::newByte("Min1HP", 1))
                     .field(Gff::Field::newDword("Appearance_Type", 0))
                     .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                     .field(Gff::Field::newByte("BodyBag", 0xff))
                     .field(Gff::Field::newByte("PerceptionRange", 0xff))
                     .build();
    auto creature = game.newCreature(*saved);
    creature->deserialize(*saved);

    EXPECT_EQ(1, creature->currentHitPoints());
    EXPECT_FALSE(creature->isDead());
}

TEST(SavedRuntimeState, detached_creatures_keep_nested_item_ids_owner_scoped) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto savedItem = Gff::Builder()
                         .type(1u << InventorySlots::body)
                         .field(Gff::Field::newDword("ObjectId", 218))
                         .field(Gff::Field::newInt("BaseItem", 2))
                         .field(Gff::Field::newList("PropertiesList", {}))
                         .build();
    auto detachedCreature = Gff::Builder()
                                .field(Gff::Field::newDword("Appearance_Type", 0))
                                .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                                .field(Gff::Field::newByte("BodyBag", 0xff))
                                .field(Gff::Field::newByte("PerceptionRange", 0xff))
                                .field(Gff::Field::newList("Equip_ItemList", {savedItem}))
                                .build();

    auto first = game.newCreature();
    first->deserialize(*detachedCreature);
    auto second = game.newCreature();
    EXPECT_NO_THROW(second->deserialize(*detachedCreature));

    auto firstItem = first->getEquippedItem(InventorySlots::body);
    auto secondItem = second->getEquippedItem(InventorySlots::body);
    ASSERT_TRUE(firstItem);
    ASSERT_TRUE(secondItem);
    EXPECT_NE(218u, firstItem->id());
    EXPECT_NE(218u, secondItem->id());
    EXPECT_NE(firstItem->id(), secondItem->id());
    EXPECT_FALSE(game.getObjectById(218));
}


TEST(SavedRuntimeState, primary_health_publication_does_not_recover_unrelated_pc) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::TSL, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto makePlayer = [](uint32_t id, int16_t maxHitPoints, bool primary) {
        return Gff::Builder()
            .field(Gff::Field::newDword("ObjectId", id))
            .field(Gff::Field::newByte("IsPC", 1))
            .field(Gff::Field::newByte("Mod_IsPrimaryPlr", primary))
            .field(Gff::Field::newShort("MaxHitPoints", maxHitPoints))
            .field(Gff::Field::newShort("CurrentHitPoints", 0))
            .field(Gff::Field::newDword("Appearance_Type", 0))
            .field(Gff::Field::newWord("SoundSetFile", 0xffff))
            .field(Gff::Field::newByte("BodyBag", 0xff))
            .field(Gff::Field::newByte("PerceptionRange", 0xff))
            .build();
    };

    auto modulePlayer = makePlayer(0x7ffffffe, 20, false);
    auto ifo = Gff::Builder()
                   .field(Gff::Field::newList("Mod_PlayerList", {modulePlayer}))
                   .build();
    auto pc = makePlayer(0x7fffffff, 36, true);
    auto partyTable = Gff::Builder()
                          .field(Gff::Field::newInt("PT_CONTROLLED_NP", 0))
                          .field(Gff::Field::newByte("PT_NUM_MEMBERS", 0))
                          .field(Gff::Field::newList("PT_MEMBERS", {}))
                          .build();

    TestGameModule::publishPartyRuntimeState(game, *ifo, partyTable, pc);

    auto moduleRuntime = game.party().player();
    auto actual = game.party().actualPlayer();
    ASSERT_TRUE(moduleRuntime);
    ASSERT_TRUE(actual);
    EXPECT_NE(moduleRuntime, actual);
    EXPECT_EQ(0, moduleRuntime->currentHitPoints());
    EXPECT_EQ(36, actual->currentHitPoints());
    EXPECT_FALSE(actual->isDead());
}
TEST(SavedRuntimeState, keeps_authoritative_owner_items_local_to_their_owner_namespace) {
    TestEngine engine;
    engine.init();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    EXPECT_CALL(engine.resourceModule().twoDas(), get("appearance"))
        .WillRepeatedly(Return(makeAppearanceTable()));
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
        .WillRepeatedly(Return(makeBaseItemsTable()));
    EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
    EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
    EXPECT_CALL(static_cast<MockPortraits &>(engine.services().game.portraits), getTextureByAppearance(_))
        .Times(AnyNumber());

    auto savedLongsword = Gff::Builder()
                              .type(1u << InventorySlots::body)
                              .field(Gff::Field::newDword("ObjectId", 15))
                              .field(Gff::Field::newCExoString("Tag", "g_w_lngswrd01"))
                              .field(Gff::Field::newInt("BaseItem", 2))
                              .field(Gff::Field::newWord("StackSize", 2))
                              .field(Gff::Field::newList("PropertiesList", {}))
                              .build();
    auto savedPlayer = Gff::Builder()
                           .field(Gff::Field::newDword("ObjectId", 2147483646u))
                           .field(Gff::Field::newDword("Appearance_Type", 0))
                           .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                           .field(Gff::Field::newByte("BodyBag", 0xff))
                           .field(Gff::Field::newByte("PerceptionRange", 0xff))
                           .field(Gff::Field::newList("Equip_ItemList", {savedLongsword}))
                           .build();
    auto ifo = Gff::Builder()
                   .field(Gff::Field::newList("Mod_PlayerList", {savedPlayer}))
                   .build();

    TestGameModule::publishPartyRuntimeState(game, *ifo, nullptr, nullptr);

    auto player = game.party().actualPlayer();
    ASSERT_TRUE(player);
    auto equipped = player->getEquippedItem(InventorySlots::body);
    ASSERT_TRUE(equipped);
    EXPECT_NE(15u, equipped->id());
    EXPECT_FALSE(game.getObjectById(15));

    auto inventory = Gff::Builder()
                         .field(Gff::Field::newList("ItemList", {savedLongsword}))
                         .build();
    TestGameModule::deserializeInventory(game, *inventory);

    ASSERT_EQ(1, player->items().size());
    auto carried = player->items().front();
    EXPECT_NE(15u, carried->id());
    EXPECT_NE(equipped->id(), carried->id());
    EXPECT_FALSE(game.getObjectById(15));

    auto savedWorldItem = Gff::Builder()
                              .field(Gff::Field::newDword("ObjectId", 15))
                              .build();
    auto worldItem = game.newItem(*savedWorldItem);
    EXPECT_EQ(15u, worldItem->id());
    EXPECT_EQ(worldItem, game.getObjectById(15));
    EXPECT_THROW(game.newItem(*savedWorldItem), ValidationException);
}

namespace {

// KotOR II routine numbers, as the shipped scripts encode them.
constexpr int kRemoveHeartbeatRoutine = 866;
constexpr int kSetLocalBooleanRoutine = 680;

// An arbitrary local slot the heartbeat script writes to so the test can see
// how far the script got.
constexpr int kReachedEndFlag = 7;

std::map<std::string, int> &heartbeatDispatches() {
    static std::map<std::string, int> counts;
    return counts;
}

std::shared_ptr<TwoDA> makePlaceablesTable() {
    TwoDA::Builder builder;
    builder.columns({"modelname"});
    // No model is resolvable for this row, so appearance loading stops before
    // it needs a scene node.
    builder.row({"plc_footlker"});
    return std::shared_ptr<TwoDA>(builder.build());
}

// A placeable in the shape the game loads one: a UTP names its heartbeat in
// OnHeartbeat, not in the base object's ScriptHeartbeat field.
std::shared_ptr<Gff> makeHeartbeatPlaceableGff(
    std::string tag,
    std::string onHeartbeat,
    std::string onUsed = "") {

    auto builder = Gff::Builder();
    builder.field(Gff::Field::newCExoString("Tag", std::move(tag)));
    builder.field(Gff::Field::newDword("Appearance", 0));
    if (!onHeartbeat.empty()) {
        builder.field(Gff::Field::newResRef("OnHeartbeat", std::move(onHeartbeat)));
    }
    if (!onUsed.empty()) {
        builder.field(Gff::Field::newResRef("OnUsed", std::move(onUsed)));
    }
    return builder.build();
}

// The shape the shipped k_plc_tres* heartbeats use: do the one-off work, then
// hand OBJECT_SELF to RemoveHeartbeat. Arguments are pushed so that argument 0
// ends up on top of the stack.
std::shared_ptr<script::ScriptProgram> makeSelfRemovingHeartbeat(const std::string &resRef) {
    auto program = std::make_shared<script::ScriptProgram>(resRef);
    program->add(script::Instruction::newCONSTO(script::kObjectSelf));
    program->add(script::Instruction::newACTION(kRemoveHeartbeatRoutine, 1));
    // Executed after the removal, so a heartbeat that removes itself is still
    // seen through to the end.
    program->add(script::Instruction::newCONSTI(1));
    program->add(script::Instruction::newCONSTI(kReachedEndFlag));
    program->add(script::Instruction::newCONSTO(script::kObjectSelf));
    program->add(script::Instruction::newACTION(kSetLocalBooleanRoutine, 3));
    program->add(script::Instruction(script::InstructionType::RETN));
    return program;
}

// A heartbeat that does nothing, for the objects a test only needs to keep
// ticking.
std::shared_ptr<script::ScriptProgram> makeInertScript(const std::string &resRef) {
    auto program = std::make_shared<script::ScriptProgram>(resRef);
    program->add(script::Instruction(script::InstructionType::RETN));
    return program;
}

// Serve a program for resRef and count how many times it is dispatched. Area
// heartbeat dispatch fetches the program once per run, which is what makes the
// fetch count the dispatch count.
void serveScript(
    TestEngine &engine,
    const std::string &resRef,
    std::shared_ptr<script::ScriptProgram> program) {

    heartbeatDispatches()[resRef] = 0;
    EXPECT_CALL(engine.resourceModule().scripts(), get(resRef))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([program](const std::string &key) {
            ++heartbeatDispatches()[key];
            return program;
        }));
}

int dispatchesOf(const std::string &resRef) {
    return heartbeatDispatches()[resRef];
}

struct HeartbeatFixture {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game {GameID::TSL, "", engine.options(), engine.services(), console};
    std::shared_ptr<Area> area;

    HeartbeatFixture() {
        testSceneGraph(engine);
        EXPECT_CALL(engine.resourceModule().twoDas(), get("placeables"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makePlaceablesTable()));
        // Appearance loading looks the model up and stops when there is none;
        // these tests never need a scene node.
        EXPECT_CALL(engine.resourceModule().models(), get("plc_footlker"))
            .Times(AnyNumber());
        // Heartbeat dispatch runs scripts through the game's own script runner.
        game.initLocalServices();
        area = game.newArea();
        TestGameModule::setActiveModuleArea(game, area);
    }

    std::shared_ptr<Placeable> addPlaceable(
        std::string tag,
        std::string onHeartbeat,
        std::string onUsed = "") {

        auto placeable = game.newPlaceable();
        placeable->deserialize(*makeHeartbeatPlaceableGff(
            std::move(tag), std::move(onHeartbeat), std::move(onUsed)));
        area->add(placeable);
        return placeable;
    }

    // Advance the area far enough to cross one heartbeat interval.
    void tickHeartbeat(int times = 1) {
        for (int i = 0; i < times; ++i) {
            area->update(kHeartbeatInterval);
        }
    }
};

} // namespace

// A. The shipped pattern: a placeable's heartbeat removes its own heartbeat
// script. It runs once, and the area's heartbeat cadence never runs it again,
// however long the object stays around.
TEST(RemoveHeartbeat, should_stop_further_heartbeats_on_the_object_that_removed_its_own) {
    HeartbeatFixture fixture;
    serveScript(fixture.engine, "k_plc_treasure", makeSelfRemovingHeartbeat("k_plc_treasure"));
    auto placeable = fixture.addPlaceable("treasure", "k_plc_treasure");
    ASSERT_EQ("k_plc_treasure", placeable->getOnHeartbeat());

    fixture.tickHeartbeat();

    // It ran, and it ran all the way through: the routine after RemoveHeartbeat
    // still took effect, so removal does not abort the heartbeat in progress.
    EXPECT_EQ(1, dispatchesOf("k_plc_treasure"));
    EXPECT_TRUE(placeable->getLocalBoolean(kReachedEndFlag));

    // The slot is empty, which is what takes the object out of dispatch.
    EXPECT_TRUE(placeable->getOnHeartbeat().empty());

    // Removal holds for the rest of the object's life, not just one interval.
    fixture.tickHeartbeat(5);

    EXPECT_EQ(1, dispatchesOf("k_plc_treasure"));
}

// B. Only the heartbeat slot is cleared. The same object's other event scripts
// are untouched and still dispatch - shipped treasure placeables carry OnUsed,
// OnOpen and OnMeleeAttacked scripts alongside the heartbeat they remove.
TEST(RemoveHeartbeat, should_leave_the_other_event_scripts_on_the_object_alone) {
    HeartbeatFixture fixture;
    serveScript(fixture.engine, "k_plc_hb", makeSelfRemovingHeartbeat("k_plc_hb"));
    serveScript(fixture.engine, "k_plc_used", makeInertScript("k_plc_used"));
    auto placeable = fixture.addPlaceable("treasure", "k_plc_hb", "k_plc_used");

    fixture.tickHeartbeat();
    ASSERT_TRUE(placeable->getOnHeartbeat().empty());

    placeable->runOnUsed(nullptr);

    EXPECT_EQ(1, dispatchesOf("k_plc_used"));
}

// C. Removal is per object. A second placeable running the very same heartbeat
// script keeps being dispatched after the first one removes its own.
TEST(RemoveHeartbeat, should_not_disturb_the_heartbeat_of_another_object) {
    HeartbeatFixture fixture;
    serveScript(fixture.engine, "k_plc_selfrem", makeSelfRemovingHeartbeat("k_plc_selfrem"));
    serveScript(fixture.engine, "k_plc_keeps", makeInertScript("k_plc_keeps"));
    auto remover = fixture.addPlaceable("remover", "k_plc_selfrem");
    auto bystander = fixture.addPlaceable("bystander", "k_plc_keeps");

    fixture.tickHeartbeat(3);

    EXPECT_EQ(1, dispatchesOf("k_plc_selfrem"));
    EXPECT_TRUE(remover->getOnHeartbeat().empty());

    // The bystander ran on every interval and still has its script.
    EXPECT_EQ(3, dispatchesOf("k_plc_keeps"));
    EXPECT_EQ("k_plc_keeps", bystander->getOnHeartbeat());
}

// D. Routine 866 is registered for KotOR II and tolerates an object that has no
// heartbeat script to begin with.
TEST(RemoveHeartbeat, routine_866_is_a_no_op_on_an_object_without_a_heartbeat) {
    HeartbeatFixture fixture;
    Routines routines(GameID::TSL, &fixture.game, &fixture.engine.services());
    routines.init();
    script::Routine &routine = routines.get(kRemoveHeartbeatRoutine);
    ASSERT_EQ("RemoveHeartbeat", routine.name());

    auto placeable = fixture.addPlaceable("plain", "");
    ASSERT_TRUE(placeable->getOnHeartbeat().empty());

    script::ExecutionContext ctx;
    auto result = routine.invoke({script::Variable::ofObject(placeable->id())}, ctx);

    EXPECT_EQ(script::VariableType::Void, result.type);
    EXPECT_TRUE(placeable->getOnHeartbeat().empty());
}

// E. The routine belongs to KotOR II only - it is absent from the KotOR I
// nwscript, and its table stops short of 866 entirely.
TEST(RemoveHeartbeat, is_not_registered_for_kotor_one) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines k1(GameID::KotOR, &game, &engine.services());
    k1.init();
    EXPECT_THROW(k1.get(kRemoveHeartbeatRoutine), std::out_of_range);

    Routines k2(GameID::TSL, &game, &engine.services());
    k2.init();
    EXPECT_EQ("RemoveHeartbeat", k2.get(kRemoveHeartbeatRoutine).name());
}


namespace {

// Door action routine numbers, registered for both games.
constexpr int kActionOpenDoor = 43;
constexpr int kActionCloseDoor = 44;

// Invoke a door action routine the way a script does: by number, with the
// caller the engine would have supplied.
script::Variable invokeDoorRoutine(
    Routines &routines,
    int routine,
    const std::shared_ptr<Object> &caller,
    const std::shared_ptr<Object> &target) {

    script::ExecutionContext ctx;
    ctx.args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(caller->id()));
    return routines.get(routine).invoke({script::Variable::ofObject(target->id())}, ctx);
}

struct DoorTargetFixture {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game {GameID::KotOR, "", engine.options(), engine.services(), console};
    Routines routines {GameID::KotOR, &game, &engine.services()};

    DoorTargetFixture() {
        testSceneGraph(engine);
        routines.init();
    }
};

} // namespace

// A non-door target is rejected by argument validation, so nothing is queued on
// the caller. ActionCloseDoor takes a generic object in nwscript, so a script
// can name anything at all here.
TEST(CloseDoorTarget, should_reject_a_non_door_target) {
    DoorTargetFixture fixture;
    auto caller = makeMovingCreature(fixture.game, fixture.engine);
    auto notADoor = fixture.game.newPlaceable();

    auto result = invokeDoorRoutine(fixture.routines, kActionCloseDoor, caller, notADoor);

    EXPECT_EQ(script::VariableType::Void, result.type);
    EXPECT_TRUE(caller->actions().empty());
}

// The same rejection for a caller that is not a creature. Without validation
// this is the path that skipped the approach and went straight to closing the
// target, so it has to be covered separately from the creature case.
TEST(CloseDoorTarget, should_reject_a_non_door_target_for_a_non_creature_caller) {
    DoorTargetFixture fixture;
    auto caller = fixture.game.newPlaceable();
    auto notADoor = fixture.game.newPlaceable();

    invokeDoorRoutine(fixture.routines, kActionCloseDoor, caller, notADoor);

    EXPECT_TRUE(caller->actions().empty());
}

// A real door is queued and closed as before.
TEST(CloseDoorTarget, should_close_a_door_the_actor_has_reached) {
    DoorTargetFixture fixture;
    auto door = makePlainDoor(fixture.game, fixture.engine);
    auto caller = makeMovingCreature(fixture.game, fixture.engine);
    door->open();
    ASSERT_TRUE(door->isOpen());

    invokeDoorRoutine(fixture.routines, kActionCloseDoor, caller, door);
    ASSERT_EQ(1u, caller->actions().size());
    auto action = caller->actions().front();

    action->execute(action, *caller, 1.0f);

    EXPECT_TRUE(action->isCompleted());
    EXPECT_FALSE(door->isOpen());
    EXPECT_EQ(DoorState::Closed, door->state());
}

// Out of range the action stays in progress and the door is left alone, which
// is what separates an honored action from a dropped one.
TEST(CloseDoorTarget, should_keep_approaching_a_door_that_is_out_of_reach) {
    DoorTargetFixture fixture;
    auto door = makePlainDoor(fixture.game, fixture.engine, /*locked=*/false, /*onOpen=*/"",
                              glm::vec3(50.0f, 0.0f, 0.0f));
    auto caller = makeMovingCreature(fixture.game, fixture.engine);
    caller->setMovementRestricted(true);
    door->open();

    invokeDoorRoutine(fixture.routines, kActionCloseDoor, caller, door);
    ASSERT_EQ(1u, caller->actions().size());
    auto action = caller->actions().front();

    action->execute(action, *caller, 1.0f);

    EXPECT_FALSE(action->isCompleted());
    EXPECT_TRUE(door->isOpen());
}

// Both door routines apply the same validation to the same bad target.
TEST(CloseDoorTarget, opens_and_closes_reject_a_non_door_target_alike) {
    DoorTargetFixture fixture;
    auto caller = makeMovingCreature(fixture.game, fixture.engine);
    auto notADoor = fixture.game.newPlaceable();

    invokeDoorRoutine(fixture.routines, kActionOpenDoor, caller, notADoor);
    EXPECT_TRUE(caller->actions().empty());

    invokeDoorRoutine(fixture.routines, kActionCloseDoor, caller, notADoor);
    EXPECT_TRUE(caller->actions().empty());
}


void reone::game::TestGameModule::stopMovement(Game &game) {
    game.stopMovement();
}

void reone::game::TestGameModule::loadModulePlayer(Module &module) {
    module.loadPlayer();
}

namespace {

// A module with the area, cameras and player a loaded one has, which is what
// the movement stop actually reaches.
struct StopMovementFixture {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game {GameID::KotOR, "", engine.options(), engine.services(), console};

    StopMovementFixture() {
        testSceneGraph(engine);
    }

    std::shared_ptr<Creature> bringUpModule() {
        auto area = game.newArea();
        area->initCameras(glm::vec3(0.0f), 0.0f);
        TestGameModule::setActiveModuleArea(game, area);
        TestGameModule::loadModulePlayer(*game.module());

        auto leader = makeMovingCreature(game, engine);
        game.party().addMember(kNpcPlayer, leader);
        game.party().setPlayer(leader);
        area->add(leader);
        return leader;
    }
};

} // namespace

// Between resetting the game and the destination module coming up there is no
// module to stop anything on. Nothing is halted because nothing is moving, and
// no state is conjured to stand in for the absent module.
TEST(StopMovement, should_do_nothing_without_a_module) {
    StopMovementFixture fixture;
    ASSERT_FALSE(fixture.game.module());

    TestGameModule::stopMovement(fixture.game);

    EXPECT_FALSE(fixture.game.module());
    EXPECT_TRUE(fixture.game.party().isEmpty());
}

// With a module the player is still halted, which is the whole point of the
// call: a player holding a movement key is left standing.
TEST(StopMovement, should_halt_the_player_with_a_module) {
    StopMovementFixture fixture;
    fixture.bringUpModule();
    Player &player = fixture.game.module()->player();

    ASSERT_TRUE(player.handle(input::Event::newKeyDown(
        input::KeyEvent(true, input::KeyCode::W, 0, false))));
    ASSERT_TRUE(player.isMovementRequested());

    TestGameModule::stopMovement(fixture.game);

    EXPECT_FALSE(player.isMovementRequested());
}
