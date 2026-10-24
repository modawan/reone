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

struct DialogAnimation;
class Placeable;

class DialogGUI : public Conversation {
    friend class MixedStuntTestAccess;

public:
    DialogGUI(Game &game, ServicesView &services) :
        Conversation(game, services) {
        _resRef = guiResRef("dialog");
    }

    void update(float dt) override;
    /** Draws the letterbox fade under the conversation's panel. */
    void render() override;

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
        /** The ordinal, the clip's animation ID. */
        int id {0};
        std::string name;
        bool looping {false};
        /** The clip is authored in world space, and suspends the participant's head look. */
        bool worldSpace {false};
    };

    struct Controls {
        std::shared_ptr<gui::Label> LBL_MESSAGE;
        std::shared_ptr<gui::ListBox> LB_REPLIES;
    };

    Controls _controls;

    RuntimeObjectRef<Object> _currentSpeaker;
    std::map<std::string, Participant> _participantByTag;

    /**
     * The pair that turns to each other on each entry, and the pair before it.
     * While the replies are listed, the one who answers speaks to the
     * entry's speaker.
     */
    RuntimeObjectRef<Object> _pairSpeaker;
    RuntimeObjectRef<Object> _pairListener;
    RuntimeObjectRef<Object> _previousPairSpeaker;
    RuntimeObjectRef<Object> _previousPairListener;
    bool _pairStarted {false};

    /** The previous list's animation per participant, so unchanged ones are left alone. */
    std::map<std::string, int> _previousAnimations;
    /** The participants whose dialog animations the current node waits for, in list order. */
    std::vector<std::string> _waitedParticipants;
    /** The animations of the list before the previous one, whose participants the end of the conversation releases. */
    std::map<std::string, int> _lastButOneAnimations;
    /** The pull-back of the face shots in a row, while one is set. */
    float _faceShotPullBack {0.0f};
    /** The dialog camera starts afresh at its next shot: the conversation has just started, or shown an animated shot. */
    bool _dialogCameraStale {true};

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
    /** Frames the camera shot of \p shot, an entry or a reply, at \p angle. */
    void frameShot(const resource::Dialog::EntryReply &shot, int angle);
    /** How far the face shot of the speaker pair pulls back. */
    float faceShotPullBack();
    /** Make \p speaker and the current entry's listener the pair that turns to each other. */
    void updateSpeakerPair(const std::shared_ptr<Object> &speaker);
    /** The object the current entry's listener tag names, if any. */
    std::shared_ptr<Object> resolveEntryListener() const;
    /**
     * The one who answers speaks to the entry's speaker, who listens, and the
     * pair turns to each other as the first reply's camera shot has it.
     */
    void passTurnToReplySpeaker();
    /** The creature who answers in the conversation: the player character, or the party leader. */
    std::shared_ptr<Object> replySpeaker() const;
    /**
     * Release the previous pair's looks and turn the speaker and the listener
     * to each other; a camera shot of \p shot that keeps the previous pair
     * makes it the pair again. In an animated cutscene nobody turns.
     */
    void orientSpeakerPair(const resource::Dialog::EntryReply &shot);
    /** Turn \p turner's head, and its body when needed, toward \p other. */
    void turnToward(Object &turner, const std::shared_ptr<Object> &other);
    /** Release the pair's looks and forget the pair. */
    void endSpeakerPair();
    /** The object a participant tag names: a stunt participant's creature, or an object in the area. */
    std::shared_ptr<Object> participantObject(const std::string &participant) const;
    /** The creature a participant tag names: a stunt participant's, or one in the area. */
    std::shared_ptr<Creature> participantCreature(const std::string &participant) const;
    /** A creature that is debilitated, dead, or a party member at no vitality plays no dialog animation. */
    bool canPlayDialogAnimations(const Creature &creature) const;
    /**
     * Applies the animation list of \p node, an entry or a chosen reply, and
     * poses the speaker pair.
     */
    void updateParticipantAnimations(const resource::Dialog::EntryReply &node);
    /** The objects the participants of \p animations name. */
    std::vector<std::shared_ptr<Object>> participantObjects(const std::map<std::string, int> &animations) const;
    /**
     * Releases \p participants: a creature may look again and returns to its
     * pause, and a placeable shows its own state again.
     */
    void releaseParticipants(const std::vector<std::shared_ptr<Object>> &participants);
    /** The previous speaker pair returns to its pause, unless the previous list named it among \p named. */
    void returnPreviousPairToPause(const std::vector<std::shared_ptr<Object>> &named);
    /** A speaker or listener returns to its pause: a creature that can play dialog animations, or a placeable. */
    void returnToPause(Object &object) const;
    /**
     * The listener listens and the speaker talks, unless \p listed, the
     * objects the list animates, names them: injured, each its injured pose.
     */
    void poseSpeakerPair(const std::vector<std::shared_ptr<Object>> &listed);
    /** Plays dialog animation \p ordinal, a cut clip when \p cut, on a placeable participant. */
    void applyPlaceableAnimation(Placeable &placeable, int ordinal, const std::optional<CutAnimation> &cut);
    /**
     * Plays a cut clip on a participant.
     *
     * @return whether the participant's model had the clip and it played
     */
    bool applyCutAnimation(const std::string &participant, const CutAnimation &cut);
    /** Plays a dialog animation that is not a cut clip on a participant. */
    void applyDialogAnimation(const std::string &participant, int ordinal);
    /** Mixed-stunt participants that \p node does not drive from their stunt model leave their staging. */
    void restoreInactiveStuntParticipants(const resource::Dialog::EntryReply &node);
    /**
     * Stages a mixed-stunt participant in \p animation, a loop or a one-shot,
     * shown as animation \p animationId.
     */
    bool enterMixedStunt(Participant &participant, const std::shared_ptr<graphics::Animation> &animation, bool looping, int animationId);
    void leaveMixedStunt(Participant &participant);

    static std::optional<CutAnimation> decodeCutAnimation(int ordinal);
    /**
     * The pose a dialoganimations.2da ordinal names; Invalid for a lower
     * ordinal and for one the table does not name.
     */
    AnimationType getDialogAnimationType(int ordinal) const;
    /** The pose a dialoganimations.2da row names; Invalid for a name no pose has. */
    static AnimationType getDialogAnimationType(const DialogAnimation &row);
    /** The object a participant tag names in the area: the owner, the player, or one by tag. */
    std::shared_ptr<Object> resolveParticipantObject(const std::string &participant) const;
    std::shared_ptr<Creature> resolveParticipantCreature(const std::string &participant) const;
    bool hasStuntPresentation() const;
    std::shared_ptr<graphics::Animation> getStuntParticipantAnimation(
        const std::string &participant,
        int ordinal) const;

    void setMessage(std::string message) override;
    void setReplyLines(std::vector<std::string> lines) override;
    std::shared_ptr<gui::ListBox> replyList() const override;

    void onStart() override;
    void onFinish() override;
    void onLoadEntry() override;
    void onEntryEnded() override;
    void onReplyPicked(const resource::Dialog::EntryReply &reply) override;
    void onReplyTaken(const resource::Dialog::EntryReply &reply) override;
    /** A creature named by the last list of animations still shows a dialog animation. */
    bool isDialogAnimationPlaying() const override;

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

    // END Participants
};

} // namespace game

} // namespace reone
