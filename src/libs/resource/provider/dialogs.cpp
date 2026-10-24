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

#include "reone/resource/provider/dialogs.h"

#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resref.h"
#include "reone/resource/strings.h"

namespace reone {

namespace resource {

static constexpr char kEmotionsResRef[] = "emotion";
static constexpr char kAlienVoicesResRef[] = "alienvo";
static constexpr char kEmotionLabelColumn[] = "label";

// SoundExists: 1 the node's sound is a recorded voice; 2 (TSL) it takes a
// variation number; 4 (TSL) the number has been added.
static constexpr int kSoundIsVoice = 1;
static constexpr int kSoundTakesVariation = 2;
static constexpr int kSoundVariationAdded = 4;

std::shared_ptr<Dialog> Dialogs::doGet(std::string resRef) {
    auto dlg = _gffs.get(resRef, ResType::Dlg);
    if (!dlg) {
        return nullptr;
    }
    auto dlgParsed = resource::generated::parseDLG(*dlg);
    auto dialog = loadDialog(dlgParsed);
    dialog->resRef = ResRef(std::move(resRef)).value();
    return dialog;
}

std::unique_ptr<Dialog> Dialogs::loadDialog(const resource::generated::DLG &dlg) {
    auto dialog = std::make_unique<Dialog>();

    dialog->skippable = dlg.Skippable;
    dialog->cameraModel = dlg.CameraModel;
    dialog->endScript = dlg.EndConversation;
    dialog->abortScript = dlg.EndConverAbort;
    dialog->animatedCutscene = dlg.AnimatedCut;
    dialog->entryDelay = dlg.DelayEntry;
    dialog->oldHitCheck = dlg.OldHitCheck != 0;
    dialog->conversationType = static_cast<ConversationType>(dlg.ConversationType);
    dialog->computerType = static_cast<ComputerType>(dlg.ComputerType);

    std::optional<AlienSpeech> alienSpeech;
    if (_game == GameID::TSL) {
        alienSpeech = AlienSpeech {alienSpeechTables(), dlg.AlienRaceOwner};
    }
    for (auto &entry : dlg.EntryList) {
        dialog->entries.push_back(getEntryReply(entry, alienSpeech));
    }
    for (auto &reply : dlg.ReplyList) {
        dialog->replies.push_back(getEntryReply(reply, alienSpeech));
    }
    for (auto &entry : dlg.StartingList) {
        dialog->startEntries.push_back(getEntryReplyLink(entry));
    }
    for (auto &stunt : dlg.StuntList) {
        dialog->stunts.push_back(getStunt(stunt));
    }

    return dialog;
}

// A table the game data lacks finds nothing.
const Dialogs::AlienSpeechTables &Dialogs::alienSpeechTables() {
    if (!_alienSpeechTables) {
        _alienSpeechTables = AlienSpeechTables {_twoDas.get(kEmotionsResRef), _twoDas.get(kAlienVoicesResRef)};
    }
    return *_alienSpeechTables;
}

Dialog::EntryReplyLink Dialogs::getEntryReplyLink(const resource::generated::DLG_EntryReplyList_EntriesRepliesList &dlg) const {
    Dialog::EntryReplyLink link;
    link.index = dlg.Index;
    link.active = dlg.Active;
    link.active2 = dlg.Active2;
    link.notActive = dlg.Not != 0;
    link.notActive2 = dlg.Not2 != 0;
    link.logic = dlg.Logic;
    link.params.ints = {dlg.Param1, dlg.Param2, dlg.Param3, dlg.Param4, dlg.Param5};
    link.params.str = dlg.ParamStrA;
    link.params2.ints = {dlg.Param1b, dlg.Param2b, dlg.Param3b, dlg.Param4b, dlg.Param5b};
    link.params2.str = dlg.ParamStrB;
    link.displayInactive = dlg.DisplayInactive != 0;

    return link;
}

Dialog::EntryReply Dialogs::getEntryReply(const resource::generated::DLG_EntryReplyList &dlg, const std::optional<AlienSpeech> &alienSpeech) const {
    int strRef = dlg.Text.first;

    Dialog::EntryReply entry;
    entry.speaker = dlg.Speaker;
    entry.text = strRef == -1 ? "" : _strings.getText(strRef);
    entry.textStrRef = strRef;
    entry.voResRef = dlg.VO_ResRef;
    entry.script = dlg.Script;
    entry.script2 = dlg.Script2;
    entry.listener = dlg.Listener;
    entry.quest = dlg.Quest;
    entry.questEntry = dlg.QuestEntry;
    entry.plotIndex = dlg.PlotIndex;
    entry.plotXPPercentage = dlg.PlotXPPercentage;
    entry.actionParams.ints = {dlg.ActionParam1, dlg.ActionParam2, dlg.ActionParam3, dlg.ActionParam4, dlg.ActionParam5};
    entry.actionParams.str = dlg.ActionParamStrA;
    entry.actionParams2.ints = {dlg.ActionParam1b, dlg.ActionParam2b, dlg.ActionParam3b, dlg.ActionParam4b, dlg.ActionParam5b};
    entry.actionParams2.str = dlg.ActionParamStrB;
    entry.delay = dlg.Delay;
    entry.waitFlags = dlg.WaitFlags;
    // KotOR plays the node's Sound field. TSL reads no Sound field: a node's
    // sound is its alien speech, which is a recorded voice and takes a
    // variation number. Without a sound, the voice-over plays; with neither,
    // the node has no recorded voice.
    int soundExists = dlg.SoundExists;
    if (alienSpeech) {
        entry.sound = getAlienSpeech(dlg, *alienSpeech);
        if (!entry.sound.empty()) {
            soundExists |= kSoundIsVoice | kSoundTakesVariation;
        }
    } else {
        entry.sound = dlg.Sound;
    }
    if (entry.sound.empty() && entry.voResRef.empty()) {
        soundExists = 0;
    }
    entry.soundExists = (soundExists & kSoundIsVoice) != 0;
    entry.soundTakesVariation = alienSpeech && (soundExists & kSoundTakesVariation) != 0;
    entry.soundVariationAdded = alienSpeech && (soundExists & kSoundVariationAdded) != 0;
    entry.voiceOverOnly = (soundExists & kSoundIsVoice) != 0 && (soundExists & kSoundTakesVariation) == 0 && entry.sound.empty();
    entry.nodeUnskippable = dlg.NodeUnskippable != 0;
    // Fade types 1 and 2 cut at once.
    static constexpr int kFirstCutFade = 1;
    static constexpr int kLastCutFade = 2;
    entry.fadeType = dlg.FadeType;
    entry.fadeColor = dlg.FadeColor;
    entry.fadeDelay = dlg.FadeDelay;
    entry.fadeLength = entry.fadeType >= kFirstCutFade && entry.fadeType <= kLastCutFade ? 0.0f : dlg.FadeLength;
    entry.cameraId = dlg.CameraID;
    entry.cameraAngle = dlg.CameraAngle;
    entry.cameraAnimation = dlg.CameraAnimation;
    entry.camFieldOfView = dlg.CamFieldOfView;
    entry.camHeightOffset = dlg.CamHeightOffset;
    entry.tarHeightOffset = dlg.TarHeightOffset;
    entry.camVidEffect = dlg.CamVidEffect;

    boost::to_lower(entry.speaker);
    boost::to_lower(entry.listener);

    for (auto &link : dlg.RepliesList) {
        entry.replies.push_back(getEntryReplyLink(link));
    }
    for (auto &link : dlg.EntriesList) {
        entry.entries.push_back(getEntryReplyLink(link));
    }
    for (auto &anim : dlg.AnimList) {
        entry.animations.push_back(getParticipantAnimation(anim));
    }

    return entry;
}

// The race is the node's own; a node with no speaker, which the owner speaks,
// otherwise takes the owner's. A race of 0 or less has no alien speech, and
// neither has an emotion or race the tables lack or leave blank.
std::string Dialogs::getAlienSpeech(const resource::generated::DLG_EntryReplyList &dlg, const AlienSpeech &alienSpeech) const {
    int race = dlg.AlienRaceNode;
    if (race <= 0 && dlg.Speaker.empty()) {
        race = alienSpeech.ownerRace;
    }
    const AlienSpeechTables &tables = alienSpeech.tables;
    if (race <= 0 || !tables.emotions || !tables.voices) {
        return "";
    }
    const std::string emotion = tables.emotions->getString(dlg.Emotion, kEmotionLabelColumn);
    if (emotion.empty()) {
        return "";
    }
    return ResRef(tables.voices->getString(race, emotion)).value();
}

Dialog::Stunt Dialogs::getStunt(const resource::generated::DLG_StuntList &dlg) const {
    Dialog::Stunt stunt;
    stunt.participant = boost::to_lower_copy(dlg.Participant);
    stunt.stuntModel = boost::to_lower_copy(dlg.StuntModel);
    return stunt;
}

Dialog::ParticipantAnimation Dialogs::getParticipantAnimation(const resource::generated::DLG_EntryReplyList_AnimList &dlg) const {
    Dialog::ParticipantAnimation anim;
    anim.participant = boost::to_lower_copy(dlg.Participant);
    anim.animation = dlg.Animation;
    return anim;
}

} // namespace resource

} // namespace reone
