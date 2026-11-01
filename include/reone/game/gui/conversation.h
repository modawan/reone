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

#include <array>
#include <map>

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

namespace gui {

class ListBox;

}

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

    /** The conversation's dialog is an animated cutscene. */
    bool isAnimatedCutscene() const { return _dialog && _dialog->isAnimatedCutscene(); }

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

    /** The current entry's shot is an animation of the conversation's camera model. */
    bool isAnimatedCameraShot() const;
    /** The camera angle of the current entry's shot: its own, or the one the conversation gave it. */
    int entryCameraAngle() const { return _entryCameraAngle; }
    /** The camera angle of the first listed reply's shot: its own, or the one the conversation gave it. */
    int firstReplyCameraAngle() const { return _firstReplyCameraAngle; }
    /**
     * The view takes the camera of \p shot at \p angle: a placed camera
     * (angle 6) shows the shot's camera, angle 5 keeps the view of a cinematic
     * conversation as it is, and any other angle is a dialog camera shot.
     * After an animated shot the dialog camera takes the view first.
     */
    void takeCameraShot(const resource::Dialog::EntryReply &shot, int angle);
    /** The dialog camera shows the shot, as for a placed camera the area lacks. */
    void showShotOnDialogCamera() { _shotCamera = CameraType::Dialog; }
    /** The camera shot of \p shot, an entry or a reply, keeps the previous speaker pair. */
    static bool keepsPreviousSpeakerPair(const resource::Dialog::EntryReply &shot);

    /** The current entry's sole reply is taken when the entry ends, rather than its replies listed. */
    bool autoPicksFirstReply() const { return _autoPickFirstReply; }
    /** The first of the current entry's replies, if it has any. */
    std::optional<std::reference_wrapper<const resource::Dialog::EntryReply>> firstReply() const;

    void pickReply(int index);

    // Complete the active entry's presentation using the same path as expiry.
    void endCurrentEntry();
    /**
     * Asks to skip the shown entry, paused or not. The next update that runs
     * the conversation honours it, if the entry can be skipped then.
     */
    void requestSkip();
    /**
     * Whether a press asks for a skip: while the entry shows, and while a
     * reply taken without a choice holds the next entry back.
     */
    bool acceptsSkip() const;
    /** As the replies are listed, selects the row under the pointer, if any. */
    void selectReplyUnderPointer();

    /**
     * The conversation's own input: a left button press or Return or keypad
     * Enter asks to skip the shown entry. With the replies listed, Return or
     * keypad Enter chooses the selected reply, a number key the reply of its
     * number, and the up and down arrows move the selection.
     *
     * @return whether \p event was used up
     */
    bool handleConversationInput(const input::Event &event);
    /** The list the replies are listed in; none in a presentation without one. */
    virtual std::shared_ptr<gui::ListBox> replyList() const;

    /** One line per reply, in reply order; an empty line stands for a reply that is not listed. */
    virtual void setReplyLines(std::vector<std::string> lines) = 0;

    // How a one-liner presents its entry, alongside setMessage/setReplyLines.
    virtual void setBarkText(std::string text, float duration);

    virtual void onStart();
    virtual void onFinish();
    virtual void onLoadEntry();
    virtual void onEntryEnded();
    /** The player chose \p reply from the listed replies; called before the reply's scripts run. */
    virtual void onReplyPicked(const resource::Dialog::EntryReply &reply);
    /** A reply was taken without a choice, as the entry ended; called before the reply's scripts run. */
    virtual void onReplyTaken(const resource::Dialog::EntryReply &reply);
    /** Whether a participant of the list waited for still shows a dialog animation. */
    virtual bool isDialogAnimationPlaying() const;

    /**
     * The dialogue letterbox's fade: a colour over the whole screen, which an
     * entry sets and the arrival of its replies clears. It runs only while
     * the letterbox is drawn.
     */
    struct EntryFade {
        glm::vec3 color {0.0f};
        float alpha {0.0f};
        float delay {0.0f};
        float length {0.0f};
        /** Seconds since the fade was set, counted from a tenth of a second; 0 without a fade. */
        float clock {0.0f};
        /** The fade goes from the colour to clear, rather than from clear to the colour. */
        bool fromColor {false};
    };

    const EntryFade &entryFade() const { return _entryFade; }
    /** Runs the letterbox fade on by \p dt seconds, as the letterbox is drawn. */
    void advanceEntryFade(float dt);
    /** Clears the letterbox fade. */
    void resetEntryFade();

private:
    std::shared_ptr<audio::AudioSource> _currentVoice;
    Timer _endEntryTimer;
    /** What the current entry, or the reply taken without a choice, waits for past its delay. */
    int _waitFlags {0};
    /** The entry that follows a reply taken without a choice, once the reply's delay and waits are over. */
    std::optional<int> _pendingEntry;
    /** A skip of the shown entry has been asked for and not yet honoured. */
    bool _entrySkipped {false};
    EntryFade _entryFade;
    std::vector<const resource::Dialog::EntryReply *> _replies;
    bool _autoPickFirstReply {false};
    /** Whether the conversation has listed the replies of the entry it ends on. */
    bool _endReached {false};
    AutoSkip *_autoSkip {nullptr};
    GlobalFade::DialogTicket _fadeDialog;
    uint64_t _generation {0};
    /** The objects this conversation has made its participants. */
    std::vector<RuntimeObjectRef<Object>> _dialogParticipants;

    /**
     * A walk through a cycle of values, which starts where the dialog's name
     * puts it and moves by a step in one direction.
     */
    struct DrawCycle {
        static constexpr int kLength = 19;
        using Values = std::array<int, kLength>;

        int place {0};
        int step {1};
        bool forward {false};

        /** Starts the walk where \p seed puts it. */
        void seed(uint32_t seed);
        /** The value at the walk's place in \p values; the walk then moves on. */
        int draw(const Values &values);
    };

    // Camera shots

    /** The walk through the cycle of camera angles. */
    DrawCycle _angleCycle;
    /** No entry, or no list of replies, has been given a camera angle yet. */
    bool _firstEntryShot {true};
    bool _firstReplyShot {true};
    /** The camera angles of the entries and replies by index, once shown, as given. */
    std::map<int, int> _entryCameraAngles;
    std::map<int, int> _replyCameraAngles;
    /** The resolved camera angle of the current entry. */
    int _entryCameraAngle {0};
    /** The resolved camera angle of the first listed reply. */
    int _firstReplyCameraAngle {0};
    /** The camera the last shot took, and the placed camera's id. */
    CameraType _shotCamera {CameraType::Dialog};
    int _shotCameraId {0};
    /** The view is an animated shot, until another shot is taken. */
    bool _animatedView {false};

    /** Starts the cycles of camera angles and sound variations where the dialog's name puts them. */
    void seedCameraAngles();
    /** The next angle of the cycle, for an entry or reply without one. */
    int drawCameraAngle();

    // END Camera shots

    /** The walk through the cycle of sound variation numbers. */
    DrawCycle _soundVariationCycle;
    /** The sounds of the entries by index that took a variation number, once shown. */
    std::map<int, std::string> _entrySounds;

    /** Make \p object a participant of this conversation, unless another conversation still holds it. */
    void attachDialogParticipant(const std::shared_ptr<Object> &object);
    /** Release the participants that \p owner's conversation still holds. */
    void releaseDialogParticipants(const std::shared_ptr<Object> &owner);
    /** Run the end-dialogue scripts of the creatures and placeables in \p owner's area. */
    void runAreaEndDialogScripts(const Object &owner);

    bool isCurrent(uint64_t generation) const;

    void loadCameraModel();
    void loadStartEntry();
    /** Plays the current entry's voice: \p sound, or else its voice-over. */
    void loadVoiceOver(const std::string &sound);
    void scheduleEndOfEntry();
    /** Holds the next entry back after \p reply, taken without a choice. */
    void scheduleTakenReply(const resource::Dialog::EntryReply &reply);
    /** Whether something the current entry or the taken reply waits for still goes on. */
    bool isWaiting() const;
    /** Stops the conversation's voice. */
    void stopVoice();
    /** Sets the letterbox fade of \p entry, if it has one. */
    void startEntryFade(const resource::Dialog::EntryReply &entry);
    /** Whether the letterbox fade is over, or none is set. */
    bool isEntryFadeDone() const;
    /** Whether the conversation's animated camera still plays its clip. */
    bool isCameraAnimationPlaying() const;
    void loadReplies();

    bool isSkippableEntry() const;
    /** Add the shown entry's \p text to the dialogue log under its speaker's name. */
    void logEntryLine(const std::string &text);

    /** How many of the replies are listed, and so can be taken. */
    size_t listedReplyCount() const;
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
    /** Set or clear the video effect for the current entry's camera shot. */
    void applyShotVideoEffect();

    virtual void setMessage(std::string message) = 0;

    // Event handlers

    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    /** Return, keypad Enter, the arrows and the number keys, on their press. */
    bool handleKeyDown(const input::KeyEvent &event);
    /** Chooses the reply the reply list has selected, if it has one. */
    void pickSelectedReply();
    /** Moves the reply list's selection by \p step rows. */
    void moveReplySelection(int step);

    // END Event handlers
};

} // namespace game

} // namespace reone
