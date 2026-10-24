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

#include <cmath>

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/game/animations.h"
#include "reone/game/animationutil.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/panel.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/types.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

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
/** How far a participant of a conversation looks at the other of its pair. */
static constexpr float kDialogLookDistance = 10.0f;
/** A participant turning its body brings the other a degree inside its head arc. */
static constexpr float kHeadArcMarginDegrees = 1.0f;

// DLG participant animation ordinals occupy two namespaces.
//
// Ordinals at or above kDialogAnimationBase index dialoganimations.2da and name
// a semantic dialogue animation. K1 also uses valid positive 2DA rows directly.
// Recognized lower ordinal bands name a cutscene clip on the target model: the
// band selects the clip name suffix and whether the clip loops, while the
// offset within the band selects the clip number. Both namespaces are
// independent of AnimatedCut and of whether the participant is driven by a
// stunt model.
static constexpr int kDialogAnimationBase = 10000;

// The conversation bands are viewport-relative, not authored plate art:
// the subtitle sits in the top sixth and the reply list in the bottom sixth
// of whatever viewport the game is running at.
static constexpr int kBandDivisor = 6;
// Cut ordinals 1000-1327 play once and 1400-1727 loop. Each band names up to
// 128 clips; an ordinal in those ranges that names no clip plays the model's
// pause.
static constexpr int kFirstOneShotCutOrdinal = 1000;
static constexpr int kLastOneShotCutOrdinal = 1327;
static constexpr int kFirstLoopingCutOrdinal = 1400;
static constexpr int kLastLoopingCutOrdinal = 1727;
static constexpr int kCutAnimationBandSize = 128;

// The "w" bands name clips authored in world space.
static const struct CutAnimationBand {
    int base;
    const char *suffix;
    bool worldSpace;
} g_cutAnimationBands[] {
    {1000, "", false},
    {1200, "w", true},
    {1400, "l", false},
    {1600, "wl", true}};

/** The clip a cut ordinal plays on a creature: the clip it names, else the creature's pause. */
static std::string getCutClipName(const std::string &name, const Object &creature) {
    return name.empty() ? creature.getAnimationName(AnimationType::LoopingPause) : name;
}

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
    gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);
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

void DialogGUI::selectReplyForCapture(int index) {
    _controls.LB_REPLIES->setSelectedItemIndex(index);
}

int DialogGUI::bandHeight() const {
    return _game.options().graphics.height / kBandDivisor;
}

Control::Extent DialogGUI::bandExtent(int top) const {
    return {0, top, _game.options().graphics.width, bandHeight()};
}

Control::Extent DialogGUI::replySafeArea() const {
    int safeWidth = std::min(_game.options().graphics.width, _game.options().graphics.height * 4 / 3);
    int safeLeft = (_game.options().graphics.width - safeWidth) / 2;
    return {safeLeft, _game.options().graphics.height - bandHeight(), safeWidth, bandHeight()};
}

void DialogGUI::loadFrames() {
    addFrame(kControlTagTopFrame, 0);
    addFrame(kControlTagBottomFrame, _game.options().graphics.height - bandHeight());
}

void DialogGUI::addFrame(std::string tag, int top) {
    auto frame = _gui->newControl(ControlType::Panel, tag);
    frame->setExtent(bandExtent(top));
    frame->setBorderFill("blackfill");

    _gui->addControlToFront(std::move(frame), IGUI::ControlCoordinates::Screen);
}

void DialogGUI::configureMessage() {
    _controls.LBL_MESSAGE->setExtent(bandExtent(0));
    _controls.LBL_MESSAGE->setTextColor(_baseColor);
}

void DialogGUI::configureReplies() {
    // Reply prose is authored for a 4:3 dialogue safe area. Keep that area
    // centred on wider displays, but preserve the original left alignment
    // inside it so choices scan as a conventional vertical list.
    // The list's root remains full-width so its authored child coordinates
    // do not receive the safe-area offset twice. The row prototype below is
    // positioned in the 4:3 rectangle itself.
    _controls.LB_REPLIES->setExtent(bandExtent(_game.options().graphics.height - bandHeight()));
    // The authored list reserves a scroll-bar column against its left edge,
    // with the row prototype indented past it. Recreate that column at the
    // safe area's left edge: the list is moved into the band by the extent
    // override above, and no layout pass carries its scroll bar along, so
    // without this the bar would render at its raw authored coordinates in
    // the screen's top-left corner whenever the replies overflow the band.
    // The bar and the row indent share the dialogue text scale, not the
    // layout factor: the rows draw their prose at that scale, and the
    // authored proportion is a bar as wide as a row is tall.
    if (auto scrollBar = _controls.LB_REPLIES->scrollBarOrNull()) {
        auto safeArea = replySafeArea();
        scrollBar->setExtent({
            safeArea.left,
            safeArea.top,
            static_cast<int>(std::lround(scrollBar->authoredExtent().width * _controls.LBL_MESSAGE->scale())),
            safeArea.height});
    }
    _controls.LB_REPLIES->setProtoMatchContent(true);
    _controls.LB_REPLIES->protoItem().setTextFont(_controls.LBL_MESSAGE->text().font);
    _controls.LB_REPLIES->protoItem().setScale(_controls.LBL_MESSAGE->scale());
    _controls.LB_REPLIES->protoItem().setTextAlignment(Control::TextAlign::LeftCenter);
    _controls.LB_REPLIES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_REPLIES->protoItem().setTextColor(_baseColor);
}

void DialogGUI::onStart() {
    _currentSpeaker = owner();
    endSpeakerPair();
    _previousAnimations.clear();
    _cutParticipants.clear();
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
            creature->setIsInConversation(true);
        }

        _participantByTag.insert(std::make_pair(stunt.participant, std::move(participant)));
    }
}

bool DialogGUI::hasStuntPresentation() const {
    return _dialog->isAnimatedCutscene() || !_dialog->stunts.empty();
}

std::shared_ptr<Creature> DialogGUI::resolveParticipantCreature(const std::string &participant) const {
    if (participant == kObjectTagOwner) {
        return std::dynamic_pointer_cast<Creature>(owner());
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
    if (maybeParticipant == _participantByTag.end()) {
        return nullptr;
    }
    auto creature = maybeParticipant->second.creature.resolve();
    return maybeParticipant->second.model->getAnimation(creature ? getCutClipName(cut->name, *creature) : cut->name);
}

void DialogGUI::onLoadEntry() {
    restoreInactiveStuntParticipants();
    loadCurrentSpeaker();
    updateParticipantAnimations();
    orientSpeakerPair();
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
    auto creature = participant.creature.resolve();
    if (!creature) {
        participant.mixedStuntActive = false;
        return false;
    }
    if (!participant.mixedStuntActive && creature->isStuntMode()) {
        warn("Dialog: participant is already in stunt mode: " + creature->tag());
        return false;
    }

    AnimationProperties properties;
    properties.flags = AnimationFlags::propagate | (looping ? AnimationFlags::loop : 0);
    properties.scale = 1.0f;
    if (!creature->playExternalAnimation(animation, std::move(properties))) {
        return false;
    }

    if (!participant.mixedStuntActive) {
        participant.restorePosition = creature->position();
        participant.restoreFacing = creature->getFacing();
        if (auto node = creature->sceneNode()) {
            participant.restoreCulling = node->isCullingEnabled();
        }
        creature->startStuntMode();
        participant.mixedStuntActive = true;
    }
    return true;
}

void DialogGUI::leaveMixedStunt(Participant &participant) {
    if (!participant.mixedStuntActive) {
        return;
    }
    auto creature = participant.creature.resolve();
    if (!creature) {
        participant.mixedStuntActive = false;
        return;
    }
    creature->resumeStateDrivenAnimation();
    creature->setPosition(participant.restorePosition);
    creature->setFacing(participant.restoreFacing);
    creature->stopStuntMode();
    if (auto node = creature->sceneNode()) {
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
        speaker = owner();
    }

    // Make previous speaker stop talking, if any
    auto previousSpeaker = _currentSpeaker.resolve();
    if (previousSpeaker && previousSpeaker != speaker) {
        auto speakerCreature =
            std::dynamic_pointer_cast<Creature>(previousSpeaker);
        if (speakerCreature) {
            speakerCreature->stopTalking();
        }
    }
    _currentSpeaker = speaker;
    updateSpeakerPair(speaker);

    if (auto speakerCreature = std::dynamic_pointer_cast<Creature>(speaker)) {
        speakerCreature->startTalking(_lipAnimation);
    }
}

// The entry's listener: none, the owner, the player, or an object by tag.
std::shared_ptr<Object> DialogGUI::resolveEntryListener() const {
    const std::string &listener = _currentEntry->listener;
    if (listener.empty()) {
        return nullptr;
    }
    if (boost::iequals(listener, kObjectTagOwner)) {
        return owner();
    }
    if (boost::iequals(listener, kObjectTagPlayer)) {
        return _game.party().player();
    }
    return _game.module()->area()->getObjectByTag(boost::to_lower_copy(listener));
}

// The first entry's listener defaults to whoever started the conversation, or
// else the player. Later entries without a listener answer the previous speaker when the
// speaker changed, and otherwise that same partner.
void DialogGUI::updateSpeakerPair(const std::shared_ptr<Object> &speaker) {
    std::shared_ptr<Object> partner = _partner.resolve();
    if (!partner) {
        partner = _game.party().player();
    }
    auto listener = resolveEntryListener();
    auto previousSpeaker = _pairSpeaker.resolve();
    _previousPairListener = _pairListener;
    if (!_pairStarted) {
        _pairStarted = true;
        _pairListener = listener ? listener : partner;
    } else if (!listener) {
        _pairListener = previousSpeaker != speaker ? previousSpeaker : partner;
    } else {
        _pairListener = listener;
    }
    _previousPairSpeaker = _pairSpeaker;
    _pairSpeaker = speaker;
}

// The previous pair stop looking; then the listener turns to the speaker and
// the speaker to the listener. Camera angle 5 keeps the previous pair. An
// animated camera shot leaves everyone as they are.
void DialogGUI::orientSpeakerPair() {
    if (isAnimatedCameraShot()) {
        return;
    }
    for (auto &reference : {_previousPairSpeaker, _previousPairListener}) {
        if (auto creature = std::dynamic_pointer_cast<Creature>(reference.resolve())) {
            creature->lookAt(nullptr, 0.0f);
        }
    }
    if (keepsPreviousSpeakerPair() && _previousPairSpeaker.resolve()) {
        _pairSpeaker = _previousPairSpeaker;
        _pairListener = _previousPairListener;
    }
    auto speaker = _pairSpeaker.resolve();
    auto listener = _pairListener.resolve();
    if (!speaker || !listener) {
        return;
    }
    turnToward(*listener, speaker);
    turnToward(*speaker, listener);
}

// Within the head arc only the head turns; beyond it the body turns until
// the other is a degree past the arc, and the head does the rest. A creature
// whose head-follow is locked, or whose head cannot look, faces the other.
void DialogGUI::turnToward(Object &turner, const std::shared_ptr<Object> &other) {
    auto *creature = dyn_cast<Creature>(&turner);
    if (!creature || _game.isDialogOrientationLocked(turner.id()) || creature->walkSpeed() <= 0.0f) {
        return;
    }
    // A participant turned by the conversation drops its orientation lock.
    creature->setOrientationLock(script::kObjectInvalid);
    bool head = false;
    float facing = turner.getFacing();
    if (!_game.isDialogHeadFollowLocked(turner.id())) {
        const glm::vec3 offset {other->position() - turner.position()};
        const glm::vec3 forward {-std::sin(facing), std::cos(facing), 0.0f};
        float angle = 0.0f;
        if (glm::length(offset) > 0.0f) {
            const float cosine = glm::clamp(glm::dot(glm::normalize(offset), forward), -1.0f, 1.0f);
            angle = glm::degrees(std::acos(cosine));
        }
        const float arc = creature->headTurnHorizontal();
        head = true;
        if (angle > arc) {
            float delta = glm::radians(angle - (arc + kHeadArcMarginDegrees));
            if (offset.x * forward.y - offset.y * forward.x > 0.0f) {
                delta = -delta;
            }
            facing += delta;
        }
    }
    if (head && creature->lookAt(other, kDialogLookDistance)) {
        turner.setFacing(facing);
    } else {
        turner.face(*other);
    }
}

bool DialogGUI::isSpeakerOrListener(const Object &object) const {
    return _pairSpeaker.resolve().get() == &object || _pairListener.resolve().get() == &object;
}

void DialogGUI::endSpeakerPair() {
    for (auto &reference : {_pairSpeaker, _pairListener}) {
        if (auto creature = std::dynamic_pointer_cast<Creature>(reference.resolve())) {
            creature->lookAt(nullptr, 0.0f);
        }
    }
    _pairSpeaker.reset();
    _pairListener.reset();
    _previousPairSpeaker.reset();
    _previousPairListener.reset();
    _pairStarted = false;
}

// Ordinals 1000-1727 play on the dialog's camera model; any other entry uses
// the dialog camera.
void DialogGUI::updateCamera() {
    std::shared_ptr<Area> area(_game.module()->area());

    if (!isAnimatedCameraShot()) {
        std::shared_ptr<Creature> player(_game.party().player());
        glm::vec3 listenerPosition(player ? getTalkPosition(*player) : glm::vec3(0.0f));
        auto speaker = _currentSpeaker.resolve();
        glm::vec3 speakerPosition(
            speaker ? getTalkPosition(*speaker) : glm::vec3(0.0f));
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
    //
    // A participant keeping the previous entry's animation, or one that cannot
    // play dialog animations, is left as it is. One given a new dialog pose
    // stops looking and may look again; a world-space cut clip then suspends
    // its head look.
    std::map<std::string, int> animations;
    for (auto &anim : _currentEntry->animations) {
        animations[anim.participant] = anim.animation;
        auto previous = _previousAnimations.find(anim.participant);
        if (previous != _previousAnimations.end() && previous->second == anim.animation) {
            continue;
        }
        auto creature = participantCreature(anim.participant);
        if (creature && !canPlayDialogAnimations(*creature)) {
            continue;
        }
        if (creature && _services.game.animations.isDialogAnimation(anim.animation)) {
            creature->setHeadLookSuspended(false);
            creature->lookAt(nullptr, 0.0f);
        }
        auto cut = decodeCutAnimation(anim.animation);
        if (cut) {
            applyCutAnimation(anim.participant, *cut);
        } else {
            applyDialogAnimation(anim.participant, anim.animation);
        }
        if (cut && cut->worldSpace && creature) {
            creature->setHeadLookSuspended(true);
        }
    }
    _previousAnimations = std::move(animations);
}

std::shared_ptr<Creature> DialogGUI::participantCreature(const std::string &participant) const {
    auto stunt = _participantByTag.find(participant);
    if (stunt != _participantByTag.end()) {
        return stunt->second.creature.resolve();
    }
    return resolveParticipantCreature(participant);
}

bool DialogGUI::canPlayDialogAnimations(const Creature &creature) const {
    if (creature.isDebilitated() || creature.isDead()) {
        return false;
    }
    return !_game.party().isMember(creature) || creature.currentHitPoints() > 0;
}

void DialogGUI::applyCutAnimation(const std::string &participant, const CutAnimation &cut) {
    auto maybeParticipant = _participantByTag.find(participant);
    if (maybeParticipant != _participantByTag.end()) {
        Participant &stunt = maybeParticipant->second;
        auto stuntCreature = stunt.creature.resolve();
        const std::string name = stuntCreature ? getCutClipName(cut.name, *stuntCreature) : cut.name;
        if (auto animation = stunt.model->getAnimation(name)) {
            if (_dialog->isAnimatedCutscene()) {
                AnimationProperties properties;
                properties.flags = AnimationFlags::propagate | (cut.looping ? AnimationFlags::loop : 0);
                properties.scale = 1.0f;
                if (stuntCreature) {
                    stuntCreature->playExternalAnimation(
                        animation, std::move(properties));
                }
            } else {
                enterMixedStunt(stunt, animation, cut.looping);
            }
            return;
        }
        // The stunt model is the authored source for this participant, so a
        // missing clip is a data problem rather than a reason to silently
        // animate from somewhere else. Staged participants also sit at the
        // stunt origin, where an in-place clip would play in the wrong place.
        warn("Dialog: stunt model has no animation: " + name);
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
    auto animation = std::static_pointer_cast<ModelSceneNode>(node)->model().getAnimation(
        getCutClipName(cut.name, *creature));
    if (!animation) {
        return;
    }
    AnimationProperties properties;
    if (cut.looping) {
        properties.flags |= AnimationFlags::loop;
    }
    // A one-shot clip returns the creature to its pause when it ends; a
    // looping clip stays until another animation replaces it or the
    // conversation ends.
    if (creature->playExternalAnimation(animation, std::move(properties))) {
        addCutParticipant(creature);
    }
}

void DialogGUI::applyDialogAnimation(const std::string &participant, int ordinal) {
    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return;
    }
    const auto pose = getDialogPose(ordinal);
    if (pose.type != AnimationType::Invalid) {
        // The pose is known by its dialog animation ID; lower ordinals index
        // the dialog animations too.
        const int id = ordinal >= kDialogAnimationBase ? ordinal : kDialogAnimationBase + ordinal;
        creature->playDialogAnimation(pose.type, AnimationSource {id, pose.dialog});
    }
}

std::optional<DialogGUI::CutAnimation> DialogGUI::decodeCutAnimation(int ordinal) {
    CutAnimation cut;
    if (ordinal >= kFirstLoopingCutOrdinal && ordinal <= kLastLoopingCutOrdinal) {
        cut.looping = true;
    } else if (ordinal < kFirstOneShotCutOrdinal || ordinal > kLastOneShotCutOrdinal) {
        return std::nullopt;
    }
    for (auto &band : g_cutAnimationBands) {
        int offset = ordinal - band.base;
        if (offset >= 0 && offset < kCutAnimationBandSize) {
            cut.name = str(boost::format("cut%03d%s") % (offset + 1) % band.suffix);
            cut.worldSpace = band.worldSpace;
            break;
        }
    }
    return cut;
}

AnimationType DialogGUI::getDialogAnimationType(int ordinal) const {
    return getDialogPose(ordinal).type;
}

DialogGUI::DialogPose DialogGUI::getDialogPose(int ordinal) const {
    int index;
    if (ordinal >= kDialogAnimationBase) {
        index = ordinal - kDialogAnimationBase;
    } else if (ordinal > 0 && !_game.isTSL()) {
        index = ordinal;
    } else {
        // Cut-band ordinals never reach here. K2 lower ordinals and the zero
        // sentinel belong to no recognised ordinary-animation namespace.
        warn("Dialog: unsupported animation ordinal: " + std::to_string(ordinal));
        return {};
    }
    const auto row = _services.game.animations.findDialogAnimation(index);
    if (!row) {
        if (ordinal < kDialogAnimationBase) {
            warn("Dialog: unsupported animation ordinal: " + std::to_string(ordinal));
        } else {
            warn("Dialog: animation index out of bounds: " + std::to_string(index));
        }
        return {};
    }

    auto maybeAnimType = g_animTypeByName.find(row->get().name);
    DialogPose pose;
    pose.type = maybeAnimType != g_animTypeByName.end() ? maybeAnimType->second : AnimationType::Invalid;
    pose.dialog = row->get().dialog;
    return pose;
}

void DialogGUI::repositionMessage() {
    Control::Text text(_controls.LBL_MESSAGE->text());
    int top;

    if (_entryEnded) {
        text.align = Control::TextAlign::CenterBottom;
        top = 0;
    } else {
        text.align = Control::TextAlign::CenterTop;
        top = _controls.LB_REPLIES->extent().top;
    }

    _controls.LBL_MESSAGE->setText(std::move(text));
    _controls.LBL_MESSAGE->setExtentTop(top);
}

void DialogGUI::onFinish() {
    // The last speaker and listener stop looking; then the last posed
    // participants may look again.
    endSpeakerPair();
    for (const auto &[participant, ordinal] : _previousAnimations) {
        auto creature = participantCreature(participant);
        if (creature && canPlayDialogAnimations(*creature)) {
            creature->setHeadLookSuspended(false);
        }
    }
    _previousAnimations.clear();

    if (hasStuntPresentation()) {
        releaseStuntParticipants();
    }
    releaseCutParticipants();

    // Make current speaker stop talking, if any
    auto speakerCreature =
        std::dynamic_pointer_cast<Creature>(_currentSpeaker.resolve());
    if (speakerCreature) {
        speakerCreature->stopTalking();
    }
}

void DialogGUI::addCutParticipant(const std::shared_ptr<Creature> &creature) {
    auto maybeTracked = std::find_if(
        _cutParticipants.begin(), _cutParticipants.end(),
        [&creature](const auto &tracked) {
            return tracked.resolve() == creature;
        });
    if (maybeTracked == _cutParticipants.end()) {
        _cutParticipants.push_back(creature);
    }
}

void DialogGUI::releaseCutParticipants() {
    for (auto &reference : _cutParticipants) {
        if (auto creature = reference.resolve()) {
            creature->resumeStateDrivenAnimation();
        }
    }
    _cutParticipants.clear();
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
        auto creature = participant.second.creature.resolve();
        if (!creature) continue;
        creature->resumeStateDrivenAnimation();
        creature->stopStuntMode();
        creature->setIsInConversation(false);
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
        if (lines[i].empty()) {
            continue;
        }
        ListBox::Item item;
        item.tag = std::to_string(i);
        item.text = lines[i];
        _controls.LB_REPLIES->addItem(std::move(item));
    }
    // Replies start at the top-left of the centred 4:3 safe area within the
    // bottom band, indented past the scroll-bar column by their authored
    // offset so an overflowing list shows its bar beside the prose, not
    // under it. K1 authors the rows flush against the bar, which reads as
    // touching; hold them clear of it by the gap TSL authors, which leaves
    // TSL's own indent unchanged. The list root stays full-width so the
    // offset is applied exactly once to its row prototype.
    static constexpr int kScrollBarTextGap = 8;
    auto extent = _controls.LB_REPLIES->protoItem().extent();
    const auto &band = _controls.LB_REPLIES->extent();
    auto safeArea = replySafeArea();
    float textScale = _controls.LBL_MESSAGE->scale();
    int indent = static_cast<int>(std::lround(
        (_controls.LB_REPLIES->protoItem().authoredExtent().left -
         _controls.LB_REPLIES->authoredExtent().left) *
        textScale));
    if (auto scrollBar = _controls.LB_REPLIES->scrollBarOrNull()) {
        indent = std::max(
            indent,
            scrollBar->extent().width + static_cast<int>(std::lround(kScrollBarTextGap * textScale)));
    }
    extent.left = safeArea.left + indent;
    extent.width = safeArea.width - indent;
    extent.top = band.top;
    _controls.LB_REPLIES->protoItem().setExtent(std::move(extent));
}

void DialogGUI::update(float dt) {
    Conversation::update(dt);

    // Dialog camera follows the current speaker, if any
    auto speaker = _currentSpeaker.resolve();
    if (speaker && _game.cameraType() == CameraType::Dialog) {
        auto camera = _game.module()->area()->getCamera<DialogCamera>(CameraType::Dialog);
        camera->setSpeakerPosition(getTalkPosition(*speaker));
    }
}

} // namespace game

} // namespace reone
