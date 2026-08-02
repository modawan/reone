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

#include "reone/audio/clip.h"
#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/console.h"
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/resource/dialog.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class StubConsole : public IConsole, boost::noncopyable {
public:
    void registerCommand(std::string name, std::string description, CommandHandler handler) override {}
    void printLine(const std::string &text) override {}
};

class TestConversation : public Conversation {
public:
    TestConversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

    const std::string &currentText() const {
        return _currentEntry->text;
    }

    int entryLoadCount() const {
        return _entryLoadCount;
    }

    int entryEndCount() const {
        return _entryEndCount;
    }

    bool isPaused() const {
        return _paused;
    }

    void pickReplyForTest(int index) {
        pickReply(index);
    }

    void pauseOnFirstEntryLoad() {
        _pauseOnFirstEntryLoad = true;
    }

    const std::string &barkText() const {
        return _barkText;
    }

    int barkCount() const {
        return _barkCount;
    }

    int finishCount() const {
        return _finishCount;
    }

protected:
    void setReplyLines(std::vector<std::string> lines) override {}
    void setMessage(std::string message) override {}

    void setBarkText(std::string text, float duration) override {
        _barkText = std::move(text);
        ++_barkCount;
    }

    void onLoadEntry() override {
        ++_entryLoadCount;
        if (_pauseOnFirstEntryLoad && _entryLoadCount == 1) {
            pause();
        }
    }

    void onEntryEnded() override {
        ++_entryEndCount;
    }

    void onFinish() override {
        ++_finishCount;
    }

private:
    int _entryLoadCount {0};
    int _entryEndCount {0};
    bool _pauseOnFirstEntryLoad {false};
    std::string _barkText;
    int _barkCount {0};
    int _finishCount {0};
};

std::shared_ptr<Dialog> makeDialog(int firstEntryDelay = 1, bool voiced = false) {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "pause_test";
    Dialog::EntryReplyLink startLink;
    startLink.index = 0;
    dialog->startEntries.push_back(startLink);
    dialog->entries.resize(2);
    dialog->replies.resize(2);

    auto &firstEntry = dialog->entries[0];
    firstEntry.text = "first";
    firstEntry.delay = firstEntryDelay;
    Dialog::EntryReplyLink automaticReplyLink;
    automaticReplyLink.index = 0;
    firstEntry.replies.push_back(automaticReplyLink);
    if (voiced) {
        firstEntry.sound = "test_voice";
    }

    auto &automaticReply = dialog->replies[0];
    Dialog::EntryReplyLink secondEntryLink;
    secondEntryLink.index = 1;
    automaticReply.entries.push_back(secondEntryLink);

    auto &secondEntry = dialog->entries[1];
    secondEntry.text = "second";
    secondEntry.delay = 100;
    Dialog::EntryReplyLink visibleReplyLink;
    visibleReplyLink.index = 1;
    secondEntry.replies.push_back(visibleReplyLink);

    dialog->replies[1].text = "Continue";
    return dialog;
}

// A one-liner: a single start entry whose only eligible reply is empty and
// terminal. Both nodes carry an authored action.
std::shared_ptr<Dialog> makeOneLinerDialog(std::string entryScript, std::string replyScript) {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "one_liner_test";
    Dialog::EntryReplyLink startLink;
    startLink.index = 0;
    dialog->startEntries.push_back(startLink);
    dialog->entries.resize(1);
    dialog->replies.resize(1);

    auto &entry = dialog->entries[0];
    entry.text = "bark";
    entry.delay = 1;
    entry.script = std::move(entryScript);
    Dialog::EntryReplyLink terminalReplyLink;
    terminalReplyLink.index = 0;
    entry.replies.push_back(terminalReplyLink);

    // Empty text and no child entries -- this is what makes it a one-liner.
    dialog->replies[0].script = std::move(replyScript);
    return dialog;
}

std::shared_ptr<AudioClip> makeOneSecondClip() {
    auto clip = std::make_shared<AudioClip>();
    AudioClip::Frame frame;
    frame.sampleRate = 100;
    frame.samples.resize(100);
    clip->add(std::move(frame));
    return clip;
}

class ConversationTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        // Gives the game a real script runner over MockScripts, so dialogue
        // actions can be observed as requests for their script.
        _game->initLocalServices();
        _conversation = std::make_unique<TestConversation>(*_game, _engine.services());
    }

    void startSilent(int delay = 1) {
        _conversation->start(makeDialog(delay), nullptr);
    }

    void startVoiced() {
        auto clip = makeOneSecondClip();
        auto source = std::make_shared<AudioSource>(clip);

        EXPECT_CALL(static_cast<resource::MockLips &>(_engine.services().resource.lips), get("test_voice"))
            .WillOnce(Return(nullptr));
        EXPECT_CALL(_engine.resourceModule().audioClips(), get("test_voice"))
            .WillOnce(Return(clip));
        EXPECT_CALL(static_cast<audio::MockAudioMixer &>(_engine.services().audio.mixer), play(_, AudioType::Voice, _, _, _))
            .WillOnce(Return(source));

        _conversation->start(makeDialog(-1, true), nullptr);
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<TestConversation> _conversation;
};

TEST_F(ConversationTest, silent_entry_timer_expiry_does_not_advance_while_paused) {
    startSilent();
    _conversation->pause();

    _conversation->update(2.0f);

    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->entryEndCount());
}

TEST_F(ConversationTest, voice_completion_does_not_advance_while_paused) {
    startVoiced();
    _conversation->pause();

    _conversation->update(0.1f);

    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->entryEndCount());
}

TEST_F(ConversationTest, resume_after_expiry_advances_on_next_update_exactly_once) {
    startSilent();
    _conversation->pause();
    _conversation->update(2.0f);

    _conversation->resume();
    _conversation->update(0.0f);
    _conversation->update(0.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, resume_before_expiry_preserves_elapsed_timing) {
    startSilent();
    _conversation->pause();
    _conversation->update(0.4f);

    _conversation->resume();
    _conversation->update(0.5f);
    EXPECT_EQ("first", _conversation->currentText());

    _conversation->update(0.2f);
    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, unpaused_silent_entry_retains_automatic_progression) {
    startSilent();

    _conversation->update(1.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, unpaused_voiced_entry_retains_completion_progression) {
    startVoiced();

    _conversation->update(0.1f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, repeated_updates_while_paused_do_not_advance_or_duplicate_events) {
    startSilent();
    _conversation->pause();

    for (int i = 0; i < 10; ++i) {
        _conversation->update(1.0f);
    }

    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->entryEndCount());
}

TEST_F(ConversationTest, repeated_pause_and_resume_calls_are_safe_and_deterministic) {
    startSilent();
    _conversation->pause();
    _conversation->pause();
    _conversation->update(0.4f);

    _conversation->resume();
    _conversation->resume();
    _conversation->update(0.5f);
    EXPECT_EQ("first", _conversation->currentText());

    _conversation->update(0.2f);
    _conversation->pause();
    _conversation->pause();
    _conversation->resume();
    _conversation->resume();
    _conversation->update(0.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, automatic_skip_does_not_bypass_a_paused_entry) {
    Conversation::AutoSkip autoSkip;
    autoSkip.enabled = true;
    autoSkip.entries.push(true);
    _conversation->setAutoSkip(&autoSkip);
    _conversation->pauseOnFirstEntryLoad();

    _conversation->start(makeDialog(), nullptr);

    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->entryEndCount());
    EXPECT_TRUE(autoSkip.entries.empty());

    _conversation->resume();
    _conversation->update(1.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, unpaused_automatic_skip_retains_immediate_progression) {
    Conversation::AutoSkip autoSkip;
    autoSkip.enabled = true;
    autoSkip.entries.push(true);
    _conversation->setAutoSkip(&autoSkip);

    _conversation->start(makeDialog(), nullptr);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
    EXPECT_TRUE(autoSkip.entries.empty());
}

TEST_F(ConversationTest, entry_pause_then_external_resume_advances_next_entry_once) {
    _conversation->pauseOnFirstEntryLoad();
    _conversation->start(makeDialog(), nullptr);

    _conversation->update(2.0f);
    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(0, _conversation->entryEndCount());

    _conversation->resume();
    _conversation->update(0.0f);
    _conversation->update(0.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(2, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, replacing_a_paused_conversation_starts_the_new_session_unpaused) {
    startSilent();
    _conversation->pause();

    _conversation->start(makeDialog(), nullptr);
    EXPECT_FALSE(_conversation->isPaused());

    _conversation->update(1.0f);
    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(3, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, finishing_a_paused_conversation_does_not_leak_pause_to_the_next_session) {
    startSilent();
    _conversation->update(1.0f);
    _conversation->pause();

    _conversation->pickReplyForTest(0);
    EXPECT_FALSE(_conversation->isPaused());

    startSilent();
    _conversation->update(1.0f);
    _conversation->update(0.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(4, _conversation->entryLoadCount());
    EXPECT_EQ(2, _conversation->entryEndCount());
}

TEST_F(ConversationTest, finishing_leaves_a_screen_opened_by_a_reply_script_alone) {
    // A reply script can hand the screen to something else before the
    // conversation ends -- PlayPazaak opens the pazaak board from a dialogue
    // action -- so finishing must not pull the screen back to the world.
    startSilent();
    _conversation->update(1.0f);

    _conversation->pickReplyForTest(0);

    EXPECT_EQ(Game::Screen::None, _game->currentScreen());
}

TEST_F(ConversationTest, module_transition_cleanup_clears_pause_before_the_next_session) {
    startSilent();
    _conversation->pause();

    _conversation->cleanupForModuleTransition();
    EXPECT_FALSE(_conversation->isPaused());

    startSilent();
    _conversation->update(1.0f);

    EXPECT_EQ("second", _conversation->currentText());
    EXPECT_EQ(3, _conversation->entryLoadCount());
    EXPECT_EQ(1, _conversation->entryEndCount());
}

TEST_F(ConversationTest, one_liner_runs_entry_then_terminal_reply_actions_exactly_once) {
    auto &scripts = _engine.resourceModule().scripts();
    InSequence seq;
    EXPECT_CALL(scripts, get("one_entry")).WillOnce(Return(nullptr));
    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Return(nullptr));

    _conversation->start(makeOneLinerDialog("one_entry", "one_reply"), nullptr);
}

TEST_F(ConversationTest, one_liner_with_no_entry_action_still_runs_the_terminal_reply_action) {
    // The authored action commonly sits only on the terminal reply, so running
    // the entry action alone would still drop it.
    auto &scripts = _engine.resourceModule().scripts();
    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Return(nullptr));

    _conversation->start(makeOneLinerDialog("", "one_reply"), nullptr);
}

TEST_F(ConversationTest, one_liner_presents_its_entry_and_completes_without_opening_the_gui) {
    auto &scripts = _engine.resourceModule().scripts();
    EXPECT_CALL(scripts, get(_)).WillRepeatedly(Return(nullptr));

    _conversation->start(makeOneLinerDialog("one_entry", "one_reply"), nullptr);

    // Barked, not presented through the conversation GUI, and already over.
    EXPECT_EQ("bark", _conversation->barkText());
    EXPECT_EQ(1, _conversation->barkCount());
    EXPECT_EQ("bark", _conversation->currentText());
    EXPECT_EQ(Game::Screen::None, _game->currentScreen());
    EXPECT_EQ(1, _conversation->entryLoadCount());
}

TEST_F(ConversationTest, completed_one_liner_does_not_run_its_actions_a_second_time) {
    auto &scripts = _engine.resourceModule().scripts();
    EXPECT_CALL(scripts, get("one_entry")).WillOnce(Return(nullptr));
    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Return(nullptr));

    _conversation->start(makeOneLinerDialog("one_entry", "one_reply"), nullptr);

    // The one-liner is already resolved; further ticks must not re-pick it.
    for (int i = 0; i < 5; ++i) {
        _conversation->update(1.0f);
    }
    EXPECT_EQ(1, _conversation->barkCount());
}

TEST_F(ConversationTest, one_liner_entry_action_starting_a_conversation_keeps_the_new_session) {
    auto &scripts = _engine.resourceModule().scripts();
    auto replacement = makeDialog();

    // Stand in for an action script that starts another conversation: the
    // replacement happens at exactly the point the real script would run.
    EXPECT_CALL(scripts, get("one_entry")).WillOnce(Invoke([&](const std::string &) {
        _conversation->start(replacement, nullptr);
        return nullptr;
    }));
    // The old reply belongs to a conversation that no longer exists.
    EXPECT_CALL(scripts, get("one_reply")).Times(0);

    _conversation->start(makeOneLinerDialog("one_entry", "one_reply"), nullptr);

    // The new conversation is live and presented once by itself: the old one
    // must not have gone on to bark, reload or otherwise drive it.
    EXPECT_EQ("first", _conversation->currentText());
    EXPECT_EQ(1, _conversation->entryLoadCount());
    EXPECT_EQ(0, _conversation->barkCount());
    _conversation->update(1.0f);
    EXPECT_EQ("second", _conversation->currentText());
}

TEST_F(ConversationTest, one_liner_reply_action_starting_a_conversation_keeps_the_new_session) {
    auto &scripts = _engine.resourceModule().scripts();
    auto replacement = makeDialog();

    EXPECT_CALL(scripts, get("one_reply")).WillOnce(Invoke([&](const std::string &) {
        _conversation->start(replacement, nullptr);
        return nullptr;
    }));

    _conversation->start(makeOneLinerDialog("", "one_reply"), nullptr);

    // Only the replacement itself ended the one-liner. Had the old conversation
    // carried on to finish, it would have ended the new session instead.
    EXPECT_EQ(1, _conversation->finishCount());
    EXPECT_EQ("first", _conversation->currentText());
    _conversation->update(1.0f);
    EXPECT_EQ("second", _conversation->currentText());
}

} // namespace
