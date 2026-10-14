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

#pragma once

#include "reone/audio/source.h"
#include "reone/graphics/lipanimation.h"
#include "reone/graphics/model.h"
#include "reone/resource/dialog.h"
#include "reone/system/timer.h"

#include "../gui.h"
#include "../globalfade.h"
#include "../object.h"
#include "../runtimeref.h"
#include "../types.h"

namespace reone {

namespace game {

class Conversation : public GameGUI {
    friend class TestConversation;

public:
    Conversation(Game &game, ServicesView &services) :
        GameGUI(game, services) {
    }

    bool handle(const input::Event &event) override;
    void update(float dt) override;

    /**
     * Start \p dialog owned by \p owner. \p listener is the object that
     * started the conversation with the owner, when there is one.
     */
    void start(const std::shared_ptr<resource::Dialog> &dialog, const std::shared_ptr<Object> &owner,
               GlobalFade::DialogTicket admission = {},
               const std::shared_ptr<Object> &listener = nullptr);
    void cleanupForModuleTransition();

    CameraType getCamera(int &cameraId) const;

    void pause();

    void resume();

    /** The owner breaking off ends the conversation; any other participant leaves it. */
    void stopParticipant(const Object &object);

    struct AutoSkip {
        std::queue<std::optional<int>> replies;
        std::queue<bool> entries;
        bool enabled {false};

        std::optional<int> trySkipReply();
        std::optional<bool> trySkipEntry();
    };

    void setAutoSkip(AutoSkip *skip) { _autoSkip = skip; }

protected:
    std::shared_ptr<Object> owner() const { return _owner.resolve(); }
    std::shared_ptr<resource::Dialog> _dialog;
    RuntimeObjectRef<Object> _owner;
    /** The object that started the conversation with the owner. */
    RuntimeObjectRef<Object> _partner;
    std::shared_ptr<graphics::Model> _cameraModel;
    std::shared_ptr<graphics::LipAnimation> _lipAnimation;
    const resource::Dialog::EntryReply *_currentEntry {nullptr};
    bool _entryEnded {false};
    bool _paused {false};

    virtual void loadEntry(int index, bool start = false);

    void pickReply(int index);

    // Complete the active entry's presentation using the same path as expiry.
    void endCurrentEntry();

    virtual void setReplyLines(std::vector<std::string> lines) = 0;

    // How a one-liner presents its entry, alongside setMessage/setReplyLines.
    virtual void setBarkText(std::string text, float duration);

    virtual void onStart();
    virtual void onFinish();
    virtual void onLoadEntry();
    virtual void onEntryEnded();

private:
    std::shared_ptr<audio::AudioSource> _currentVoice;
    Timer _endEntryTimer;
    float _entryDuration {0.0f};
    std::vector<const resource::Dialog::EntryReply *> _replies;
    bool _autoPickFirstReply {false};
    /** Whether the conversation has shown the entry it ends on. */
    bool _endReached {false};
    AutoSkip *_autoSkip {nullptr};
    GlobalFade::DialogTicket _fadeDialog;
    uint64_t _generation {0};
    /** The objects this conversation has made its participants. */
    std::vector<RuntimeObjectRef<Object>> _dialogParticipants;

    /** Make \p object a participant of this conversation, unless another conversation still holds it. */
    void attachDialogParticipant(const std::shared_ptr<Object> &object);
    /** Release the participants that \p owner's conversation still holds. */
    void releaseDialogParticipants(const std::shared_ptr<Object> &owner);
    /** Run the end-dialogue scripts of the creatures and placeables in \p owner's area. */
    void runAreaEndDialogScripts(const Object &owner);

    bool isCurrent(uint64_t generation) const;

    void loadConversationBackground();
    void loadCameraModel();
    void loadStartEntry();
    void loadVoiceOver();
    void scheduleEndOfEntry();
    void loadReplies();

    bool isSkippableEntry() const;
    bool isNonPresentationalEntry() const;

    void refreshReplies();

    /**
     * Ends the conversation.
     *
     * @param runsEndScript whether the conversation's end script runs now; a
     *        one-liner has already run it, or runs none
     * @param aborted whether the conversation was cut off before the entry it
     *        ends on: its abort script then runs in place of the end script
     *        and the end-dialogue scripts
     */
    void finish(bool runsEndScript = true, bool aborted = false);

    /**
     * Runs the conversation's end script, with the owner as the caller.
     */
    void runEndScript(const resource::Dialog &dialog, const Object &owner);

    int indexOfFirstActive(const std::vector<resource::Dialog::EntryReplyLink> &links);

    bool isLinkActive(const resource::Dialog::EntryReplyLink &link);
    bool evaluateCondition(const std::string &scriptResRef, const resource::Dialog::EntryReplyLink::ConditionParams &params);
    void runScript(const std::string &scriptResRef, const resource::Dialog::EntryReply::ActionParams &params);
    void runScripts(const resource::Dialog::EntryReply &node);
    void applyStatusSummaryEntries(const resource::Dialog::EntryReply &node);

    virtual void setMessage(std::string message) = 0;

    // Event handlers

    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);

    // END Event handlers
};

} // namespace game

} // namespace reone
