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

#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"

#include "../object/camera/dialog.h"
#include "../object/creature.h"

#include "conversation.h"

namespace reone {

namespace game {

class DialogGUI : public Conversation {
    friend class MixedStuntTestAccess;

public:
    DialogGUI(Game &game, ServicesView &services) :
        Conversation(game, services) {
        _resRef = guiResRef("dialog");
    }

    void update(float dt) override;

    /** Selects, but does not activate, a reply for a scripted visual capture. */
    void selectReplyForCapture(int index);

    /** The object is the current entry's speaker or listener. */
    bool isSpeakerOrListener(const Object &object) const;

private:
    struct Participant {
        std::shared_ptr<graphics::Model> model;
        RuntimeObjectRef<Creature> creature;
        bool mixedStuntActive {false};
        glm::vec3 restorePosition {0.0f};
        float restoreFacing {0.0f};
        bool restoreCulling {true};
    };

    /**
     * A cutscene clip named directly by a DLG animation ordinal, together with
     * whether the clip loops rather than playing once. An empty name stands
     * for the participant's pause.
     */
    struct CutAnimation {
        std::string name;
        bool looping {false};
        /** The clip is authored in world space, and suspends the participant's head look. */
        bool worldSpace {false};
    };

    /** A pose named by a dialoganimations.2da row. */
    struct DialogPose {
        AnimationType type {AnimationType::Invalid};
        /** The row is one of the table's dialog animations. */
        bool dialog {false};
    };

    struct Controls {
        std::shared_ptr<gui::Label> LBL_MESSAGE;
        std::shared_ptr<gui::ListBox> LB_REPLIES;
    };

    Controls _controls;

    RuntimeObjectRef<Object> _currentSpeaker;
    std::map<std::string, Participant> _participantByTag;

    /** The pair that turns to each other on each entry, and the pair before it. */
    RuntimeObjectRef<Object> _pairSpeaker;
    RuntimeObjectRef<Object> _pairListener;
    RuntimeObjectRef<Object> _previousPairSpeaker;
    RuntimeObjectRef<Object> _previousPairListener;
    bool _pairStarted {false};

    /** The previous entry's animation per participant, so unchanged ones are left alone. */
    std::map<std::string, int> _previousAnimations;

    /**
     * Ordinary creatures given a cutscene clip. They return to state-driven
     * animation when the conversation ends.
     */
    std::vector<RuntimeObjectRef<Creature>> _cutParticipants;

    void preload(gui::IGUI &gui) override;
    void onGUILoaded() override;

    void bindControls() {
        _controls.LBL_MESSAGE = findControl<gui::Label>("LBL_MESSAGE");
        _controls.LB_REPLIES = findControl<gui::ListBox>("LB_REPLIES");
    }

    void addFrame(std::string tag, int top);
    void configureMessage();
    void configureReplies();
    void repositionMessage();

    void updateCamera();
    /** Make \p speaker and the current entry's listener the pair that turns to each other. */
    void updateSpeakerPair(const std::shared_ptr<Object> &speaker);
    /** The object the current entry's listener tag names, if any. */
    std::shared_ptr<Object> resolveEntryListener() const;
    /** Release the previous pair's looks and turn the speaker and the listener to each other. */
    void orientSpeakerPair();
    /** Turn \p turner's head, and its body when needed, toward \p other. */
    void turnToward(Object &turner, const std::shared_ptr<Object> &other);
    /** Release the pair's looks and forget the pair. */
    void endSpeakerPair();
    /** The creature a participant tag names: a stunt participant's, or one in the area. */
    std::shared_ptr<Creature> participantCreature(const std::string &participant) const;
    /** A creature that is debilitated, dead, or a party member at no vitality plays no dialog animation. */
    bool canPlayDialogAnimations(const Creature &creature) const;
    void updateParticipantAnimations();
    void applyCutAnimation(const std::string &participant, const CutAnimation &cut);
    void applyDialogAnimation(const std::string &participant, int ordinal);
    void restoreInactiveStuntParticipants();
    bool enterMixedStunt(Participant &participant, const std::shared_ptr<graphics::Animation> &animation, bool looping);
    void leaveMixedStunt(Participant &participant);

    glm::vec3 getTalkPosition(const Object &object) const;
    DialogCamera::Variant getRandomCameraVariant() const;
    static std::optional<CutAnimation> decodeCutAnimation(int ordinal);
    AnimationType getDialogAnimationType(int ordinal) const;
    /** The pose a non-cut ordinal names; an Invalid type for one the table does not name. */
    DialogPose getDialogPose(int ordinal) const;
    std::shared_ptr<Creature> resolveParticipantCreature(const std::string &participant) const;
    bool hasStuntPresentation() const;
    std::shared_ptr<graphics::Animation> getStuntParticipantAnimation(
        const std::string &participant,
        int ordinal) const;

    void setMessage(std::string message) override;
    void setReplyLines(std::vector<std::string> lines) override;

    void onStart() override;
    void onFinish() override;
    void onLoadEntry() override;
    void onEntryEnded() override;

    // Loading

    int bandHeight() const;
    gui::Control::Extent bandExtent(int top) const;
    /** The centred 4:3 rectangle within the bottom band that the replies occupy. */
    gui::Control::Extent replySafeArea() const;
    void loadFrames();
    void loadCurrentSpeaker();

    // END Loading

    // Participants

    void loadStuntParticipants();
    void releaseStuntParticipants();
    void addCutParticipant(const std::shared_ptr<Creature> &creature);
    void releaseCutParticipants();

    // END Participants
};

} // namespace game

} // namespace reone
