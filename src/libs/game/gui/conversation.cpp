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

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/resources.h"
#include "reone/system/logutil.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/script/runner.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr float kDefaultEntryDuration = 3.0f;

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

void Conversation::start(const std::shared_ptr<Dialog> &dialog, const std::shared_ptr<Object> &owner) {
    debug("Start " + dialog->resRef, LogChannel::Conversation);

    _paused = false;
    _dialog = dialog;
    _owner = owner;

    loadConversationBackground();
    loadCameraModel();
    onStart();
    loadStartEntry();
}

static BackgroundType getBackgroundType(ComputerType compType) {
    switch (compType) {
    case ComputerType::Rakatan:
        return BackgroundType::Computer1;
    default:
        return BackgroundType::Computer0;
    }
}

void Conversation::loadConversationBackground() {
    if (_dialog->conversationType == ConversationType::Computer) {
        loadBackground(getBackgroundType(_dialog->computerType));
    } else {
        loadBackground(BackgroundType::None);
    }
}

void Conversation::loadCameraModel() {
    std::string modelResRef(_dialog->cameraModel);
    _cameraModel = modelResRef.empty() ? nullptr : _services.resource.models.get(modelResRef);
}

void Conversation::onStart() {
}

void Conversation::loadStartEntry() {
    int entryIdx = indexOfFirstActive(_dialog->startEntries);
    if (entryIdx == -1) {
        debug("Finish (no active start entry)", LogChannel::Conversation);
        finish();
        return;
    }
    loadEntry(entryIdx, true);
}

int Conversation::indexOfFirstActive(const std::vector<Dialog::EntryReplyLink> &links) {
    for (auto &link : links) {
        if (isLinkActive(link)) {
            return link.index;
        }
    }
    return -1;
}

bool Conversation::isLinkActive(const Dialog::EntryReplyLink &link) {
    std::optional<bool> active;
    if (!link.active.empty()) {
        active = evaluateCondition(link.active, link.params);
        if (link.notActive) {
            active = !active.value();
        }
    }
    std::optional<bool> active2;
    if (!link.active2.empty()) {
        active2 = evaluateCondition(link.active2, link.params2);
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
    return _game.scriptRunner().run(scriptResRef, makeScriptArgs(_owner ? _owner->id() : 0, params)) != 0;
}

void Conversation::runScript(const std::string &scriptResRef, const Dialog::EntryReply::ActionParams &params) {
    if (!scriptResRef.empty()) {
        _game.scriptRunner().run(scriptResRef, makeScriptArgs(_owner ? _owner->id() : 0, params));
    }
}

void Conversation::runScripts(const Dialog::EntryReply &node) {
    runScript(node.script, node.actionParams);
    runScript(node.script2, node.actionParams2);
}

void Conversation::applyStatusSummaryEntries(const Dialog::EntryReply &node) {
    if (!node.quest.empty()) {
        _game.journal().addEntry(node.quest, static_cast<int>(node.questEntry));
    }
    _game.awardPlotXPByIndex(node.plotIndex, node.plotXPPercentage);
}

void Conversation::finish() {
    _paused = false;
    onFinish();

    _game.openInGame();

    // Run EndConversation script
    if (!_dialog->endScript.empty()) {
        _game.scriptRunner().run(_dialog->endScript, _owner->id());
    }
}

void Conversation::onFinish() {
}

void Conversation::cleanupForModuleTransition() {
    _paused = false;
    if (!_dialog) {
        return;
    }
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
    }
    _lipAnimation.reset();
    onFinish();
}

void Conversation::loadEntry(int index, bool start) {
    debug("Load entry " + std::to_string(index), LogChannel::Conversation);
    _currentEntry = &_dialog->getEntry(index);

    applyStatusSummaryEntries(*_currentEntry);

    std::string entryText(_game.substituteCustomTokens(_currentEntry->text));
    setMessage(entryText);
    loadReplies();
    loadVoiceOver();
    scheduleEndOfEntry();
    onLoadEntry();

    // Conversation is a one-liner if there is exactly one empty reply that has no entries
    bool oneLiner = false;
    if (start && _replies.size() == 1ll) {
        const Dialog::EntryReply &reply = *_replies[0];
        oneLiner = reply.text.empty() && reply.entries.empty();
    }
    if (oneLiner) {
        _game.setBarkBubbleText(std::move(entryText), _entryDuration);
        debug("Dialog: finish (one-liner)");
        finish();
        return;
    }

    // Run entry scripts
    runScripts(*_currentEntry);

    if (_autoSkip) {
        if (std::optional<bool> skip = _autoSkip->trySkipEntry()) {
            if (skip.value() && !_paused) {
                endCurrentEntry();
            }
        }
    }
}

void Conversation::onLoadEntry() {
}

void Conversation::loadVoiceOver() {
    // Stop previous voice, if any
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
        _lipAnimation.reset();
    }

    // Play current voice over either from Sound or from VO_ResRef
    std::string voiceResRef;
    if (!_currentEntry->sound.empty()) {
        voiceResRef = _currentEntry->sound;
        _lipAnimation = _services.resource.lips.get(_currentEntry->sound);
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

static std::string getCameraAnimationName(int ordinal) {
    return str(boost::format("cut%03dw") % (ordinal - 1200 + 1));
}

void Conversation::scheduleEndOfEntry() {
    float duration = kDefaultEntryDuration;

    if (_cameraModel && (_currentEntry->waitFlags & Dialog::WaitFlags::waitAnimFinish)) {
        std::string animName(getCameraAnimationName(_currentEntry->cameraAnimation));
        std::shared_ptr<Animation> animation(_cameraModel->getAnimation(animName));
        if (animation) {
            duration = animation->length();
        }
    } else if (_currentEntry->delay != -1) {
        duration = static_cast<float>(_currentEntry->delay);
    } else if (_currentVoice) {
        duration = _currentVoice->duration();
    }

    _entryEnded = false;
    _entryDuration = duration;
    _endEntryTimer.reset(duration);
}

void Conversation::loadReplies() {
    _replies.clear();
    for (auto &link : _currentEntry->replies) {
        if (isLinkActive(link)) {
            _replies.push_back(&_dialog->getReply(link.index));
        }
    }

    // If there is only one empty reply, pick it automatically when the current entry ends
    _autoPickFirstReply = _replies.size() == 1ll && _replies.front()->text.empty();

    refreshReplies();
}

static std::string getReplyText(const Dialog::EntryReply &reply, int index, const Game &game) {
    return str(boost::format("%d. %s") % (index + 1) % (reply.text.empty() ? "[empty]" : game.substituteCustomTokens(reply.text)));
}

void Conversation::refreshReplies() {
    std::vector<std::string> lines;
    if (!_autoPickFirstReply) {
        for (size_t i = 0; i < _replies.size(); ++i) {
            lines.push_back(getReplyText(*_replies[i], static_cast<int>(i), _game));
        }
    }
    setReplyLines(std::move(lines));
}

void Conversation::pickReply(int index) {
    debug("Pick reply " + std::to_string(index), LogChannel::Conversation);
    const Dialog::EntryReply &reply = *_replies[index];

    applyStatusSummaryEntries(reply);

    // Run reply scripts
    runScripts(reply);

    int entryIdx = indexOfFirstActive(reply.entries);
    if (entryIdx == -1) {
        debug("Finish (no active entries)", LogChannel::Conversation);
        finish();
        return;
    }
    loadEntry(entryIdx);
}

bool Conversation::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::MouseButtonDown:
        if (handleMouseButtonDown(event.button))
            return true;
        break;
    case input::EventType::KeyUp:
        if (handleKeyUp(event.key))
            return true;
        break;
    default:
        break;
    }

    return GameGUI::handle(event);
}

bool Conversation::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button == input::MouseButton::Left && !_entryEnded && isSkippableEntry()) {
        endCurrentEntry();
        return true;
    }
    return false;
}

bool Conversation::isSkippableEntry() const {
    return g_allEntriesSkippable || (_dialog->isSkippable() && !_paused);
}

void Conversation::endCurrentEntry() {
    _entryEnded = true;

    // Stop voice over, if any
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
    }

    onEntryEnded();

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

bool Conversation::handleKeyUp(const input::KeyEvent &event) {
    if (!_entryEnded) {
        return false;
    }

    using IntKeyCode = std::underlying_type_t<input::KeyCode>;
    auto code = static_cast<IntKeyCode>(event.code);
    auto key1 = static_cast<IntKeyCode>(input::KeyCode::Key1);
    auto key9 = static_cast<IntKeyCode>(input::KeyCode::Key9);

    if (code < key1 || code > key9) {
        return false;
    }

    size_t index = code - key1;
    if (index < _replies.size()) {
        pickReply(index);
    } else {
        debug("Invalid reply index: " + std::to_string(index), LogChannel::Conversation);
    }
    return true;
}

void Conversation::update(float dt) {
    GameGUI::update(dt);
    if (!_entryEnded) {
        _endEntryTimer.update(dt);
        if (!_paused && (_endEntryTimer.elapsed() || (_currentVoice && !_currentVoice->isPlaying()))) {
            endCurrentEntry();
        }
    }
}

CameraType Conversation::getCamera(int &cameraId) const {
    std::string cameraModel(_dialog->cameraModel);
    if (!cameraModel.empty()) {
        return CameraType::Animated;
    }
    if (_currentEntry->cameraId != 0) {
        cameraId = _currentEntry->cameraId;
        return CameraType::Static;
    }
    return CameraType::Dialog;
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
