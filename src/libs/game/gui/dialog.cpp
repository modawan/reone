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
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/panel.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/types.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/logutil.h"

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

// DLG participant animation ordinals occupy three namespaces, whatever the
// conversation's AnimatedCut and whether a stunt model drives the participant.
//
// Ordinals from kDialogAnimationBase index dialoganimations.2da and name a
// semantic dialogue animation. Ordinals in the cut bands name a cutscene clip
// on the target model: the band selects the clip name suffix and whether the
// clip loops, while the offset within the band selects the clip number. Any
// other ordinal is an animations.2da row. Only a looping or fire-and-forget
// dialog animation is played.
static constexpr int kDialogAnimationBase = 10000;
static constexpr int kPauseAnimationId = 10000;
// The speaker pair's poses: the listener listens and the speaker talks;
// injured, each its injured pose.
static constexpr int kListenAnimationId = 10030;
static constexpr int kTalkAnimationId = 10038;
static constexpr int kTalkInjuredAnimationId = 10154;
static constexpr int kListenInjuredAnimationId = 10155;

// The conversation bands are viewport-relative, not authored plate art:
// the subtitle sits in the top sixth and the reply list in the bottom sixth
// of whatever viewport the game is running at.
static constexpr int kBandDivisor = 6;
// Cut ordinals 1000-1327 play once and 1400-1727 loop. Each band names up to
// 128 clips; an ordinal in those ranges that names no clip plays the model's
// pause.
static constexpr int kFirstLoopingCutOrdinal = 1400;

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
    // The selected reply stays selected when the pointer moves off it.
    _controls.LB_REPLIES->setKeepsSelection(true);
    _controls.LB_REPLIES->protoItem().setTextFont(_controls.LBL_MESSAGE->text().font);
    _controls.LB_REPLIES->protoItem().setScale(_controls.LBL_MESSAGE->scale());
    _controls.LB_REPLIES->protoItem().setTextAlignment(Control::TextAlign::LeftCenter);
    _controls.LB_REPLIES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_REPLIES->protoItem().setTextColor(_baseColor);
}

void DialogGUI::render() {
    const EntryFade &fade = entryFade();
    if (fade.alpha > 0.0f) {
        _game.renderScreenFill(fade.color, fade.alpha);
    }
    Conversation::render();
}

void DialogGUI::onStart() {
    // The letterbox shows with the conversation, without a fade.
    resetEntryFade();
    _currentSpeaker = owner();
    endSpeakerPair();
    _previousAnimations.clear();
    _lastButOneAnimations.clear();
    _waitedParticipants.clear();
    loadStuntParticipants();

    _dialogCameraStale = true;
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

        // A participant of an animated cutscene stands where it is; each of
        // its clips authored in world space puts it at the world origin.
        if (_dialog->isAnimatedCutscene()) {
            creature->setIsInConversation(true);
        }

        _participantByTag.insert(std::make_pair(stunt.participant, std::move(participant)));
    }
}

bool DialogGUI::hasStuntPresentation() const {
    return _dialog->isAnimatedCutscene() || !_dialog->stunts.empty();
}

std::shared_ptr<Object> DialogGUI::resolveParticipantObject(const std::string &participant) const {
    if (participant == kObjectTagOwner) {
        return owner();
    }
    if (boost::iequals(participant, kObjectTagPlayer)) {
        return _game.party().player();
    }
    return _game.module()->area()->getObjectByTag(participant);
}

std::shared_ptr<Creature> DialogGUI::resolveParticipantCreature(const std::string &participant) const {
    return std::dynamic_pointer_cast<Creature>(resolveParticipantObject(participant));
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
    restoreInactiveStuntParticipants(*_currentEntry);
    loadCurrentSpeaker();
    updateParticipantAnimations(*_currentEntry);
    // An animated camera shot leaves everyone as they are.
    if (!isAnimatedCameraShot()) {
        orientSpeakerPair(*_currentEntry);
    }
    updateCamera();
    repositionMessage();

    _controls.LB_REPLIES->setVisible(false);
}

void DialogGUI::restoreInactiveStuntParticipants(const Dialog::EntryReply &node) {
    if (_dialog->isAnimatedCutscene()) {
        return;
    }
    for (auto &entry : _participantByTag) {
        if (!entry.second.mixedStuntActive) {
            continue;
        }
        bool drivenThisEntry = false;
        for (auto &anim : node.animations) {
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

bool DialogGUI::enterMixedStunt(Participant &participant, const std::shared_ptr<Animation> &animation, bool looping, int animationId) {
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
    if (!creature->playExternalAnimation(animation, std::move(properties), AnimationSource {animationId, true})) {
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
// the speaker to the listener. Camera angle 5 keeps the previous pair.
void DialogGUI::orientSpeakerPair(const Dialog::EntryReply &shot) {
    for (auto &reference : {_previousPairSpeaker, _previousPairListener}) {
        if (auto creature = std::dynamic_pointer_cast<Creature>(reference.resolve())) {
            creature->lookAt(nullptr, 0.0f);
        }
    }
    if (keepsPreviousSpeakerPair(shot) && _previousPairSpeaker.resolve()) {
        _pairSpeaker = _previousPairSpeaker;
        _pairListener = _previousPairListener;
    }
    if (_dialog->isAnimatedCutscene()) {
        return;
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

// While the replies are listed the one who answers is the speaker and the
// entry's speaker the listener; nothing is posed until a reply is chosen. A
// sole reply taken when the entry ends changes nothing.
void DialogGUI::passTurnToReplySpeaker() {
    _previousPairListener = _pairListener;
    _pairListener = _pairSpeaker;
    _previousPairSpeaker = _pairSpeaker;
    _pairSpeaker = replySpeaker();
    if (auto first = firstReply()) {
        orientSpeakerPair(first->get());
        takeCameraShot(first->get(), firstReplyCameraAngle());
        frameShot(first->get(), firstReplyCameraAngle());
    }
}

// The player character answers while it is in the party, unless a script
// asked for the leader; otherwise the leader answers, or the player character
// when the party has none.
std::shared_ptr<Object> DialogGUI::replySpeaker() const {
    auto &party = _game.party();
    auto player = party.player();
    if (player && party.isMember(*player) && !_game.leaderAnswersInDialog()) {
        return player;
    }
    if (auto leader = party.getLeader()) {
        return leader;
    }
    return player;
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

// Ordinals 1000-1727 play on the dialog's camera model; any other entry
// frames its shot.
void DialogGUI::updateCamera() {
    if (!isAnimatedCameraShot()) {
        frameShot(*_currentEntry, entryCameraAngle());
        return;
    }
    _dialogCameraStale = true;
    auto camera = _game.module()->area()->getCamera<AnimatedCamera>(CameraType::Animated);
    // A shot without a field of view of its own keeps the view's.
    if (_currentEntry->camFieldOfView > 0.0f) {
        camera->setFieldOfView(_currentEntry->camFieldOfView);
    } else if (const Camera *view = _game.getActiveCamera(); view != camera) {
        camera->setFieldOfView(view->viewAngle());
    }
    camera->playAnimation(_currentEntry->cameraAnimation);
}

// The dialog camera starts afresh at the conversation's first shot and after
// each animated shot. Angle 5 keeps the view as it is, and a placed camera
// (angle 6) shows itself; any other angle frames the speaker pair.
void DialogGUI::frameShot(const Dialog::EntryReply &shot, int angle) {
    static constexpr int kSamePairAngle = 5;
    static constexpr int kPlacedAngle = 6;
    auto area = _game.module()->area();
    auto camera = area->getCamera<DialogCamera>(CameraType::Dialog);
    if (_dialogCameraStale) {
        camera->startConversation(_dialog->oldHitCheck);
        _dialogCameraStale = false;
    }
    if (angle == kSamePairAngle) {
        return;
    }
    if (angle == kPlacedAngle) {
        // In place of a placed camera the area lacks, the dialog camera looks
        // straight down from the world origin.
        area->setStaticCamera(shot.cameraId);
        const Camera *placed = area->getCamera(CameraType::Static);
        if (placed && placed->cameraId() == shot.cameraId) {
            camera->takePlacedShot(placed->position());
        } else {
            showShotOnDialogCamera();
            camera->takePlacedShot(glm::vec3(0.0f));
        }
        return;
    }
    float pullBack = 0.0f;
    if (angle == DialogCamera::kFaceAngle) {
        pullBack = faceShotPullBack();
    } else {
        _faceShotPullBack = 0.0f;
    }
    camera->setShot(_pairSpeaker.resolve(), _pairListener.resolve(), {angle, shot.camHeightOffset, shot.tarHeightOffset, pullBack});
}

// The speaker's animation in the list (talk when the list names none) sets
// the pull-back when it is a dialog animation: its dialoganimations.2da row's,
// and in TSL a quarter metre more for a Gran. A pull-back once set holds for
// the face shots that follow until a shot sets none or another shot comes.
float DialogGUI::faceShotPullBack() {
    static constexpr int kTalkAnimation = 10038;
    static constexpr int kDialogAnimationBase = 10000;
    static constexpr float kNoPullBack = 0.001f;
    static constexpr int kGranAppearance = 565;
    static constexpr float kGranPullBack = 0.25f;
    auto speaker = _pairSpeaker.resolve();
    int ordinal = kTalkAnimation;
    for (const auto &[participant, animation] : _previousAnimations) {
        if (speaker && participantObject(participant) == speaker) {
            ordinal = animation;
            break;
        }
    }
    float pullBack = 0.0f;
    auto &animations = _services.game.animations;
    if (animations.isDialogAnimation(ordinal)) {
        if (auto row = animations.findDialogAnimation(ordinal - kDialogAnimationBase)) {
            pullBack = row->get().pullBack;
        }
        if (std::abs(pullBack) < kNoPullBack) {
            pullBack = 0.0f;
        }
        auto creature = std::dynamic_pointer_cast<Creature>(speaker);
        if (_game.isTSL() && creature && creature->appearance() == kGranAppearance) {
            pullBack += kGranPullBack;
        }
        if (pullBack == 0.0f) {
            _faceShotPullBack = 0.0f;
        }
    }
    if (_faceShotPullBack > 0.0f) {
        return _faceShotPullBack;
    }
    _faceShotPullBack = pullBack;
    return pullBack;
}

void DialogGUI::updateParticipantAnimations(const Dialog::EntryReply &node) {
    // Each authored animation is resolved on its own. The ordinal decides which
    // animation is meant, the participant decides which model plays it, and a
    // single list may drive stunt-bound participants, ordinary area creatures
    // and placeables side by side.
    //
    // Participants of the previous list that this one leaves out are released
    // first, and the previous speaker pair returns to its pause. A participant
    // keeping the previous list's animation, one that cannot play dialog
    // animations, or one given an ordinal that is no dialog animation is left
    // as it is. One given a new dialog pose stops looking and may look again;
    // a world-space cut clip it then plays suspends its head look. Last, the
    // listener and the speaker take their poses unless the list names them.
    std::map<std::string, int> animations;
    std::vector<std::shared_ptr<Object>> listed;
    _waitedParticipants.clear();
    for (auto &anim : node.animations) {
        _waitedParticipants.push_back(anim.participant);
        animations[anim.participant] = anim.animation;
        if (auto object = participantObject(anim.participant)) {
            listed.push_back(std::move(object));
        }
    }
    const auto named = participantObjects(_previousAnimations);
    std::vector<std::shared_ptr<Object>> dropped;
    for (const auto &object : named) {
        if (std::find(listed.begin(), listed.end(), object) == listed.end()) {
            dropped.push_back(object);
        }
    }
    releaseParticipants(dropped);
    returnPreviousPairToPause(named);
    for (auto &anim : node.animations) {
        auto previous = _previousAnimations.find(anim.participant);
        if (previous != _previousAnimations.end() && previous->second == anim.animation) {
            continue;
        }
        auto object = participantObject(anim.participant);
        auto creature = std::dynamic_pointer_cast<Creature>(object);
        if (creature && !canPlayDialogAnimations(*creature)) {
            continue;
        }
        auto cut = decodeCutAnimation(anim.animation);
        if (!cut && !_services.game.animations.isDialogAnimation(anim.animation)) {
            continue;
        }
        // A new animation from the list releases a world-space one.
        if (object) {
            object->releaseCutsceneAnimation();
        }
        if (auto placeable = std::dynamic_pointer_cast<Placeable>(object)) {
            applyPlaceableAnimation(*placeable, anim.animation, cut);
            continue;
        }
        if (creature) {
            creature->setHeadLookSuspended(false);
            creature->lookAt(nullptr, 0.0f);
        }
        if (!cut) {
            applyDialogAnimation(anim.participant, anim.animation);
            continue;
        }
        // A world-space clip suspends the head look: a looping one once it
        // plays, a one-shot whether or not the model has it.
        const bool played = applyCutAnimation(anim.participant, *cut);
        if (cut->worldSpace && creature && (played || !cut->looping)) {
            creature->setHeadLookSuspended(true);
        }
    }
    poseSpeakerPair(listed);
    _lastButOneAnimations = std::move(_previousAnimations);
    _previousAnimations = std::move(animations);
}

std::vector<std::shared_ptr<Object>> DialogGUI::participantObjects(const std::map<std::string, int> &animations) const {
    std::vector<std::shared_ptr<Object>> objects;
    for (const auto &[participant, ordinal] : animations) {
        if (auto object = participantObject(participant)) {
            objects.push_back(std::move(object));
        }
    }
    return objects;
}

// A released creature may look again and returns to its pause, unless it
// cannot play dialog animations; a released placeable shows its own state
// again.
void DialogGUI::releaseParticipants(const std::vector<std::shared_ptr<Object>> &participants) {
    for (const auto &object : participants) {
        if (auto placeable = std::dynamic_pointer_cast<Placeable>(object)) {
            placeable->releaseCutsceneAnimation();
            placeable->showStateAnimation();
            continue;
        }
        auto creature = std::dynamic_pointer_cast<Creature>(object);
        if (!creature || !canPlayDialogAnimations(*creature)) {
            continue;
        }
        creature->releaseCutsceneAnimation();
        creature->setHeadLookSuspended(false);
        creature->showDialogPause();
    }
}

// The previous listener, then the previous speaker, return to their pause,
// unless the previous list named them, whether or not the current one keeps
// them.
void DialogGUI::returnPreviousPairToPause(const std::vector<std::shared_ptr<Object>> &named) {
    for (auto &reference : {_previousPairListener, _previousPairSpeaker}) {
        auto object = reference.resolve();
        if (object && std::find(named.begin(), named.end(), object) == named.end()) {
            returnToPause(*object);
        }
    }
}

// A creature that can play dialog animations shows its pause; a placeable
// shows its default pose.
void DialogGUI::returnToPause(Object &object) const {
    if (auto *placeable = dyn_cast<Placeable>(&object)) {
        placeable->playDialogAnimation(kPauseAnimationId, true);
        return;
    }
    auto *creature = dyn_cast<Creature>(&object);
    if (creature && canPlayDialogAnimations(*creature)) {
        creature->showDialogPause();
    }
}

// A pair creature that can play dialog animations first ends its one-shot,
// unless it shows its pause, then loops its pose.
void DialogGUI::poseSpeakerPair(const std::vector<std::shared_ptr<Object>> &listed) {
    const auto pose = [&listed, this](const std::shared_ptr<Object> &object, int id, int injuredId) {
        auto creature = std::dynamic_pointer_cast<Creature>(object);
        if (!creature || std::find(listed.begin(), listed.end(), object) != listed.end() ||
            !canPlayDialogAnimations(*creature)) {
            return;
        }
        if (creature->currentAnimationId() != kPauseAnimationId) {
            creature->abortFireForgetAnimation();
        }
        // A pose the conversation gives counts as a dialog animation.
        auto animation = creature->getScriptAnimation(creature->isInjured() ? injuredId : id);
        animation.source.dialog = true;
        creature->playScriptAnimation(animation, 1.0f);
    };
    pose(_pairListener.resolve(), kListenAnimationId, kListenInjuredAnimationId);
    pose(_pairSpeaker.resolve(), kTalkAnimationId, kTalkInjuredAnimationId);
}

// A placeable plays a cut clip as its band has it, and a dialog animation as
// its row has it: a loop, once, or a loop and then once.
void DialogGUI::applyPlaceableAnimation(Placeable &placeable, int ordinal, const std::optional<CutAnimation> &cut) {
    if (cut) {
        placeable.playDialogAnimation(ordinal, cut->looping);
        return;
    }
    bool looping = false;
    bool fireForget = false;
    if (ordinal < kDialogAnimationBase) {
        const auto row = static_cast<uint32_t>(ordinal);
        looping = _services.game.animations.isLoopingById(row);
        fireForget = _services.game.animations.isFireForgetById(row);
    } else if (auto row = _services.game.animations.findDialogAnimation(ordinal - kDialogAnimationBase)) {
        looping = row->get().looping;
        fireForget = row->get().fireForget;
    }
    if (looping) {
        placeable.playDialogAnimation(ordinal, true);
    }
    if (fireForget) {
        placeable.playDialogAnimation(ordinal, false);
    }
}

std::shared_ptr<Object> DialogGUI::participantObject(const std::string &participant) const {
    auto stunt = _participantByTag.find(participant);
    if (stunt != _participantByTag.end()) {
        return stunt->second.creature.resolve();
    }
    return resolveParticipantObject(participant);
}

std::shared_ptr<Creature> DialogGUI::participantCreature(const std::string &participant) const {
    return std::dynamic_pointer_cast<Creature>(participantObject(participant));
}

bool DialogGUI::canPlayDialogAnimations(const Creature &creature) const {
    if (creature.isDebilitated() || creature.isDead()) {
        return false;
    }
    return !_game.party().isMember(creature) || creature.currentHitPoints() > 0;
}

bool DialogGUI::applyCutAnimation(const std::string &participant, const CutAnimation &cut) {
    auto maybeParticipant = _participantByTag.find(participant);
    if (maybeParticipant != _participantByTag.end()) {
        Participant &stunt = maybeParticipant->second;
        auto stuntCreature = stunt.creature.resolve();
        const std::string name = stuntCreature ? getCutClipName(cut.name, *stuntCreature) : cut.name;
        if (auto animation = stunt.model->getAnimation(name)) {
            if (!_dialog->isAnimatedCutscene()) {
                return enterMixedStunt(stunt, animation, cut.looping, cut.id);
            }
            // The clip's ID decides whether it plays at the world origin.
            AnimationProperties properties;
            properties.flags = AnimationFlags::propagate | (cut.looping ? AnimationFlags::loop : 0);
            properties.scale = 1.0f;
            return stuntCreature && stuntCreature->playExternalAnimation(animation, std::move(properties), AnimationSource {cut.id, true});
        }
        // The stunt model is the authored source for this participant, so a
        // missing clip is a data problem rather than a reason to silently
        // animate from somewhere else.
        warn("Dialog: stunt model has no animation: " + name);
        return false;
    }

    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return false;
    }
    auto node = creature->sceneNode();
    if (!node || node->type() != SceneNodeType::Model) {
        return false;
    }
    // Cut clips authored without the world-space suffix live on the creature's
    // own model, so they play in place rather than through stunt staging.
    auto animation = std::static_pointer_cast<ModelSceneNode>(node)->model().getAnimation(
        getCutClipName(cut.name, *creature));
    if (!animation) {
        return false;
    }
    AnimationProperties properties;
    if (cut.looping) {
        properties.flags |= AnimationFlags::loop;
    }
    // A one-shot clip returns the creature to its pause when it ends; a
    // looping clip stays until another animation replaces it or the creature
    // is released from the conversation. A world-space clip
    // plays with the model at the world origin.
    if (!creature->playExternalAnimation(animation, std::move(properties), AnimationSource {cut.id, true})) {
        return false;
    }
    return true;
}

// A dialoganimations.2da ordinal plays the pose its row names; a lower one
// plays its animations.2da row. Either loops or plays once as its row says; a
// row that does both loops and then plays once over the loop.
void DialogGUI::applyDialogAnimation(const std::string &participant, int ordinal) {
    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return;
    }
    const AnimationSource source {ordinal, true};
    if (ordinal < kDialogAnimationBase) {
        const auto row = static_cast<uint32_t>(ordinal);
        const std::string clip = _services.game.animations.getNameById(row);
        if (_services.game.animations.isLoopingById(row)) {
            creature->playDialogAnimation(clip, true, source);
        }
        if (_services.game.animations.isFireForgetById(row)) {
            creature->playDialogAnimation(clip, false, source);
        }
        return;
    }
    const auto row = _services.game.animations.findDialogAnimation(ordinal - kDialogAnimationBase);
    if (!row) {
        return;
    }
    const AnimationType type = getDialogAnimationType(row->get());
    if (type == AnimationType::Invalid) {
        return;
    }
    if (row->get().looping) {
        creature->playDialogAnimation(type, true, source);
    }
    if (row->get().fireForget) {
        creature->playDialogAnimation(type, false, source);
    }
}

std::optional<DialogGUI::CutAnimation> DialogGUI::decodeCutAnimation(int ordinal) {
    if (!isDialogCutClip(ordinal)) {
        return std::nullopt;
    }
    CutAnimation cut;
    cut.id = ordinal;
    cut.name = cutClipName(ordinal);
    cut.looping = ordinal >= kFirstLoopingCutOrdinal;
    cut.worldSpace = isWorldSpaceCutClip(ordinal);
    return cut;
}

AnimationType DialogGUI::getDialogAnimationType(int ordinal) const {
    if (ordinal < kDialogAnimationBase) {
        return AnimationType::Invalid;
    }
    const int index = ordinal - kDialogAnimationBase;
    const auto row = _services.game.animations.findDialogAnimation(index);
    if (!row) {
        warn("Dialog: animation index out of bounds: " + std::to_string(index));
        return AnimationType::Invalid;
    }
    return getDialogAnimationType(row->get());
}

AnimationType DialogGUI::getDialogAnimationType(const DialogAnimation &row) {
    auto maybeAnimType = g_animTypeByName.find(row.name);
    return maybeAnimType != g_animTypeByName.end() ? maybeAnimType->second : AnimationType::Invalid;
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
    // The last speaker and listener stop looking. The participants of the
    // list before the last one are released; those the last list animated
    // keep their animations and looks. Then the last pair returns to its
    // pause, the speaker first.
    auto speaker = _pairSpeaker.resolve();
    auto listener = _pairListener.resolve();
    endSpeakerPair();
    releaseParticipants(participantObjects(_lastButOneAnimations));
    for (const auto &object : {speaker, listener}) {
        if (object) {
            returnToPause(*object);
        }
    }
    _previousAnimations.clear();
    _lastButOneAnimations.clear();
    _waitedParticipants.clear();

    if (hasStuntPresentation()) {
        releaseStuntParticipants();
    }

    // Make current speaker stop talking, if any
    auto speakerCreature =
        std::dynamic_pointer_cast<Creature>(_currentSpeaker.resolve());
    if (speakerCreature) {
        speakerCreature->stopTalking();
    }
}

void DialogGUI::releaseStuntParticipants() {
    if (!_dialog->isAnimatedCutscene()) {
        for (auto &participant : _participantByTag) {
            leaveMixedStunt(participant.second);
        }
        _participantByTag.clear();
        return;
    }
    // The animated cutscene is over, and holds no one.
    for (auto &participant : _participantByTag) {
        auto creature = participant.second.creature.resolve();
        if (!creature) continue;
        creature->releaseCutsceneAnimation();
        creature->resumeStateDrivenAnimation();
        creature->setIsInConversation(false);
    }
    _participantByTag.clear();
}

void DialogGUI::onEntryEnded() {
    _controls.LB_REPLIES->setVisible(true);
    if (!autoPicksFirstReply()) {
        passTurnToReplySpeaker();
    }
    repositionMessage();
}

// A chosen reply animates its participants and poses the answering pair,
// except in an animated cutscene.
// In an animated cutscene a chosen reply leaves the entry's participants
// animated and waited for.
void DialogGUI::onReplyPicked(const Dialog::EntryReply &reply) {
    if (_dialog->isAnimatedCutscene()) {
        return;
    }
    restoreInactiveStuntParticipants(reply);
    updateParticipantAnimations(reply);
}

// A reply taken without a choice puts its participants in the first places of
// the list waited for, as far as the list goes.
void DialogGUI::onReplyTaken(const Dialog::EntryReply &reply) {
    const size_t count = std::min(reply.animations.size(), _waitedParticipants.size());
    for (size_t i = 0; i < count; ++i) {
        _waitedParticipants[i] = reply.animations[i].participant;
    }
}

// Only the creatures of the list count. KotOR waits while any of them plays a
// dialog animation. TSL waits until one of them has stopped, so a list that
// names no creature keeps it waiting.
bool DialogGUI::isDialogAnimationPlaying() const {
    const bool tsl = _game.isTSL();
    for (const auto &participant : _waitedParticipants) {
        auto creature = std::dynamic_pointer_cast<Creature>(participantObject(participant));
        if (!creature) {
            continue;
        }
        const bool animating = _services.game.animations.isDialogAnimation(creature->currentAnimationId());
        if (tsl && !animating) {
            return false;
        }
        if (!tsl && animating) {
            return true;
        }
    }
    return tsl;
}

// With subtitles off, the text shows only when no voice-over speaks the entry
// alone, and never in an animated cutscene.
void DialogGUI::setMessage(std::string message) {
    const bool shown = _game.feedbackOptions().subtitles || (!_dialog->isAnimatedCutscene() && !_currentEntry->voiceOverOnly);
    _controls.LBL_MESSAGE->setTextMessage(shown ? message : "");
    // The replies of the entry are listed when it ends; those of the entry
    // before are gone.
    _controls.LB_REPLIES->clearItems();
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
    // The first reply is selected as the replies are listed.
    _controls.LB_REPLIES->setSelectedItemIndex(0);
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

std::shared_ptr<ListBox> DialogGUI::replyList() const {
    return _controls.LB_REPLIES;
}

// The letterbox is drawn with the conversation, so its fade runs with it.
void DialogGUI::update(float dt) {
    advanceEntryFade(dt);
    Conversation::update(dt);

    // The dialog camera follows the pair of its shot.
    if (_game.cameraType() == CameraType::Dialog) {
        _game.module()->area()->getCamera<DialogCamera>(CameraType::Dialog)->followShot();
    }
}

} // namespace game

} // namespace reone
