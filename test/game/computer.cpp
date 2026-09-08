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

#include "../fixtures/engine.h"

#include "reone/game/console.h"
#include "reone/game/game.h"
#include "reone/game/gui/computer.h"

using namespace reone;
using namespace reone::game;
using namespace reone::gui;
using namespace reone::resource;
using namespace testing;

namespace {

class ComputerConsole : public IConsole {
public:
    void registerCommand(std::string, std::string, CommandHandler) override {}
    void printLine(const std::string &) override {}
};

class TestComputer : public ComputerGUI {
public:
    using ComputerGUI::ComputerGUI;

    int loads {0};
    int ends {0};
    int finishes {0};
    int replyRefreshes {0};
    std::vector<std::string> replies;
    const Dialog::EntryReply *entry() const { return _currentEntry; }
    std::shared_ptr<Object> resolvedOwner() const { return owner(); }

protected:
    // Exercise real Conversation transitions and GUI delegation without
    // building the normal terminal's list controls.
    void onGUILoaded() override {}
    void setMessage(std::string) override {}
    void setReplyLines(std::vector<std::string> lines) override {
        replies = std::move(lines);
        ++replyRefreshes;
    }
    void onLoadEntry() override {
        ComputerGUI::onLoadEntry();
        ++loads;
    }
    void onEntryEnded() override {
        ComputerGUI::onEntryEnded();
        ++ends;
    }
    void onFinish() override {
        ComputerGUI::onFinish();
        ++finishes;
    }
};

std::shared_ptr<Dialog> computerDialog(bool camera = true, bool automatic = false) {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "computer_test";
    dialog->conversationType = ConversationType::Computer;
    dialog->startEntries.push_back({});
    dialog->entries.resize(2);
    dialog->replies.resize(3);
    auto &first = dialog->entries[0];
    first.text = "Camera menu";
    first.cameraId = camera ? 1 : 0;
    first.delay = 2;
    first.replies.push_back({});
    if (!automatic) {
        Dialog::EntryReplyLink secondReply;
        secondReply.index = 1;
        first.replies.push_back(secondReply);
        dialog->replies[0].text = "Mess Hall";
        dialog->replies[1].text = "Log out";
    }
    Dialog::EntryReplyLink nextEntry;
    nextEntry.index = 1;
    dialog->replies[0].entries.push_back(nextEntry);
    auto &next = dialog->entries[1];
    next.text = "Main console";
    next.cameraId = 0;
    next.delay = 100;
    Dialog::EntryReplyLink lastReply;
    lastReply.index = 2;
    next.replies.push_back(lastReply);
    dialog->replies[2].text = "Log out";
    return dialog;
}

input::Event key(input::KeyCode code) {
    return input::Event::newKeyUp({false, code, 0, false});
}

class ComputerGUITest : public TestWithParam<GameID> {
protected:
    void SetUp() override {
        engine.init();
        game = std::make_unique<Game>(GetParam(), std::filesystem::path {}, engine.options(), engine.services(), console);
        game->initLocalServices();
        normal = std::make_shared<NiceMock<MockGUI>>();
        camera = std::make_shared<NiceMock<MockGUI>>();
        auto &svc = engine.services();
        returnControl = std::make_shared<Label>(*camera, svc.scene.graphs, svc.graphics, svc.resource);
        EXPECT_CALL(*camera, findControl("LBL_RETURN")).WillOnce(Return(returnControl));
        EXPECT_CALL(*camera, setBackground(_)).Times(0);
        EXPECT_CALL(engine.guiModule().guis(), get(GetParam() == GameID::KotOR ? "computer" : "computer_p", _))
            .WillOnce(Return(normal));
        EXPECT_CALL(engine.guiModule().guis(), get(GetParam() == GameID::KotOR ? "computercamera" : "computercam_p", _))
            .WillOnce(Invoke([this](const std::string &, std::function<void(IGUI &)> preload) {
                InSequence sequence;
                if (GetParam() == GameID::TSL) {
                    EXPECT_CALL(*camera, setResolution(800, 600));
                }
                EXPECT_CALL(*camera, setResolution(640, 480));
                preload(*camera);
                return camera;
            }));
        computer = std::make_unique<TestComputer>(*game, svc);
        computer->init();
    }

    void expectRender(bool cameraActive) {
        EXPECT_CALL(*normal, render()).Times(cameraActive ? 0 : 1);
        EXPECT_CALL(*camera, render()).Times(cameraActive ? 1 : 0);
        computer->render();
        Mock::VerifyAndClearExpectations(normal.get());
        Mock::VerifyAndClearExpectations(camera.get());
    }

    TestEngine engine;
    ComputerConsole console;
    std::unique_ptr<Game> game;
    std::shared_ptr<NiceMock<MockGUI>> normal;
    std::shared_ptr<NiceMock<MockGUI>> camera;
    std::shared_ptr<Label> returnControl;
    std::unique_ptr<TestComputer> computer;
};

TEST_P(ComputerGUITest, normal_entry_renders_terminal_and_handles_normal_input) {
    expectRender(false);
    computer->start(computerDialog(false), nullptr);
    expectRender(false);
    EXPECT_CALL(*normal, handle(_)).WillOnce(Return(true));
    EXPECT_CALL(*camera, handle(_)).Times(0);
    EXPECT_TRUE(computer->handle(key(input::KeyCode::Tab)));
}

TEST_P(ComputerGUITest, static_entry_renders_only_camera_and_isolates_input) {
    computer->start(computerDialog(), nullptr);
    expectRender(true);
    EXPECT_CALL(*normal, handle(_)).Times(0);
    EXPECT_CALL(*camera, handle(_)).Times(2).WillRepeatedly(Return(false));
    computer->handle(input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, 100, 100}));
    computer->handle(key(input::KeyCode::Key1));
    EXPECT_EQ(computer->loads, 1);
    EXPECT_EQ(computer->ends, 0);
}

TEST_P(ComputerGUITest, animated_camera_takes_precedence_over_static_id) {
    auto dialog = computerDialog();
    dialog->cameraModel = "camera_model";
    computer->start(dialog, nullptr);
    int id = 0;
    EXPECT_EQ(computer->getCamera(id), CameraType::Animated);
    expectRender(false);
}

TEST_P(ComputerGUITest, return_auto_selects_empty_reply_once_without_replaying_scripts) {
    auto dialog = computerDialog(true, true);
    dialog->entries[0].script = "camera_entry";
    dialog->replies[0].script = "empty_reply";
    dialog->entries[1].script = "next_entry";
    auto &scripts = engine.resourceModule().scripts();
    EXPECT_CALL(scripts, get("camera_entry")).WillOnce(Return(nullptr));
    EXPECT_CALL(scripts, get("empty_reply")).WillOnce(Return(nullptr));
    EXPECT_CALL(scripts, get("next_entry")).WillOnce(Return(nullptr));

    computer->start(dialog, nullptr);
    EXPECT_TRUE(computer->handle(key(input::KeyCode::Escape)));
    EXPECT_EQ(computer->entry(), &dialog->entries[1]);
    EXPECT_EQ(computer->ends, 1);
    EXPECT_EQ(computer->loads, 2);
    expectRender(false);

    // A queued duplicate click on the old camera control cannot advance again.
    returnControl->handleClick(0, 0, 1);
    computer->update(3.0f);
    EXPECT_EQ(computer->ends, 1);
    EXPECT_EQ(computer->loads, 2);
}

TEST_P(ComputerGUITest, return_preserves_entry_and_existing_multiple_replies) {
    auto dialog = computerDialog();
    computer->start(dialog, nullptr);
    auto replies = computer->replies;
    EXPECT_TRUE(returnControl->isSelectable());
    returnControl->handleClick(0, 0, 1);
    EXPECT_EQ(computer->entry(), &dialog->entries[0]);
    EXPECT_EQ(computer->replies, replies);
    EXPECT_EQ(computer->replyRefreshes, 1);
    EXPECT_EQ(computer->loads, 1);
    EXPECT_EQ(computer->ends, 1);
    expectRender(false);
    computer->update(3.0f);
    EXPECT_EQ(computer->ends, 1);
    computer->handle(key(input::KeyCode::Key1));
    EXPECT_EQ(computer->entry(), &dialog->entries[1]);
    EXPECT_EQ(computer->loads, 2);
}

TEST_P(ComputerGUITest, conversation_timer_expires_camera_with_automatic_reply) {
    auto dialog = computerDialog(true, true);
    computer->start(dialog, nullptr);
    computer->update(1.0f);
    expectRender(true);
    computer->update(1.0f);
    expectRender(false);
    EXPECT_EQ(computer->entry(), &dialog->entries[1]);
    EXPECT_EQ(computer->ends, 1);
    EXPECT_EQ(computer->loads, 2);
}

TEST_P(ComputerGUITest, conversation_timer_expires_camera_with_multiple_replies) {
    auto dialog = computerDialog();
    computer->start(dialog, nullptr);
    computer->update(2.0f);
    expectRender(false);
    EXPECT_EQ(computer->entry(), &dialog->entries[0]);
    EXPECT_EQ(computer->replies.size(), 2u);
    EXPECT_EQ(computer->replyRefreshes, 1);
    EXPECT_EQ(computer->ends, 1);
    computer->update(20.0f);
    EXPECT_EQ(computer->ends, 1);
}

TEST_P(ComputerGUITest, consecutive_camera_entries_keep_the_next_feed_visible) {
    auto dialog = computerDialog(true, true);
    dialog->entries[1].cameraId = 3;
    computer->start(dialog, nullptr);
    computer->handle(key(input::KeyCode::Return));
    EXPECT_EQ(computer->entry(), &dialog->entries[1]);
    EXPECT_EQ(computer->loads, 2);
    EXPECT_EQ(computer->ends, 1);
    expectRender(true);
    computer->handle(key(input::KeyCode::Return));
    EXPECT_EQ(computer->ends, 2);
    EXPECT_EQ(computer->replies.size(), 1u);
    expectRender(false);
}

TEST_P(ComputerGUITest, paused_entry_cannot_be_completed_by_return_or_timeout) {
    computer->start(computerDialog(), nullptr);
    computer->pause();
    computer->handle(key(input::KeyCode::Return));
    computer->update(3.0f);
    expectRender(true);
    EXPECT_EQ(computer->ends, 0);
    computer->resume();
    computer->update(0.0f);
    expectRender(false);
    EXPECT_EQ(computer->ends, 1);
}

TEST_P(ComputerGUITest, finish_without_replies_clears_camera) {
    auto dialog = computerDialog();
    dialog->entries[0].replies.clear();
    computer->start(dialog, nullptr);
    expectRender(true);
    computer->handle(key(input::KeyCode::Return));
    expectRender(false);
    EXPECT_EQ(computer->finishes, 1);
    computer->update(3.0f);
    EXPECT_EQ(computer->finishes, 1);
}

TEST_P(ComputerGUITest, replacement_and_module_cleanup_clear_camera) {
    computer->start(computerDialog(), nullptr);
    expectRender(true);
    computer->start(computerDialog(false), nullptr);
    expectRender(false);
    computer->start(computerDialog(), nullptr);
    expectRender(true);
    computer->cleanupForModuleTransition();
    expectRender(false);
    // Reuse after transition does not inherit the previous presentation.
    computer->start(computerDialog(false), nullptr);
    expectRender(false);
}

TEST_P(ComputerGUITest, retiring_owner_ends_camera_without_retaining_runtime_object) {
    auto owner = game->newCreature();
    computer->start(computerDialog(), owner);
    EXPECT_EQ(computer->resolvedOwner(), owner);
    expectRender(true);
    std::weak_ptr<Object> retiredOwner = owner;
    game->destroyRuntimeObjectGraph(owner);
    owner.reset();
    EXPECT_TRUE(retiredOwner.expired());
    computer->update(0.0f);
    EXPECT_FALSE(computer->resolvedOwner());
    expectRender(false);
    EXPECT_EQ(computer->finishes, 1);
}

TEST_P(ComputerGUITest, automatic_reply_script_replacement_preserves_new_conversation) {
    auto dialog = computerDialog(true, true);
    dialog->replies[0].script = "replace_dialog";
    auto replacement = computerDialog(false);
    EXPECT_CALL(engine.resourceModule().scripts(), get("replace_dialog"))
        .WillOnce(Invoke([&](const std::string &) {
            computer->start(replacement, nullptr);
            return nullptr;
        }));
    computer->start(dialog, nullptr);
    computer->handle(key(input::KeyCode::Return));
    EXPECT_EQ(computer->entry(), &replacement->entries[0]);
    EXPECT_EQ(computer->loads, 2);
    EXPECT_EQ(computer->ends, 1);
    expectRender(false);
}

INSTANTIATE_TEST_SUITE_P(RetailGames, ComputerGUITest, Values(GameID::KotOR, GameID::TSL));

} // namespace
