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

#include "reone/game/gui/dialog.h"

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/panel.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/types.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static const char kControlTagTopFrame[] = "TOP";
static const char kControlTagBottomFrame[] = "BOTTOM";
static const char kObjectTagOwner[] = "owner";

// Odyssey DLG participant animation ordinals occupy two namespaces.
//
// Ordinals at or above kDialogAnimationBase index dialoganimations.2da and name
// a semantic dialogue animation. K1 also uses valid positive 2DA rows directly.
// Recognized lower ordinal bands name a cutscene clip on the target model: the
// band selects the clip name suffix and whether the clip is held, while the
// offset within the band selects the clip number. Both namespaces are
// independent of AnimatedCut and of whether the participant is driven by a
// stunt model.
static constexpr int kDialogAnimationBase = 10000;
static constexpr int kCutAnimationBandSize = 200;

static const struct CutAnimationBand {
    int base;
    const char *suffix;
    bool looping;
} g_cutAnimationBands[] {
    {1000, "", false},
    {1200, "w", false},
    {1400, "l", true},
    {1600, "wl", true}};

static const std::unordered_map<std::string, AnimationType> g_animTypeByName {
    {"dead", AnimationType::LoopingDead},
    {"taunt", AnimationType::FireForgetTaunt},
    {"greeting", AnimationType::FireForgetGreeting},
    {"listen", AnimationType::LoopingListen},
    {"worship", AnimationType::LoopingWorship},
    {"salute", AnimationType::FireForgetSalute},
    {"bow", AnimationType::FireForgetBow},
    {"talk_normal", AnimationType::LoopingTalkNormal},
    {"talk_pleading", AnimationType::LoopingTalkPleading},
    {"talk_forceful", AnimationType::LoopingTalkForceful},
    {"talk_laughing", AnimationType::LoopingTalkLaughing},
    {"talk_sad", AnimationType::LoopingTalkSad},
    {"victory", AnimationType::FireForgetVictory1},
    {"scratch_head", AnimationType::FireForgetPauseScratchHead},
    {"drunk", AnimationType::LoopingPauseDrunk},
    {"inject", AnimationType::FireForgetInject},
    {"flirt", AnimationType::LoopingFlirt},
    {"use_computer_lp", AnimationType::LoopingUseComputer},
    {"horror", AnimationType::LoopingHorror},
    {"use_computer", AnimationType::FireForgetUseComputer},
    {"persuade", AnimationType::FireForgetPersuade},
    {"activate", AnimationType::FireForgetActivate},
    {"sleep", AnimationType::LoopingSleep},
    {"prone", AnimationType::LoopingProne},
    {"ready", AnimationType::LoopingReady},
    {"pause", AnimationType::LoopingPause},
    {"choked", AnimationType::LoopingChoke},
    {"talk_injured", AnimationType::LoopingTalkInjured},
    {"listen_injured", AnimationType::LoopingListenInjured},
    {"kneel_talk_angry", AnimationType::LoopingKneelTalkAngry},
    {"kneel_talk_sad", AnimationType::LoopingKneelTalkSad}};

void DialogGUI::preload(IGUI &gui) {
    GameGUI::preload(gui);
    // Conversation bands and reply boxes are viewport-relative rather than
    // authored plate art. Their dialog-specific font scale follows the
    // uniform limiting axis without inheriting the global text multiplier.
    gui.setScaling(GUI::ScalingMode::Stretch);
    gui.setTextScale(_game.options().graphics.guiDialogTextScale);
}

void DialogGUI::onGUILoaded() {
    bindControls();
    configureMessage();
    configureReplies();
    loadFrames();

    _controls.LB_REPLIES->setOnItemClick([this](const std::string &item) {
        int replyIdx = stoi(item);
        pickReply(replyIdx);
    });
}

void DialogGUI::loadFrames() {
    int rootTop = _gui->rootControl().extent().top;
    int messageHeight = _controls.LBL_MESSAGE->extent().height;

    addFrame(kControlTagTopFrame, -rootTop, messageHeight);
    addFrame(kControlTagBottomFrame, 0, _game.options().graphics.height - rootTop);
}

void DialogGUI::addFrame(std::string tag, int top, int height) {
    auto frame = _gui->newControl(ControlType::Panel, tag);

    Control::Extent extent;
    extent.left = -_gui->rootControl().extent().left;
    extent.top = top;
    extent.width = _game.options().graphics.width;
    extent.height = height;

    frame->setExtent(std::move(extent));
    frame->setBorderFill("blackfill");

    _gui->addControlToFront(std::move(frame), IGUI::ControlCoordinates::Screen);
}

void DialogGUI::configureMessage() {
    _controls.LBL_MESSAGE->setExtentTop(-_gui->rootControl().extent().top);
    _controls.LBL_MESSAGE->setTextColor(_baseColor);
}

void DialogGUI::configureReplies() {
    _controls.LB_REPLIES->setProtoMatchContent(true);
    _controls.LB_REPLIES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_REPLIES->protoItem().setTextColor(_baseColor);
}

void DialogGUI::onStart() {
    _currentSpeaker = _owner;
    _heldCutParticipants.clear();
    loadStuntParticipants();

    auto camera = _game.module()->area()->getCamera<AnimatedCamera>(CameraType::Animated);
    camera->setModel(_cameraModel);
}

void DialogGUI::loadStuntParticipants() {
    if (!hasStuntPresentation()) {
        return;
    }

    _participantByTag.clear();

    for (auto &stunt : _dialog->stunts) {
        std::shared_ptr<Creature> creature(resolveParticipantCreature(stunt.participant));
        if (!creature) {
            warn("Dialog: participant creature not found by tag: " + stunt.participant);
            continue;
        }
        Participant participant;
        participant.creature = creature;

        std::shared_ptr<Model> model(_services.resource.models.get(stunt.stuntModel));
        if (!model) {
            warn("Dialog: stunt model not found: " + stunt.stuntModel);
            continue;
        }
        participant.model = model;

        if (_dialog->isAnimatedCutscene()) {
            creature->startStuntMode();
            participant.creature->setIsInConversation(true);
        }

        _participantByTag.insert(std::make_pair(stunt.participant, std::move(participant)));
    }
}

bool DialogGUI::hasStuntPresentation() const {
    return _dialog->isAnimatedCutscene() || !_dialog->stunts.empty();
}

std::shared_ptr<Creature> DialogGUI::resolveParticipantCreature(const std::string &participant) const {
    if (participant == kObjectTagOwner) {
        return std::dynamic_pointer_cast<Creature>(_owner);
    }
    if (boost::iequals(participant, kObjectTagPlayer)) {
        return _game.party().player();
    }
    return std::dynamic_pointer_cast<Creature>(_game.module()->area()->getObjectByTag(participant));
}

std::shared_ptr<Animation> DialogGUI::getStuntParticipantAnimation(
    const std::string &participant,
    int ordinal) const {
    auto cut = decodeCutAnimation(ordinal);
    if (!cut) {
        return nullptr;
    }
    auto maybeParticipant = _participantByTag.find(participant);
    return maybeParticipant != _participantByTag.end()
               ? maybeParticipant->second.model->getAnimation(cut->name)
               : nullptr;
}

void DialogGUI::onLoadEntry() {
    restoreInactiveStuntParticipants();
    loadCurrentSpeaker();
    updateParticipantAnimations();
    updateCamera();
    repositionMessage();

    _controls.LB_REPLIES->setVisible(false);
}

void DialogGUI::restoreInactiveStuntParticipants() {
    if (_dialog->isAnimatedCutscene()) {
        return;
    }
    for (auto &entry : _participantByTag) {
        if (!entry.second.mixedStuntActive) {
            continue;
        }
        bool drivenThisEntry = false;
        for (auto &anim : _currentEntry->animations) {
            if (anim.participant == entry.first && getStuntParticipantAnimation(anim.participant, anim.animation)) {
                drivenThisEntry = true;
                break;
            }
        }
        if (!drivenThisEntry) {
            leaveMixedStunt(entry.second);
        }
    }
}

bool DialogGUI::enterMixedStunt(Participant &participant, const std::shared_ptr<Animation> &animation, bool looping) {
    if (!participant.mixedStuntActive && participant.creature->isStuntMode()) {
        warn("Dialog: participant is already in stunt mode: " + participant.creature->tag());
        return false;
    }

    AnimationProperties properties;
    properties.flags = AnimationFlags::propagate | (looping ? AnimationFlags::loop : 0);
    properties.scale = 1.0f;
    if (!participant.creature->playExternalAnimation(animation, std::move(properties))) {
        return false;
    }

    if (!participant.mixedStuntActive) {
        participant.restorePosition = participant.creature->position();
        participant.restoreFacing = participant.creature->getFacing();
        if (auto node = participant.creature->sceneNode()) {
            participant.restoreCulling = node->isCullingEnabled();
        }
        participant.creature->startStuntMode();
        participant.mixedStuntActive = true;
    }
    return true;
}

void DialogGUI::leaveMixedStunt(Participant &participant) {
    if (!participant.mixedStuntActive) {
        return;
    }
    participant.creature->resumeStateDrivenAnimation();
    participant.creature->setPosition(participant.restorePosition);
    participant.creature->setFacing(participant.restoreFacing);
    participant.creature->stopStuntMode();
    if (auto node = participant.creature->sceneNode()) {
        node->setCullingEnabled(participant.restoreCulling);
    }
    participant.mixedStuntActive = false;
}

void DialogGUI::loadCurrentSpeaker() {
    std::shared_ptr<Area> area(_game.module()->area());
    std::shared_ptr<Object> speaker;

    if (!_currentEntry->speaker.empty()) {
        speaker = area->getObjectByTag(_currentEntry->speaker);
    }
    if (!speaker) {
        speaker = _owner;
    }

    // Make previous speaker stop talking, if any
    if (_currentSpeaker && _currentSpeaker != speaker) {
        auto speakerCreature = std::dynamic_pointer_cast<Creature>(_currentSpeaker);
        if (speakerCreature) {
            speakerCreature->stopTalking();
        }
    }
    _currentSpeaker = speaker;

    // Make current speaker face the player, and vice versa
    if (_currentSpeaker) {
        std::shared_ptr<Creature> player(_game.party().player());
        player->face(*_currentSpeaker);

        auto speakerCreature = std::dynamic_pointer_cast<Creature>(_currentSpeaker);
        if (speakerCreature) {
            speakerCreature->startTalking(_lipAnimation);
            speakerCreature->face(*player);
        }
    }
}

void DialogGUI::updateCamera() {
    std::shared_ptr<Area> area(_game.module()->area());

    if (_dialog->cameraModel.empty()) {
        std::shared_ptr<Creature> player(_game.party().player());
        glm::vec3 listenerPosition(player ? getTalkPosition(*player) : glm::vec3(0.0f));
        glm::vec3 speakerPosition(_currentSpeaker ? getTalkPosition(*_currentSpeaker) : glm::vec3(0.0f));
        auto camera = area->getCamera<DialogCamera>(CameraType::Dialog);
        camera->setListenerPosition(listenerPosition);
        camera->setSpeakerPosition(speakerPosition);
        camera->setVariant(getRandomCameraVariant());
    } else {
        auto camera = area->getCamera<AnimatedCamera>(CameraType::Animated);
        camera->setFieldOfView(_currentEntry->camFieldOfView != 0.0f ? _currentEntry->camFieldOfView : kDefaultAnimCamFOV);
        camera->playAnimation(_currentEntry->cameraAnimation);
    }
}

glm::vec3 DialogGUI::getTalkPosition(const Object &object) const {
    auto node = object.sceneNode();
    if (node->type() != SceneNodeType::Model) {
        return object.position();
    }

    auto model = std::static_pointer_cast<ModelSceneNode>(node);
    std::shared_ptr<ModelNode> talkDummy(model->model().getNodeByNameRecursive("talkdummy"));
    if (!talkDummy)
        return model->getWorldCenterOfAABB();

    return (model->absoluteTransform() * talkDummy->absoluteTransform())[3];
}

DialogCamera::Variant DialogGUI::getRandomCameraVariant() const {
    int r = randomInt(0, 2);
    switch (r) {
    case 0:
        return _entryEnded ? DialogCamera::Variant::ListenerClose : DialogCamera::Variant::SpeakerClose;
    case 1:
        return _entryEnded ? DialogCamera::Variant::ListenerFar : DialogCamera::Variant::SpeakerFar;
    default:
        return DialogCamera::Variant::Both;
    }
}

void DialogGUI::updateParticipantAnimations() {
    // Each authored animation is resolved on its own. The ordinal decides which
    // animation is meant, the participant decides which model plays it, and a
    // single entry may drive stunt-bound participants and ordinary area
    // creatures side by side.
    for (auto &anim : _currentEntry->animations) {
        if (auto cut = decodeCutAnimation(anim.animation)) {
            applyCutAnimation(anim.participant, *cut);
        } else {
            applyDialogAnimation(anim.participant, anim.animation);
        }
    }
}

void DialogGUI::applyCutAnimation(const std::string &participant, const CutAnimation &cut) {
    auto maybeParticipant = _participantByTag.find(participant);
    if (maybeParticipant != _participantByTag.end()) {
        Participant &stunt = maybeParticipant->second;
        if (auto animation = stunt.model->getAnimation(cut.name)) {
            if (_dialog->isAnimatedCutscene()) {
                AnimationProperties properties;
                properties.flags = AnimationFlags::propagate | (cut.looping ? AnimationFlags::loop : 0);
                properties.scale = 1.0f;
                stunt.creature->playExternalAnimation(animation, std::move(properties));
            } else {
                enterMixedStunt(stunt, animation, cut.looping);
            }
            return;
        }
        // The stunt model is the authored source for this participant, so a
        // missing clip is a data problem rather than a reason to silently
        // animate from somewhere else. Staged participants also sit at the
        // stunt origin, where an in-place clip would play in the wrong place.
        warn("Dialog: stunt model has no animation: " + cut.name);
        return;
    }

    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return;
    }
    auto node = creature->sceneNode();
    if (!node || node->type() != SceneNodeType::Model) {
        return;
    }
    // Cut clips authored without the world-space suffix live on the creature's
    // own model, so they play in place rather than through stunt staging.
    auto animation = std::static_pointer_cast<ModelSceneNode>(node)->model().getAnimation(cut.name);
    if (!animation) {
        return;
    }
    AnimationProperties properties;
    if (cut.looping) {
        properties.flags |= AnimationFlags::loop;
    }
    // Authored cutscene clips stay under dialogue ownership: a one-shot clip
    // holds its final frame instead of falling back to the state-driven idle,
    // because the authored sequence may leave entries without an AnimList
    // before the next clip takes over.
    if (creature->playExternalAnimation(animation, std::move(properties))) {
        holdCutParticipant(creature);
    }
}

void DialogGUI::applyDialogAnimation(const std::string &participant, int ordinal) {
    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return;
    }
    AnimationType animType = getDialogAnimationType(ordinal);
    if (animType != AnimationType::Invalid) {
        creature->playAnimation(animType);
    }
}

std::optional<DialogGUI::CutAnimation> DialogGUI::decodeCutAnimation(int ordinal) {
    for (auto &band : g_cutAnimationBands) {
        int offset = ordinal - band.base;
        if (offset < 0 || offset >= kCutAnimationBandSize) {
            continue;
        }
        CutAnimation cut;
        cut.name = str(boost::format("cut%03d%s") % (offset + 1) % band.suffix);
        cut.looping = band.looping;
        return cut;
    }
    return std::nullopt;
}

AnimationType DialogGUI::getDialogAnimationType(int ordinal) const {
    int index;
    if (ordinal >= kDialogAnimationBase) {
        index = ordinal - kDialogAnimationBase;
    } else if (ordinal > 0 && !_game.isTSL()) {
        index = ordinal;
    } else {
        // Cut-band ordinals never reach here. K2 lower ordinals and the zero
        // sentinel belong to no ordinary-animation namespace reone recognises.
        warn("Dialog: unsupported animation ordinal: " + std::to_string(ordinal));
        return AnimationType::Invalid;
    }
    std::shared_ptr<TwoDA> animations(_services.resource.twoDas.get("dialoganimations"));

    if (index >= animations->getRowCount()) {
        if (ordinal < kDialogAnimationBase) {
            warn("Dialog: unsupported animation ordinal: " + std::to_string(ordinal));
        } else {
            warn("Dialog: animation index out of bounds: " + std::to_string(index));
        }
        return AnimationType::Invalid;
    }

    std::string name(boost::to_lower_copy(animations->getString(index, "name")));
    auto maybeAnimType = g_animTypeByName.find(name);

    return maybeAnimType != g_animTypeByName.end() ? maybeAnimType->second : AnimationType::Invalid;
}

void DialogGUI::repositionMessage() {
    Control::Text text(_controls.LBL_MESSAGE->text());
    int top;

    if (_entryEnded) {
        text.align = Control::TextAlign::CenterBottom;
        top = -_gui->rootControl().extent().top;
    } else {
        text.align = Control::TextAlign::CenterTop;
        top = _controls.LB_REPLIES->extent().top;
    }

    _controls.LBL_MESSAGE->setText(std::move(text));
    _controls.LBL_MESSAGE->setExtentTop(top);
}

void DialogGUI::onFinish() {
    if (hasStuntPresentation()) {
        releaseStuntParticipants();
    }
    releaseHeldCutParticipants();

    // Make current speaker stop talking, if any
    auto speakerCreature = std::dynamic_pointer_cast<Creature>(_currentSpeaker);
    if (speakerCreature) {
        speakerCreature->stopTalking();
    }
}

void DialogGUI::holdCutParticipant(const std::shared_ptr<Creature> &creature) {
    auto maybeHeld = std::find(_heldCutParticipants.begin(), _heldCutParticipants.end(), creature);
    if (maybeHeld == _heldCutParticipants.end()) {
        _heldCutParticipants.push_back(creature);
    }
}

void DialogGUI::releaseHeldCutParticipants() {
    for (auto &creature : _heldCutParticipants) {
        creature->resumeStateDrivenAnimation();
    }
    _heldCutParticipants.clear();
}

void DialogGUI::releaseStuntParticipants() {
    if (!_dialog->isAnimatedCutscene()) {
        for (auto &participant : _participantByTag) {
            leaveMixedStunt(participant.second);
        }
        _participantByTag.clear();
        return;
    }
    for (auto &participant : _participantByTag) {
        participant.second.creature->resumeStateDrivenAnimation();
        participant.second.creature->stopStuntMode();
        participant.second.creature->setIsInConversation(false);
    }
    _participantByTag.clear();
}

void DialogGUI::onEntryEnded() {
    _controls.LB_REPLIES->setVisible(true);

    updateCamera();
    repositionMessage();
}

void DialogGUI::setMessage(std::string message) {
    _controls.LBL_MESSAGE->setTextMessage(message);
}

void DialogGUI::setReplyLines(std::vector<std::string> lines) {
    _controls.LB_REPLIES->clearItems();

    for (size_t i = 0; i < lines.size(); ++i) {
        ListBox::Item item;
        item.tag = std::to_string(i);
        item.text = lines[i];
        _controls.LB_REPLIES->addItem(std::move(item));
    }
}

void DialogGUI::update(float dt) {
    Conversation::update(dt);

    // Dialog camera follows the current speaker, if any
    if (_currentSpeaker && _game.cameraType() == CameraType::Dialog) {
        auto camera = _game.module()->area()->getCamera<DialogCamera>(CameraType::Dialog);
        camera->setSpeakerPosition(getTalkPosition(*_currentSpeaker));
    }
}

} // namespace game

} // namespace reone
