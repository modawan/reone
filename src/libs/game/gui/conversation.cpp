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

#include "reone/game/gui/conversation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/placeable.h"
#include "reone/game/script/runner.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/resources.h"
#include "reone/resource/resref.h"
#include "reone/resource/strings.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

// The time a line takes to speak: a second, and a little more for each
// character, in whole seconds.
static constexpr float kSpokenTimeBase = 1.0f;
static constexpr float kSpokenTimePerCharacter = 0.11f;

// Camera shots: the placed camera and same-pair angles, the animated shot
// range, and the video effect values a shot can carry besides a row.
static constexpr int kPlacedCameraAngle = 6;
static constexpr int kSamePairCameraAngle = 5;
static constexpr int kFirstCameraAnimation = 1000;
static constexpr int kLastCameraAnimation = 1727;
static constexpr int kNoShotVideoEffect = -1;
static constexpr int kKeepShotVideoEffect = -2;
static constexpr int kSecurityCameraVideoEffect = 0;
// The angles an entry or reply without one takes in turn, and the angle of
// the first entry and the first list of replies without one.
static constexpr std::array<int, 19> kCameraAngleCycle {1, 3, 1, 3, 2, 2, 1, 3, 1, 2, 1, 2, 1, 3, 2, 3, 1, 1, 1};
static constexpr int kFirstShotCameraAngle = 2;
// The variation numbers the entries whose sound takes one draw in turn, and
// the highest number a barked one-liner's sound takes at random.
static constexpr std::array<int, 19> kSoundVariationCycle {1, 1, 2, 3, 1, 2, 1, 2, 2, 3, 3, 3, 1, 3, 2, 1, 3, 2, 2};
static constexpr int kBarkSoundVariations = 3;

static bool g_allEntriesSkippable = false;

static script::ArgKind getScriptParamArgKind(size_t index) {
    switch (index) {
    case 0:
        return script::ArgKind::ScriptParam1;
    case 1:
        return script::ArgKind::ScriptParam2;
    case 2:
        return script::ArgKind::ScriptParam3;
    case 3:
        return script::ArgKind::ScriptParam4;
    case 4:
    default:
        return script::ArgKind::ScriptParam5;
    }
}

template <typename Params>
static std::vector<script::Argument> makeScriptArgs(uint32_t callerId, const Params &params) {
    std::vector<script::Argument> args;
    if (callerId) {
        args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(callerId));
    }
    for (size_t i = 0; i < params.ints.size(); ++i) {
        args.emplace_back(getScriptParamArgKind(i), script::Variable::ofInt(params.ints[i]));
    }
    args.emplace_back(script::ArgKind::ScriptStringParam, script::Variable::ofString(params.str));
    return args;
}

bool Conversation::isCurrent(uint64_t generation) const {
    return generation == _generation && _game.globalFade().isCurrentDialog(_fadeDialog);
}

void Conversation::start(const std::shared_ptr<Dialog> &dialog, const std::shared_ptr<Object> &owner,
                         GlobalFade::DialogTicket admission, const std::shared_ptr<Object> &listener) {
    if (!admission) {
        admission = _game.globalFade().admitDialog(/*replace=*/true);
    }
    if (!_game.globalFade().isCurrentDialog(admission)) {
        return;
    }
    auto generation = ++_generation;
    _fadeDialog = std::move(admission);
    if (_dialog) {
        stopVoice();
        resetEntryFade();
        onFinish();
        if (!isCurrent(generation)) {
            return;
        }
        releaseDialogParticipants(_owner.resolve());
    }
    _pendingEntry.reset();
    debug("Start " + dialog->resRef, LogChannel::Conversation);

    _paused = false;
    _endReached = false;
    _dialog = dialog;
    _owner = owner;
    _partner = listener;
    _shotCamera = CameraType::Dialog;
    _animatedView = false;
    seedCameraAngles();

    if (owner) {
        attachDialogParticipant(owner);
        attachDialogParticipant(listener);
    }

    loadCameraModel();
    onStart();
    if (isCurrent(generation)) {
        loadStartEntry();
    }
}

void Conversation::attachDialogParticipant(const std::shared_ptr<Object> &object) {
    // A conversation without a listener, or an entry whose speaker tag names
    // no object, attaches no one.
    const auto dialogOwner = _owner.resolve();
    if (!object || !dialogOwner) {
        return;
    }
    const auto previous = object->dialogOwner();
    // A speaker stays with another conversation that still owns it.
    if (previous && previous != dialogOwner && previous->dialogOwner() == previous) {
        return;
    }
    object->setDialogOwner(dialogOwner);
    object->setIsInConversation(true);
    const auto found = std::find_if(_dialogParticipants.begin(), _dialogParticipants.end(),
                                    [&](const auto &ref) { return ref.resolve() == object; });
    if (found == _dialogParticipants.end()) {
        _dialogParticipants.emplace_back(object);
    }
}

void Conversation::releaseDialogParticipants(const std::shared_ptr<Object> &dialogOwner) {
    auto participants = std::move(_dialogParticipants);
    _dialogParticipants.clear();
    for (const auto &reference : participants) {
        if (const auto object = reference.resolve(); object && object->dialogOwner() == dialogOwner) {
            object->setDialogOwner(nullptr);
            object->setIsInConversation(false);
        }
    }
}

// An object's end-dialogue script: a creature's or a placeable's. Other
// objects have none.
static void runObjectEndDialogScript(const std::shared_ptr<Object> &object) {
    if (auto creature = std::dynamic_pointer_cast<Creature>(object)) {
        creature->runEndDialogScript();
    } else if (auto placeable = std::dynamic_pointer_cast<Placeable>(object)) {
        placeable->runEndDialogScript();
    }
}

void Conversation::runAreaEndDialogScripts(const Object &dialogOwner) {
    // The end of a conversation runs the end-dialogue script of every creature
    // and placeable in the owner's area, not only the speaker's. An owner in
    // no area has none to run. The scripts may add or remove objects, so the
    // area's objects are taken before any runs.
    auto area = dialogOwner.spatialArea();
    if (!area) {
        return;
    }
    std::vector<RuntimeObjectRef<Object>> objects;
    for (const auto &object : area->get().objects()) {
        objects.emplace_back(object);
    }
    for (const auto &reference : objects) {
        runObjectEndDialogScript(reference.resolve());
    }
}

void Conversation::loadCameraModel() {
    std::string modelResRef(_dialog->cameraModel);
    _cameraModel = modelResRef.empty() ? nullptr : _services.resource.models.get(modelResRef);
}

void Conversation::setBarkText(std::string text, float duration) {
    _game.setBarkBubbleText(std::move(text), duration);
}

void Conversation::onStart() {
}

void Conversation::loadStartEntry() {
    auto generation = _generation;
    auto dialog = _dialog;
    int entryIdx = indexOfFirstActive(dialog->startEntries);
    if (!isCurrent(generation)) {
        return;
    }
    if (entryIdx == -1) {
        debug("Finish (no active start entry)", LogChannel::Conversation);
        finish();
        return;
    }
    loadEntry(entryIdx, true);
}

int Conversation::indexOfFirstActive(const std::vector<Dialog::EntryReplyLink> &links) {
    auto generation = _generation;
    for (auto &link : links) {
        bool active = isLinkActive(link);
        if (!isCurrent(generation)) {
            return -1;
        }
        if (active) {
            return link.index;
        }
    }
    return -1;
}

bool Conversation::isLinkActive(const Dialog::EntryReplyLink &link) {
    auto generation = _generation;
    std::optional<bool> active;
    if (!link.active.empty()) {
        active = evaluateCondition(link.active, link.params);
        if (!isCurrent(generation)) {
            return false;
        }
        if (link.notActive) {
            active = !active.value();
        }
    }
    std::optional<bool> active2;
    if (!link.active2.empty()) {
        active2 = evaluateCondition(link.active2, link.params2);
        if (!isCurrent(generation)) {
            return false;
        }
        if (link.notActive2) {
            active2 = !active2.value();
        }
    }
    if (!active && !active2) {
        return true;
    }
    if (!active) {
        return active2.value();
    }
    if (!active2) {
        return active.value();
    }
    return link.logic == 1 ? active.value() || active2.value() : active.value() && active2.value();
}

bool Conversation::evaluateCondition(const std::string &scriptResRef, const Dialog::EntryReplyLink::ConditionParams &params) {
    auto owner = _owner.resolve();
    return _game.scriptRunner().run(
               scriptResRef,
               makeScriptArgs(owner ? owner->id() : 0, params)) != 0;
}

void Conversation::runScript(const std::string &scriptResRef, const Dialog::EntryReply::ActionParams &params) {
    if (!scriptResRef.empty()) {
        auto owner = _owner.resolve();
        _game.scriptRunner().run(
            scriptResRef,
            makeScriptArgs(owner ? owner->id() : 0, params));
    }
}

void Conversation::runScripts(const Dialog::EntryReply &node) {
    auto generation = _generation;
    runScript(node.script, node.actionParams);
    if (isCurrent(generation)) {
        runScript(node.script2, node.actionParams2);
    }
}

void Conversation::applyStatusSummaryEntries(const Dialog::EntryReply &node) {
    if (!node.quest.empty()) {
        _game.journal().addEntry(node.quest, static_cast<int>(node.questEntry));
    }
    _game.awardPlotXPByIndex(node.plotIndex, node.plotXPPercentage);
}

void Conversation::finish(bool runsEndScript, bool aborted) {
    auto generation = ++_generation;
    auto dialog = _dialog;
    auto ownerRef = _owner;
    _game.globalFade().finishDialog(_fadeDialog);
    _fadeDialog.reset();
    // A conversation ending takes the video effect away, and any script's
    // request that the leader answer.
    _game.disableVideoEffect();
    _game.setLeaderAnswersInDialog(false);
    _game.setDialogUnderWay(false);
    _paused = false;
    _entryEnded = true;
    _pendingEntry.reset();
    // A closed conversation keeps no replies to take.
    _replies.clear();
    stopVoice();
    resetEntryFade();
    onFinish();
    if (_generation != generation) {
        return;
    }

    // A reply script can hand the screen to something else before the
    // conversation ends -- PlayPazaak opens the pazaak board from a dialogue
    // action -- so only return to the world if the conversation still owns it.
    if (_game.currentScreen() == Game::Screen::Conversation) {
        _game.openInGame();
    }
    // A conversation a companion handed on returns control to it.
    _game.finishPostDialogCharacterSwitch();

    // Run EndConversation script, or EndConverAbort for a conversation cut
    // off before its last entry
    if (auto owner = ownerRef.resolve(); owner && runsEndScript) {
        if (aborted) {
            if (!dialog->abortScript.empty()) {
                _game.scriptRunner().run(dialog->abortScript, owner->id());
            }
        } else {
            runEndScript(*dialog, *owner);
            runAreaEndDialogScripts(*owner);
        }
    }

    if (_generation == generation) {
        releaseDialogParticipants(ownerRef.resolve());
    }
}

void Conversation::onFinish() {
}

void Conversation::runEndScript(const Dialog &dialog, const Object &owner) {
    if (!dialog.endScript.empty()) {
        _game.scriptRunner().run(dialog.endScript, owner.id());
    }
}

void Conversation::cleanupForModuleTransition() {
    auto generation = ++_generation;
    _game.globalFade().finishDialog(_fadeDialog);
    _fadeDialog.reset();
    _game.disableVideoEffect();
    _game.setLeaderAnswersInDialog(false);
    _paused = false;
    _entryEnded = true;
    if (!_dialog) {
        return;
    }
    _pendingEntry.reset();
    _replies.clear();
    stopVoice();
    resetEntryFade();
    _lipAnimation.reset();
    onFinish();
    if (_generation == generation) {
        releaseDialogParticipants(_owner.resolve());
    }
}

// The sound a variation number is added to: the node's sound, or else its
// voice-over.
static std::string getVariedSound(const Dialog::EntryReply &node, int variation) {
    const std::string &sound = node.sound.empty() ? node.voResRef : node.sound;
    return ResRef(sound + std::to_string(variation)).value();
}

void Conversation::loadEntry(int index, bool start) {
    auto generation = _generation;
    auto dialog = _dialog; // retain nodes across callbacks, including conditions
    debug("Load entry " + std::to_string(index), LogChannel::Conversation);
    _currentEntry = &_dialog->getEntry(index);
    // An entry keeps the camera angle it is given while the conversation lasts.
    const int drawnAngle = drawCameraAngle();
    // Each entry shown draws a sound variation number; only TSL marks a sound
    // to take one.
    const int drawnSoundVariation = _soundVariationCycle.draw(kSoundVariationCycle);
    int &angle = _entryCameraAngles.try_emplace(index, _currentEntry->cameraAngle).first->second;
    if (angle == 0) {
        angle = _firstEntryShot ? kFirstShotCameraAngle : drawnAngle;
    }
    _firstEntryShot = false;
    _entryCameraAngle = angle;
    if (!_currentEntry->speaker.empty()) {
        if (auto owner = _owner.resolve(); owner && owner->spatialArea()) {
            attachDialogParticipant(owner->spatialArea()->get().getObjectByTag(_currentEntry->speaker));
        }
    }

    // A conversation is a one-liner when it opens on an entry with no replies,
    // or with one reply that leads to no entry; the reply's text and condition
    // play no part. Cinematic conversations can be one-liners, and so can
    // computer ones in KotOR. A one-liner is barked instead of opened: it
    // lists no replies, and neither its entry nor its reply adds a journal
    // entry or plot experience.
    bool oneLiner = false;
    const ConversationType type = _dialog->conversationType;
    if (start && (type == ConversationType::Cinematic || (type == ConversationType::Computer && !_game.isTSL()))) {
        const auto &links = _currentEntry->replies;
        oneLiner = links.empty() || (links.size() == 1ll && _dialog->getReply(links.front().index).entries.empty());
    }

    // An entry's replies are listed when it ends.
    _replies.clear();
    _autoPickFirstReply = false;

    std::string entryText(_game.substituteCustomTokens(_currentEntry->text));
    if (oneLiner) {
        debug("Dialog: finish (one-liner)");

        // A one-liner is not shown in the conversation's panel. A line with
        // text is barked, and the conversation's end script runs then, with
        // the owner's own end-dialogue script but no other's; a line without
        // text shows nothing and runs no end script. The entry's actions
        // follow, then the sole reply's, whether or not its condition holds,
        // all with the owner as the caller. An action can start another
        // conversation, which then stands. Ending the entry first stops the
        // update timer from ending it again.
        _endReached = true;
        // The bark keeps its voice: the conversation ending does not stop it.
        // A sound that takes a variation number takes one at random. A line
        // with neither a sound nor a voice-over plays its talk table sound.
        std::string barkSound = _currentEntry->soundTakesVariation
                                    ? getVariedSound(*_currentEntry, randomInt(1, kBarkSoundVariations))
                                    : _currentEntry->sound;
        if (barkSound.empty() && _currentEntry->voResRef.empty()) {
            barkSound = _services.resource.strings.getSound(_currentEntry->textStrRef);
        }
        loadVoiceOver(barkSound);
        _currentVoice.reset();
        _game.globalFade().revealDialog(_fadeDialog);
        _entryEnded = true;
        if (!_currentEntry->text.empty()) {
            // The bark shows for the time its line takes to speak.
            const float barkDuration = kSpokenTimeBase + kSpokenTimePerCharacter * static_cast<float>(entryText.size());
            setBarkText(std::move(entryText), barkDuration);
            if (auto owner = _owner.resolve()) {
                runEndScript(*_dialog, *owner);
                if (!isCurrent(generation)) {
                    return;
                }
                runObjectEndDialogScript(owner);
                if (!isCurrent(generation)) {
                    return;
                }
            }
        }
        runScripts(*_currentEntry);
        if (!isCurrent(generation)) {
            return;
        }
        if (!_currentEntry->replies.empty()) {
            runScripts(_dialog->getReply(_currentEntry->replies.front().index));
            if (!isCurrent(generation)) {
                return;
            }
        }
        finish(/*runsEndScript=*/false);
        return;
    }

    // The entry is shown before its scripts run: its message, its line in the
    // dialogue list, its voice, its camera shot and its participants. Its
    // journal entry and plot experience follow.
    setMessage(entryText);
    logEntryLine(entryText);
    // An entry whose sound takes a variation number keeps the number drawn
    // the first time it shows while the conversation lasts.
    std::string sound = _currentEntry->sound;
    if (_currentEntry->soundTakesVariation && !_currentEntry->soundVariationAdded) {
        sound = _entrySounds.try_emplace(index, getVariedSound(*_currentEntry, drawnSoundVariation)).first->second;
    }
    loadVoiceOver(sound);
    scheduleEndOfEntry();
    if (isAnimatedCameraShot()) {
        _animatedView = true;
    } else {
        takeCameraShot(*_currentEntry, _entryCameraAngle);
    }
    onLoadEntry();
    if (!isCurrent(generation)) {
        return;
    }
    applyShotVideoEffect();
    resetEntryFade();
    startEntryFade(*_currentEntry);
    applyStatusSummaryEntries(*_currentEntry);

    // Entry publication consumes only the current handoff. An entry action
    // below can immediately replace this automatic reveal with its own fade.
    _game.globalFade().revealDialog(_fadeDialog);

    // Run entry scripts. An entry action can start another conversation, which
    // replaces this one outright. Holding the dialogue keeps this entry alive
    // for the script to act on, and tells us to stop rather than carry on
    // driving the new session with the old one's state. The replies are
    // listed when the entry ends, so their conditions see what its scripts
    // did.
    runScripts(*_currentEntry);
    if (!isCurrent(generation)) {
        return;
    }

    if (_autoSkip) {
        if (std::optional<bool> skip = _autoSkip->trySkipEntry()) {
            if (skip.value()) {
                requestSkip();
            }
        }
    }
}

void Conversation::onLoadEntry() {
}

// The shown entry's camera shot sets the video effect.
//  - A computer conversation takes it only from a placed camera (angle 6),
//    where no effect of its own means the security camera look (row 0).
//    KotOR leaves the effect as it is for -2.
//  - A cinematic conversation leaves it as it is for an animated shot and
//    for a shot that keeps the previous pair (angle 5). Otherwise TSL shows
//    the entry's effect or, without one, takes the effect away unless a
//    script holds it; KotOR shows the entry's effect on a placed camera and
//    takes the effect away on any other shot.
void Conversation::applyShotVideoEffect() {
    const Dialog::EntryReply &entry = *_currentEntry;
    const int effect = entry.camVidEffect;
    const bool none = effect == kNoShotVideoEffect || effect == kKeepShotVideoEffect;
    const bool placed = entry.cameraAngle == kPlacedCameraAngle;
    const bool tsl = _game.isTSL();
    if (_dialog->conversationType == ConversationType::Computer) {
        if (!placed) {
            return;
        }
        if (effect == kNoShotVideoEffect) {
            _game.enableVideoEffect(kSecurityCameraVideoEffect);
        } else if (tsl || effect != kKeepShotVideoEffect) {
            _game.enableVideoEffect(effect);
        }
        return;
    }
    if (isAnimatedCameraShot() || keepsPreviousSpeakerPair(entry)) {
        return;
    }
    if (tsl) {
        if (!none) {
            _game.enableVideoEffect(effect);
        } else if (!_game.isVideoEffectHeldByScript()) {
            _game.disableVideoEffect();
        }
    } else if (!placed) {
        _game.disableVideoEffect();
    } else if (!none) {
        _game.enableVideoEffect(effect);
    }
}

// A shot after an animated one starts on the dialog camera.
void Conversation::takeCameraShot(const Dialog::EntryReply &shot, int angle) {
    if (_animatedView) {
        _animatedView = false;
        _shotCamera = CameraType::Dialog;
    }
    if (angle == kPlacedCameraAngle) {
        _shotCamera = CameraType::Static;
        _shotCameraId = shot.cameraId;
        return;
    }
    if (angle == kSamePairCameraAngle && _dialog->conversationType != ConversationType::Computer) {
        return;
    }
    _shotCamera = CameraType::Dialog;
}

// Each character of the dialog's name weighs itself plus itself times ten to
// the power of its place, in 32-bit arithmetic; a power beyond 32 bits weighs
// as the least 32-bit integer. The sum starts the cycles of camera angles and
// sound variation numbers alike.
void Conversation::seedCameraAngles() {
    static constexpr double kFirstOutOfRangePower = 2147483648.0;
    uint32_t seed = 0;
    const std::string &name = _dialog->resRef;
    for (size_t place = 0; place < name.size(); ++place) {
        const auto weight = static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(name[place])));
        const double power = std::pow(10.0, static_cast<double>(place));
        const int32_t scale = power < kFirstOutOfRangePower ? static_cast<int32_t>(power) : std::numeric_limits<int32_t>::min();
        seed += weight + weight * static_cast<uint32_t>(scale);
    }
    _angleCycle.seed(seed);
    _soundVariationCycle.seed(seed);
    _firstEntryShot = true;
    _firstReplyShot = true;
    _entryCameraAngles.clear();
    _replyCameraAngles.clear();
    _entrySounds.clear();
}

int Conversation::drawCameraAngle() {
    return _angleCycle.draw(kCameraAngleCycle);
}

// The seed gives the place, a step of 1 to 5 and the direction.
void Conversation::DrawCycle::seed(uint32_t seed) {
    static constexpr uint32_t kStepCount = 5;
    place = static_cast<int>(seed % kLength);
    step = static_cast<int>(seed % kStepCount) + 1;
    forward = (seed & 1) != 0;
}

// A step past either end of the cycle stops at the other end.
int Conversation::DrawCycle::draw(const Values &values) {
    const int last = kLength - 1;
    const int value = values[place];
    int next = place + (forward ? step : -step);
    if (next > last) {
        next = 0;
    }
    if (next < 0) {
        next = last;
    }
    place = next;
    return value;
}

bool Conversation::isAnimatedCameraShot() const {
    const int animation = _currentEntry->cameraAnimation;
    return !_dialog->cameraModel.empty() && animation >= kFirstCameraAnimation && animation <= kLastCameraAnimation;
}

bool Conversation::keepsPreviousSpeakerPair(const Dialog::EntryReply &shot) {
    return shot.cameraAngle == kSamePairCameraAngle;
}

std::optional<std::reference_wrapper<const Dialog::EntryReply>> Conversation::firstReply() const {
    if (_replies.empty()) {
        return std::nullopt;
    }
    return std::cref(*_replies.front());
}

// An entry with a sound stops the voice before it; one without leaves it
// playing.
void Conversation::loadVoiceOver(const std::string &sound) {
    _lipAnimation.reset();
    if (sound.empty() && _currentEntry->voResRef.empty()) {
        return;
    }
    stopVoice();

    // Play current voice over either from Sound or from VO_ResRef
    std::string voiceResRef;
    if (!sound.empty()) {
        voiceResRef = sound;
        _lipAnimation = _services.resource.lips.get(sound);
    }
    if (!_currentEntry->voResRef.empty()) {
        if (voiceResRef.empty()) {
            voiceResRef = _currentEntry->voResRef;
        }
        if (!_lipAnimation) {
            _lipAnimation = _services.resource.lips.get(_currentEntry->voResRef);
        }
    }
    if (!voiceResRef.empty()) {
        auto clip = _services.resource.audioClips.get(voiceResRef);
        if (clip) {
            _currentVoice = _services.audio.mixer.play(std::move(clip), AudioType::Voice);
        }
    }
}

// The time a node's text takes to speak, in whole seconds; an empty line takes
// none. TSL measures the text with its tokens resolved and its leading and
// trailing spaces trimmed.
static float getSpokenTime(const std::string &text, const Game &game) {
    std::string spoken(text);
    if (game.isTSL()) {
        spoken = game.substituteCustomTokens(spoken);
        spoken.erase(0, spoken.find_first_not_of(' '));
        spoken.erase(spoken.find_last_not_of(' ') + 1);
    }
    if (spoken.empty()) {
        return 0.0f;
    }
    return std::trunc(kSpokenTimeBase + kSpokenTimePerCharacter * static_cast<float>(spoken.size()));
}

static bool hasSound(const Dialog::EntryReply &node) {
    return !node.sound.empty() || !node.voResRef.empty();
}

// A node with a sound and no delay or wait flags of its own waits for its
// sound.
static int getWaitFlags(const Dialog::EntryReply &node) {
    if (node.delay == -1 && hasSound(node) && node.waitFlags == 0) {
        return Dialog::WaitFlags::waitSoundFinish;
    }
    return node.waitFlags;
}

// A node's own delay, or, for one with a sound and no delay of its own, the
// dialog's entry delay.
static float getAuthoredDelay(const Dialog::EntryReply &node, const Dialog &dialog) {
    return node.delay != -1 ? static_cast<float>(node.delay) : static_cast<float>(dialog.entryDelay);
}

// An entry stays for its delay: its own; else, with a sound, the dialog's
// entry delay when the sound is a recorded voice; else none when it waits for
// something; else none in an animated cutscene, and otherwise the time its
// text takes to speak. Past its delay, it stays while what it waits for goes
// on: its camera animation, its sound and its participants' dialog
// animations.
void Conversation::scheduleEndOfEntry() {
    const Dialog::EntryReply &entry = *_currentEntry;
    const int waitFlags = getWaitFlags(entry);
    float duration = 0.0f;
    if (entry.delay != -1 || (hasSound(entry) && entry.soundExists)) {
        duration = getAuthoredDelay(entry, *_dialog);
    } else if (!_dialog->isAnimatedCutscene() && (hasSound(entry) || waitFlags == 0)) {
        duration = getSpokenTime(entry.text, _game);
    }

    if (_cameraModel && (waitFlags & Dialog::WaitFlags::waitAnimFinish)) {
        std::string animName(AnimatedCamera::getShotClipName(entry.cameraAnimation));
        if (std::shared_ptr<Animation> animation = _cameraModel->getAnimation(animName)) {
            duration = std::max(duration, animation->length());
        }
    }

    _entryEnded = false;
    _entrySkipped = false;
    _endEntryTimer.reset(duration);
    _waitFlags = waitFlags;
}

// A reply taken without a choice holds the next entry back: for its delay
// when it waits for its sound; else not at all when it waits for something
// else or in an animated cutscene; else for the time its text takes to speak.
// Past that, the next entry waits for what the reply waits for, except in an
// animated cutscene.
void Conversation::scheduleTakenReply(const Dialog::EntryReply &reply) {
    const int waitFlags = getWaitFlags(reply);
    float duration = 0.0f;
    if (waitFlags & Dialog::WaitFlags::waitSoundFinish) {
        duration = getAuthoredDelay(reply, *_dialog);
    } else if (!_dialog->isAnimatedCutscene() && (reply.delay != -1 || hasSound(reply) || waitFlags == 0)) {
        duration = getSpokenTime(reply.text, _game);
    }
    _endEntryTimer.reset(duration);
    _waitFlags = _dialog->isAnimatedCutscene() ? 0 : waitFlags;
}

bool Conversation::isWaiting() const {
    if ((_waitFlags & Dialog::WaitFlags::waitSoundFinish) && _currentVoice && _currentVoice->isPlaying()) {
        return true;
    }
    if ((_waitFlags & Dialog::WaitFlags::waitFadeFinish) && !isEntryFadeDone()) {
        return true;
    }
    if ((_waitFlags & Dialog::WaitFlags::waitAnimFinish) && isCameraAnimationPlaying()) {
        return true;
    }
    return (_waitFlags & Dialog::WaitFlags::waitParticipantAnimFinish) && isDialogAnimationPlaying();
}

// Fade types 1 and 4 go towards the colour; the others come away from it.
void Conversation::startEntryFade(const Dialog::EntryReply &entry) {
    static constexpr int kCutToColorFade = 1;
    static constexpr int kFadeToColor = 4;
    static constexpr float kFadeClockStart = 0.1f;
    if (entry.fadeType == 0) {
        return;
    }
    _entryFade.fromColor = entry.fadeType != kCutToColorFade && entry.fadeType != kFadeToColor;
    _entryFade.color = entry.fadeColor;
    _entryFade.alpha = _entryFade.fromColor ? 1.0f : 0.0f;
    _entryFade.delay = entry.fadeDelay;
    _entryFade.length = entry.fadeLength;
    _entryFade.clock = kFadeClockStart;
}

// The clock skips a frame of five seconds or more. Past its delay the fade
// moves by the share of its length gone by.
void Conversation::advanceEntryFade(float dt) {
    static constexpr float kLongestFadeFrame = 5.0f;
    if (_entryFade.clock <= 0.0f) {
        return;
    }
    if (dt < kLongestFadeFrame) {
        _entryFade.clock += dt;
    }
    if (_entryFade.clock <= _entryFade.delay) {
        return;
    }
    const float progress = (_entryFade.clock - _entryFade.delay) / _entryFade.length;
    const float alpha = _entryFade.fromColor ? std::max(0.0f, 1.0f - progress) : std::min(1.0f, progress);
    _entryFade.alpha = std::clamp(alpha, 0.0f, 1.0f);
}

void Conversation::resetEntryFade() {
    _entryFade = EntryFade();
}

bool Conversation::isEntryFadeDone() const {
    return _entryFade.clock == 0.0f || _entryFade.clock > _entryFade.delay + _entryFade.length;
}

bool Conversation::isDialogAnimationPlaying() const {
    return false;
}

void Conversation::onReplyTaken(const Dialog::EntryReply &) {
}

// The camera plays a clip only in a conversation with a camera model.
bool Conversation::isCameraAnimationPlaying() const {
    if (!_cameraModel) {
        return false;
    }
    auto camera = _game.module()->area()->getCamera<AnimatedCamera>(CameraType::Animated);
    return camera->isRequestedClipPlaying();
}

void Conversation::stopVoice() {
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
    }
}

// A reply as listed and logged: its tokens resolved and, in TSL, its leading
// and trailing spaces trimmed.
static std::string getParsedReplyText(const Dialog::EntryReply &reply, const Game &game) {
    std::string text(game.substituteCustomTokens(reply.text));
    if (game.isTSL()) {
        text.erase(0, text.find_first_not_of(' '));
        text.erase(text.find_last_not_of(' ') + 1);
    }
    return text;
}

// The replies whose condition holds are listed first. A reply whose condition
// fails is listed after them when its link displays it inactive, the last
// such reply first; it can be chosen like any other.
void Conversation::loadReplies() {
    auto generation = _generation;
    auto dialog = _dialog;
    _replies.clear();
    std::vector<const Dialog::EntryReply *> inactive;
    std::vector<int> listedIndices;
    std::vector<int> inactiveIndices;
    for (auto &link : _currentEntry->replies) {
        bool active = isLinkActive(link);
        if (!isCurrent(generation)) {
            return;
        }
        if (active) {
            _replies.push_back(&_dialog->getReply(link.index));
            listedIndices.push_back(link.index);
        } else if (link.displayInactive) {
            inactive.push_back(&_dialog->getReply(link.index));
            inactiveIndices.push_back(link.index);
        }
    }
    const size_t activeCount = _replies.size();
    _replies.insert(_replies.end(), inactive.rbegin(), inactive.rend());
    listedIndices.insert(listedIndices.end(), inactiveIndices.rbegin(), inactiveIndices.rend());

    // A list of replies draws one camera angle, which each listed reply
    // without one keeps while the conversation lasts.
    const int drawnAngle = drawCameraAngle();
    for (size_t place = 0; place < listedIndices.size(); ++place) {
        const int index = listedIndices[place];
        int &angle = _replyCameraAngles.try_emplace(index, _dialog->getReply(index).cameraAngle).first->second;
        if (angle == 0) {
            angle = _firstReplyShot ? kFirstShotCameraAngle : drawnAngle;
        }
        if (place == 0) {
            _firstReplyCameraAngle = angle;
        }
    }
    _firstReplyShot = false;

    // A sole reply whose text resolves to nothing is taken when the entry ends.
    _autoPickFirstReply = _replies.size() == 1ll && getParsedReplyText(*_replies.front(), _game).empty();
    // The conversation ends on an entry that lists no reply, or whose sole
    // reply with a holding condition leads nowhere.
    _endReached = _replies.empty() || (activeCount == 1ll && _replies.front()->entries.empty());

    refreshReplies();
}

// A reply as listed: numbered by its place among the replies, or, in KotOR,
// "-" from the tenth on. A reply whose text resolves to nothing is not listed
// and gets an empty line, which keeps the numbers of the others.
static std::string getReplyText(const Dialog::EntryReply &reply, int index, const Game &game) {
    static constexpr int kLastNumberedK1Reply = 8;
    std::string text(getParsedReplyText(reply, game));
    if (text.empty()) {
        return text;
    }
    if (!game.isTSL() && index > kLastNumberedK1Reply) {
        return "-. " + text;
    }
    return str(boost::format("%d. %s") % (index + 1) % text);
}

// TSL lists up to 30 replies, KotOR up to 20.
size_t Conversation::listedReplyCount() const {
    static constexpr size_t kMaxListedRepliesTSL = 30;
    static constexpr size_t kMaxListedRepliesK1 = 20;
    return std::min(_replies.size(), _game.isTSL() ? kMaxListedRepliesTSL : kMaxListedRepliesK1);
}

void Conversation::refreshReplies() {
    std::vector<std::string> lines;
    if (!_autoPickFirstReply) {
        const size_t listed = listedReplyCount();
        for (size_t i = 0; i < listed; ++i) {
            lines.push_back(getReplyText(*_replies[i], static_cast<int>(i), _game));
        }
    }
    setReplyLines(std::move(lines));
}

// A reply is taken by its place among the replies listed: none while an
// entry shows, none once the conversation has closed, and no place past the
// last one listed.
void Conversation::pickReply(int index) {
    // Once a reply is taken, the next entry is on its way.
    if (_pendingEntry || index >= static_cast<int>(listedReplyCount())) {
        return;
    }
    auto generation = _generation;
    debug("Pick reply " + std::to_string(index), LogChannel::Conversation);
    const Dialog::EntryReply &reply = *_replies[index];

    // The chosen reply goes to the dialog list under the party leader's name.
    if (auto leader = _game.party().getLeader()) {
        _game.messageLog().addDialog(leader->name(), getParsedReplyText(reply, _game));
    }
    // A sole reply taken when the entry ends is not chosen by the player.
    if (!_autoPickFirstReply) {
        onReplyPicked(reply);
        if (!isCurrent(generation)) {
            return;
        }
    }

    // A reply taken without a choice sets its letterbox fade and its own
    // delay and waits. A chosen reply keeps the entry's waits.
    applyStatusSummaryEntries(reply);
    const bool taken = _autoPickFirstReply;
    if (taken) {
        setMessage(getParsedReplyText(reply, _game));
        startEntryFade(reply);
        onReplyTaken(reply);
        scheduleTakenReply(reply);
    }

    // Run reply scripts
    auto dialog = _dialog;
    runScripts(reply);

    // A reply action can start another conversation, replacing this one. Going
    // on would advance or finish the new session in place of the old one.
    if (!isCurrent(generation)) {
        return;
    }

    int entryIdx = indexOfFirstActive(reply.entries);
    if (!isCurrent(generation)) {
        return;
    }
    if (entryIdx == -1) {
        debug("Finish (no active entries)", LogChannel::Conversation);
        finish();
        return;
    }
    if (!_endEntryTimer.elapsed() || isWaiting()) {
        _pendingEntry = entryIdx;
        return;
    }
    loadEntry(entryIdx);
}

bool Conversation::handle(const input::Event &event) {
    if (handleConversationInput(event)) {
        return true;
    }
    return GameGUI::handle(event);
}

bool Conversation::handleConversationInput(const input::Event &event) {
    switch (event.type) {
    case input::EventType::MouseButtonDown:
        return handleMouseButtonDown(event.button);
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    default:
        return false;
    }
}

// The press is used up, so that its release cannot choose a reply listed
// meanwhile.
bool Conversation::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button == input::MouseButton::Left && acceptsSkip()) {
        requestSkip();
        return true;
    }
    return false;
}

// The keys act once per press. A number key chooses the reply of its number,
// with the click sound, when there is one.
bool Conversation::handleKeyDown(const input::KeyEvent &event) {
    if (event.repeat) {
        return false;
    }
    switch (event.code) {
    case input::KeyCode::Return:
    case input::KeyCode::KeypadEnter:
        if (acceptsSkip()) {
            requestSkip();
        } else {
            pickSelectedReply();
        }
        return true;
    case input::KeyCode::Up:
    case input::KeyCode::Down:
        if (!_entryEnded) {
            return false;
        }
        moveReplySelection(event.code == input::KeyCode::Down ? 1 : -1);
        return true;
    default:
        break;
    }

    using IntKeyCode = std::underlying_type_t<input::KeyCode>;
    const auto code = static_cast<IntKeyCode>(event.code);
    const auto key1 = static_cast<IntKeyCode>(input::KeyCode::Key1);
    const auto key9 = static_cast<IntKeyCode>(input::KeyCode::Key9);
    if (!_entryEnded || code < key1 || code > key9) {
        return false;
    }
    const auto index = static_cast<size_t>(code - key1);
    if (index < listedReplyCount()) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
        pickReply(static_cast<int>(index));
    }
    return true;
}

std::shared_ptr<ListBox> Conversation::replyList() const {
    return nullptr;
}

// The selected row's reply is chosen with the click sound.
void Conversation::pickSelectedReply() {
    const auto list = replyList();
    if (!list || list->selectedItemIndex() == -1) {
        return;
    }
    _services.audio.mixer.play(_services.game.guiSounds.getOnClick(), AudioType::Sound);
    pickReply(std::stoi(list->getItemAt(list->selectedItemIndex()).tag));
}

// The selection stops at either end of the list, which scrolls to show it,
// and the newly selected row sounds. With no row selected, either arrow
// selects the first row, and only the down arrow sounds.
void Conversation::moveReplySelection(int step) {
    const auto list = replyList();
    if (!list || list->getItemCount() == 0) {
        return;
    }
    const int row = list->selectedItemIndex();
    int next = 0;
    bool sounds = step > 0;
    if (row != -1) {
        next = row + step;
        if (next < 0 || next >= list->getItemCount()) {
            return;
        }
        sounds = true;
    }
    list->setSelectedItemIndex(next);
    list->scrollToItem(next);
    if (sounds) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnEnter(), AudioType::Sound);
    }
}

void Conversation::requestSkip() {
    if (!acceptsSkip()) {
        return;
    }
    _entrySkipped = true;
}

// While a reply taken without a choice holds the next entry back, the panel
// shows the reply's own empty text as its message, as for an entry.
bool Conversation::acceptsSkip() const {
    return !_entryEnded || (_autoPickFirstReply && _pendingEntry);
}

// The row under a resting pointer is selected, with the sound of a newly
// selected row.
void Conversation::selectReplyUnderPointer() {
    const auto list = replyList();
    if (!list) {
        return;
    }
    const int selected = list->selectedItemIndex();
    const glm::ivec2 pointer = _game.pointerPosition();
    _gui->handle(input::Event::newMouseMotion({pointer.x, pointer.y, 0.0f, 0.0f}));
    if (list->selectedItemIndex() != selected) {
        _services.audio.mixer.play(_services.game.guiSounds.getOnEnter(), AudioType::Sound);
    }
}

// A skip takes effect only in a skippable conversation and, in TSL, on an
// entry not marked unskippable.
bool Conversation::isSkippableEntry() const {
    if (g_allEntriesSkippable) {
        return true;
    }
    const bool nodeUnskippable = _game.isTSL() && _currentEntry && _currentEntry->nodeUnskippable;
    return _dialog->isSkippable() && !nodeUnskippable;
}

// An entry shown in the conversation goes to the dialog list under its
// speaker's name: the object its speaker tag names, or else the owner.
void Conversation::logEntryLine(const std::string &text) {
    if (text.empty()) {
        return;
    }
    auto owner = _owner.resolve();
    std::shared_ptr<Object> speaker;
    if (!_currentEntry->speaker.empty() && owner && owner->spatialArea()) {
        speaker = owner->spatialArea()->get().getObjectByTag(_currentEntry->speaker);
    }
    if (!speaker) {
        speaker = owner;
    }
    if (speaker) {
        _game.messageLog().addDialog(speaker->name(), text);
    }
}

void Conversation::endCurrentEntry() {
    if (!_currentEntry || _entryEnded || _paused) {
        return;
    }
    auto generation = _generation;
    _entryEnded = true;
    _entrySkipped = false;
    // An entry ends once its time is up.
    _endEntryTimer.reset(0);

    // The entry's replies arrive as it ends: their conditions are tested and
    // they are listed now, and the letterbox fade clears.
    loadReplies();
    if (!isCurrent(generation)) {
        return;
    }
    resetEntryFade();

    onEntryEnded();
    if (!isCurrent(generation)) {
        return;
    }
    selectReplyUnderPointer();

    if (_autoPickFirstReply) {
        pickReply(0);
    } else if (_replies.empty()) {
        debug("Finish (no active replies", LogChannel::Conversation);
        finish();
    } else if (_autoSkip) {
        if (std::optional<int> reply = _autoSkip->trySkipReply()) {
            pickReply(reply.value());
        }
    }
}

void Conversation::onEntryEnded() {
}

void Conversation::onReplyPicked(const Dialog::EntryReply &) {
}

void Conversation::update(float dt) {
    if (_dialog && !_owner.empty() && !_owner.resolve()) {
        finish();
        return;
    }
    GameGUI::update(dt);
    if (!_entryEnded) {
        _endEntryTimer.update(dt);
        // A pause holds the entry, and a skip asked for meanwhile, until the
        // conversation resumes. A skip stops the voice and ends the entry,
        // whatever it waits for.
        if (!_paused) {
            if (_entrySkipped && isSkippableEntry()) {
                stopVoice();
                endCurrentEntry();
            } else if (_endEntryTimer.elapsed() && !isWaiting()) {
                endCurrentEntry();
            }
        }
    } else if (_pendingEntry) {
        _endEntryTimer.update(dt);
        // A skip ends the hold of a reply taken without a choice in the same
        // way.
        if (!_paused) {
            const bool skipped = _entrySkipped && isSkippableEntry();
            if (skipped || (_endEntryTimer.elapsed() && !isWaiting())) {
                if (skipped) {
                    stopVoice();
                }
                const int index = *_pendingEntry;
                _pendingEntry.reset();
                loadEntry(index);
            }
        }
    }
}

CameraType Conversation::getCamera(int &cameraId) const {
    if (_animatedView) {
        return CameraType::Animated;
    }
    if (_shotCamera == CameraType::Static) {
        cameraId = _shotCameraId;
    }
    return _shotCamera;
}

void Conversation::stopParticipant(const Object &object) {
    const auto dialogOwner = _owner.resolve();
    if (!_dialog || _entryEnded || !dialogOwner || object.dialogOwner() != dialogOwner) {
        return;
    }
    if (&object == dialogOwner.get()) {
        finish(true, !_endReached);
        return;
    }
    auto found = std::find_if(_dialogParticipants.begin(), _dialogParticipants.end(),
                              [&](const auto &ref) { return ref.resolve().get() == &object; });
    if (found == _dialogParticipants.end()) {
        return;
    }
    const auto participant = found->resolve();
    _dialogParticipants.erase(found);
    participant->setDialogOwner(nullptr);
    participant->setIsInConversation(false);
}

void Conversation::pause() {
    _paused = true;
}

void Conversation::resume() {
    _paused = false;
}

std::optional<int> Conversation::AutoSkip::trySkipReply() {
    if (!enabled || replies.empty()) {
        return std::optional<int>();
    }
    auto reply = replies.front();
    replies.pop();
    return reply;
}

std::optional<bool> Conversation::AutoSkip::trySkipEntry() {
    if (!enabled || entries.empty()) {
        return std::optional<bool>();
    }
    auto entry = entries.front();
    entries.pop();
    return entry;
}

} // namespace game

} // namespace reone
