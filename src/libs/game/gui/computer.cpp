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

#include "reone/game/gui/computer.h"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/d20/skills.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr char kHumResRef[] = "computer_lp_04";
static constexpr float kHumGain = 30.0f / 127.0f;
static constexpr char kComputerSpikeTag[] = "k_computer_spike";
static constexpr char kRepairPartTag[] = "k_repair_part";
static constexpr float kStaticBandAlpha = 0.4f;
static constexpr char kComputerTypesResRef[] = "comptypes";

void ComputerGUI::init() {
    Conversation::init();
    _cameraGUI.init();
}

// A camera shot takes the conversation's own input, its skip, as the panel
// does; the camera overlay sees the input first.
bool ComputerGUI::handle(const input::Event &event) {
    if (_presentation == Presentation::Camera) {
        return _cameraGUI.handle(event) || handleConversationInput(event);
    }
    return Conversation::handle(event);
}

void ComputerGUI::update(float dt) {
    Conversation::update(dt);
    if (_presentation == Presentation::Camera) {
        _cameraGUI.update(dt);
    }
}

void ComputerGUI::render() {
    if (_presentation == Presentation::Camera) {
        _cameraGUI.render();
    } else {
        Conversation::render();
    }
}

void ComputerGUI::onStart() {
    applyPanelFrame();
    loadBackground(BackgroundType::Computer, _computerType);
    _presentation = Presentation::Hidden;
}

void ComputerGUI::onFinish() {
    _presentation = Presentation::Hidden;
    stopHum();
}

void ComputerGUI::onLoadEntry() {
    int cameraId = 0;
    setPresentation(getCamera(cameraId) == CameraType::Static ? Presentation::Camera : Presentation::Normal);
    if (_presentation == Presentation::Camera) {
        _cameraGUI.clearSelection();
    }
}

void ComputerGUI::onEntryEnded() {
    setPresentation(Presentation::Normal);
}

void ComputerGUI::setPresentation(Presentation presentation) {
    if (presentation == _presentation) {
        return;
    }
    _presentation = presentation;
    if (_presentation == Presentation::Camera) {
        stopHum();
    } else if (_presentation == Presentation::Normal) {
        playHum();
    }
}

void ComputerGUI::playHum() {
    stopHum();
    _hum = _services.audio.mixer.play(_humClip, AudioType::Sound, kHumGain, true);
}

void ComputerGUI::stopHum() {
    if (!_hum) {
        return;
    }
    _hum->stop();
    _hum.reset();
}

void ComputerGUI::preload(IGUI &gui) {
    GameGUI::preload(gui);
}

void ComputerGUI::onGUILoaded() {
    bindControls();
    configureMessage();
    configureReplies();
    configureSkillLabels();
    configureBands();
    _humClip = _services.resource.audioClips.get(kHumResRef);
}

void ComputerGUI::configureMessage() {
    _controls.LB_MESSAGE->setProtoMatchContent(true);
    _controls.LB_MESSAGE->protoItem().setHilightColor(_hilightColor);
    _controls.LB_MESSAGE->protoItem().setTextColor(_baseColor);
}

void ComputerGUI::configureReplies() {
    _controls.LB_REPLIES->setProtoMatchContent(true);
    // The selected reply stays selected when the pointer moves off it.
    _controls.LB_REPLIES->setKeepsSelection(true);
    _controls.LB_REPLIES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_REPLIES->protoItem().setTextColor(_baseColor);
    _controls.LB_REPLIES->setOnItemClick([this](auto &item) {
        int replyIdx = stoi(item);
        pickReply(replyIdx);
    });
}

void ComputerGUI::configureSkillLabels() {
    const auto &computerUse = *_services.game.skills.get(SkillType::ComputerUse);
    const auto &repair = *_services.game.skills.get(SkillType::Repair);
    _controls.LBL_COMP_SKILL->setTextMessage(computerUse.name);
    _controls.LBL_REP_SKILL->setTextMessage(repair.name);
    if (isTSL()) {
        return;
    }
    _controls.LBL_COMP_SKILL_ICON->setBorderFill(computerUse.icon);
    _controls.LBL_REP_SKILL_ICON->setBorderFill(repair.icon);
}

void ComputerGUI::configureBands() {
    if (isTSL()) {
        return;
    }
    for (const auto &band : {_controls.LBL_STATIC1, _controls.LBL_STATIC2, _controls.LBL_STATIC3, _controls.LBL_STATIC4}) {
        band->setBorderAlpha(kStaticBandAlpha);
    }
}

void ComputerGUI::applyPanelFrame() {
    if (isTSL()) {
        return;
    }
    // Without the table, or with no rows in it, the panel keeps its frame
    // and its type. A computer type without a row takes the first row.
    const auto types = _services.resource.twoDas.get(kComputerTypesResRef);
    if (!types || types->getRowCount() == 0) {
        return;
    }
    const int type = static_cast<int>(_dialog->computerType);
    _computerType = type < types->getRowCount() ? type : 0;
    _gui->rootControl().setBorderFill(types->getString(_computerType, "computerbackground"));
}

void ComputerGUI::refreshSkills() {
    const auto leader = _game.party().getLeader();
    int spikes = 0;
    int parts = 0;
    for (const auto &item : _game.party().sharedInventoryReceiver(leader)->items()) {
        if (item->tag() == kComputerSpikeTag) {
            spikes += item->stackSize();
        } else if (item->tag() == kRepairPartTag) {
            parts += item->stackSize();
        }
    }
    _controls.LBL_COMP_SKILL_VAL->setTextMessage(std::to_string(leader->getUnopposedSkillRank(SkillType::ComputerUse)));
    _controls.LBL_REP_SKILL_VAL->setTextMessage(std::to_string(leader->getUnopposedSkillRank(SkillType::Repair)));
    _controls.LBL_COMP_SPIKES_VAL->setTextMessage(std::to_string(spikes));
    _controls.LBL_REP_UNITS_VAL->setTextMessage(std::to_string(parts));
}

void ComputerGUI::setMessage(std::string message) {
    refreshSkills();

    ListBox::Item item;
    item.text = std::move(message);

    _controls.LB_MESSAGE->clearItems();
    _controls.LB_MESSAGE->addItem(std::move(item));
    // The replies of the entry are listed when it ends.
    _controls.LB_REPLIES->clearItems();
}

void ComputerGUI::setReplyLines(std::vector<std::string> lines) {
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
    // The numbers are written when an entry's message is set and again when
    // its replies are listed, as the entry ends.
    refreshSkills();
}

std::shared_ptr<ListBox> ComputerGUI::replyList() const {
    return _controls.LB_REPLIES;
}

} // namespace game

} // namespace reone
