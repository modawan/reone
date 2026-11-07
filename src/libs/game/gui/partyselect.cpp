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

#include "reone/game/gui/partyselect.h"

#include <array>
#include <utility>

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/portrait.h"
#include "reone/game/portraits.h"
#include "reone/game/script/runner.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/script/types.h"
#include "reone/system/logutil.h"


using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::script;

namespace reone {

namespace game {

static constexpr int kPartySelectionTutorial = 41;

static int g_strRefAdd = 38455;
static int g_strRefRemove = 38456;
static constexpr int kStrRefCannotChangeHere = 108426;
static constexpr int kStrRefCannotChangeInCombat = 126692;
static constexpr int kStrRefCannotChangeWhileUnconscious = 126693;

static glm::vec3 g_kotorColorOn = {0.984314f, 1.0f, 0};
// Neither game specifies an in-party colour in the layout. K1 uses green
// for an active companion.
static glm::vec3 g_kotorColorAdded = {0, 0.831373f, 0.090196f};
static glm::vec3 g_tslColorAdded = {1.0f, 1.0f, 1.0f};

PartySelection::PartySelection(Game &game, ServicesView &services) :
    GameGUI(game, services) {

    if (game.isTSL()) {
        _resRef = "partyselect_p";
    } else {
        _resRef = "partyselection";
    }
}

void PartySelection::onGUILoaded() {
    loadBackground(BackgroundType::Menu);

    bindControls();

    if (_game.isTSL()) {
        // LBL_BAR1 and LBL_BAR2 are not a section-title strip here: they are
        // authored twenty pixels apart with the available-slot count centred
        // between them. Collapsing them the way the in-game menus do strands
        // the count below the strip, so the bars keep their authored places
        // and only take the shell tint.
        tintK2InGameHeader();
        for (auto &control : {
                 _controls.LBL_NA0, _controls.LBL_NA1, _controls.LBL_NA2, _controls.LBL_NA3,
                 _controls.LBL_NA4, _controls.LBL_NA5, _controls.LBL_NA6, _controls.LBL_NA7,
                 _controls.LBL_NA8, _controls.LBL_NA9, _controls.LBL_NA10, _controls.LBL_NA11}) {
            control->setTintBorderFill(true);
        }
        useK2ShellTitle(_controls.LBL_TITLE);
        for (auto &button : {_controls.BTN_ACCEPT, _controls.BTN_DONE, _controls.BTN_BACK}) {
            enableK2ButtonBodyFill(button);
        }
    }

    for (int i = 0; i < npcCount(); ++i) {
        ToggleButton &button = getNpcButton(i);
        button.setOnColor(g_kotorColorOn);
        button.setBorderColorOverride(_game.isTSL() ? g_tslColorAdded : g_kotorColorAdded);
        button.setUseBorderColorOverride(false);
    }

    bindEventHandlers();
}

void PartySelection::bindEventHandlers() {
    _controls.BTN_ACCEPT->setOnClick([this]() {
        onAcceptButtonClick();
    });
    _controls.BTN_DONE->setOnClick([this]() {
        std::array<bool, kMaxNpcCount> wasMember {};
        for (int npc = 0; npc < npcCount(); ++npc) {
            wasMember[npc] = _game.party().isMember(npc);
        }
        // A selection the party cannot take leaves it as it was, and the exit
        // script still runs: it may be what moves the story on.
        auto composition = [this]() {
            std::vector<std::pair<int, const Creature *>> result;
            for (const auto &member : _game.party().members()) {
                result.emplace_back(member.npc, member.creature.get());
            }
            return result;
        };
        const auto before = composition();
        if (!_game.reconcilePartySelection(_selection.request())) {
            warn(before == composition() ? "Party selection: party left unchanged"
                                         : "Party selection: party changed but does not match the selection");
        }
        // A companion that left the party goes with the screen.
        for (int npc = 0; npc < npcCount(); ++npc) {
            if (wasMember[npc] && !_game.party().isMember(npc)) {
                _materialized[npc] = true;
            }
        }
        releaseMaterialized();
        _game.openInGame();
        if (!_context.exitScript.empty()) {
            _game.scriptRunner().run(_context.exitScript);
        }
    });
    _controls.BTN_BACK->setOnClick([this]() {
        if (_cancelLocked) {
            return;
        }
        releaseMaterialized();
        _game.openInGame();
        if (!_context.exitScript.empty()) {
            _game.scriptRunner().run(_context.exitScript);
        }
    });
    _controls.BTN_NPC0->setOnClick([this]() {
        onNpcButtonClick(0);
    });
    _controls.BTN_NPC1->setOnClick([this]() {
        onNpcButtonClick(1);
    });
    _controls.BTN_NPC2->setOnClick([this]() {
        onNpcButtonClick(2);
    });
    _controls.BTN_NPC3->setOnClick([this]() {
        onNpcButtonClick(3);
    });
    _controls.BTN_NPC4->setOnClick([this]() {
        onNpcButtonClick(4);
    });
    _controls.BTN_NPC5->setOnClick([this]() {
        onNpcButtonClick(5);
    });
    _controls.BTN_NPC6->setOnClick([this]() {
        onNpcButtonClick(6);
    });
    _controls.BTN_NPC7->setOnClick([this]() {
        onNpcButtonClick(7);
    });
    _controls.BTN_NPC8->setOnClick([this]() {
        onNpcButtonClick(8);
    });

    if (_game.isTSL()) {
        _controls.BTN_NPC9->setOnClick([this]() {
            onNpcButtonClick(9);
        });
        _controls.BTN_NPC10->setOnClick([this]() {
            onNpcButtonClick(10);
        });
        _controls.BTN_NPC11->setOnClick([this]() {
            onNpcButtonClick(11);
        });
    }
}

void PartySelection::prepare(const PartySelectionContext &ctx) {
    // In KotOR the screen opens with the player character leading.
    if (!_game.isTSL()) {
        _game.party().leadWithPlayerCharacter();
    }
    _gui->setEventListener(*this);
    bindEventHandlers();
    _game.requestTutorialWindow(kPartySelectionTutorial);

    _context = ctx;
    _selectedNpc = -1;
    refreshNpcButtons();

    Party &party = _game.party();
    std::vector<Label *> charLabels {
        _controls.LBL_CHAR0.get(),
        _controls.LBL_CHAR1.get(),
        _controls.LBL_CHAR2.get(),
        _controls.LBL_CHAR3.get(),
        _controls.LBL_CHAR4.get(),
        _controls.LBL_CHAR5.get(),
        _controls.LBL_CHAR6.get(),
        _controls.LBL_CHAR7.get(),
        _controls.LBL_CHAR8.get(),
    };
    std::vector<Label *> naLabels {
        _controls.LBL_NA0.get(),
        _controls.LBL_NA1.get(),
        _controls.LBL_NA2.get(),
        _controls.LBL_NA3.get(),
        _controls.LBL_NA4.get(),
        _controls.LBL_NA5.get(),
        _controls.LBL_NA6.get(),
        _controls.LBL_NA7.get(),
        _controls.LBL_NA8.get()};
    if (_game.isTSL()) {
        charLabels.push_back(_controls.LBL_CHAR9.get());
        charLabels.push_back(_controls.LBL_CHAR10.get());
        charLabels.push_back(_controls.LBL_CHAR11.get());
        naLabels.push_back(_controls.LBL_NA9.get());
        naLabels.push_back(_controls.LBL_NA10.get());
        naLabels.push_back(_controls.LBL_NA11.get());
    }

    std::vector<bool> usable(npcCount(), false);
    for (int i = 0; i < npcCount(); ++i) {
        ToggleButton &BTN_NPC = getNpcButton(i);
        Label &LBL_CHAR = *charLabels[i];
        Label &LBL_NA = *naLabels[i];

        // A companion with no creature yet is brought into being for its
        // portrait, outside the area.
        const bool existed = static_cast<bool>(party.getAvailableMember(i));
        auto member = party.getAvailableMember(i, true);
        _materialized[i] = !existed && member;
        usable[i] = static_cast<bool>(member);
        if (member) {
            auto portrait = member->portrait();
            BTN_NPC.setDisabled(false);
            BTN_NPC.setVisible(true);
            LBL_CHAR.setBorderFill(std::move(portrait));
            LBL_NA.setVisible(false);
        } else {
            BTN_NPC.setDisabled(true);
            BTN_NPC.setVisible(true);
            LBL_CHAR.setBorderFill(std::shared_ptr<Texture>(nullptr));
            LBL_NA.setVisible(true);
        }
    }
    if (_game.isTSL()) {
        // Handmaiden/Disciple and Mira/Hanharr are authored on one shared
        // slot each. The not-available overlay belongs to the slot, not to
        // each NPC: with one of the pair available, the other's overlay and
        // disabled button frame would draw over the occupant's portrait, and
        // with both away a single overlay is enough.
        static constexpr std::pair<int, int> kSharedSlots[] {
            {4, 11}, // Handmaiden, Disciple
            {7, 10}, // Mira, Hanharr
        };
        for (const auto &slot : kSharedSlots) {
            bool firstAvailable = party.isMemberAvailable(slot.first);
            bool secondAvailable = party.isMemberAvailable(slot.second);
            naLabels[slot.first]->setVisible(!firstAvailable && !secondAvailable);
            naLabels[slot.second]->setVisible(false);
            getNpcButton(slot.first).setVisible(firstAvailable || !secondAvailable);
            getNpcButton(slot.second).setVisible(secondAvailable);
        }
    }

    _selection.openOn(party, std::move(usable), ctx.forceNpc1, ctx.forceNpc2);
    // KotOR's call has no say over cancelling, so leaving without confirming
    // is held back while it would lose a forced companion. TSL's call has a
    // cancel argument, not read here, and Cancel there stays as it was.
    _cancelLocked = !_game.isTSL() && _selection.forcesNewcomer();
    _controls.BTN_BACK->setDisabled(_cancelLocked);
    refreshSelectionBorders();
    refreshAvailableCount();
}

void PartySelection::refreshSelectionBorders() {
    for (int npc = 0; npc < npcCount(); ++npc) {
        getNpcButton(npc).setUseBorderColorOverride(_selection.isSelected(npc));
    }
}

ToggleButton &PartySelection::getNpcButton(int npc) {
    std::vector<ToggleButton *> npcButtons {
        _controls.BTN_NPC0.get(),
        _controls.BTN_NPC1.get(),
        _controls.BTN_NPC2.get(),
        _controls.BTN_NPC3.get(),
        _controls.BTN_NPC4.get(),
        _controls.BTN_NPC5.get(),
        _controls.BTN_NPC6.get(),
        _controls.BTN_NPC7.get(),
        _controls.BTN_NPC8.get(),
        _controls.BTN_NPC9.get(),
        _controls.BTN_NPC10.get(),
        _controls.BTN_NPC11.get()};
    return *npcButtons[npc];
}

bool PartySelection::isSupportedNpc(int npc) const {
    return npc >= 0 && npc < npcCount();
}

int PartySelection::npcCount() const {
    return _game.isTSL() ? kTSLNpcCount : kKotorNpcCount;
}

std::optional<int> PartySelection::partyLockStrRef() const {
    if (!_game.isTSL() || _context.openedByScript) {
        return std::nullopt;
    }
    const Party &party = _game.party();
    auto player = party.player();
    if (_game.module()->area()->isUnescapable() || (player && !player->isPlayerCreated())) {
        return kStrRefCannotChangeHere;
    }
    if (!party.isReadyToTravel()) {
        return kStrRefCannotChangeWhileUnconscious;
    }
    if (party.isInCombat()) {
        return kStrRefCannotChangeInCombat;
    }
    return std::nullopt;
}

void PartySelection::onAcceptButtonClick() {
    if (auto lockStrRef = partyLockStrRef()) {
        _game.showMessagePopup(_services.resource.strings.getText(*lockStrRef));
        return;
    }
    if (_selectedNpc == -1)
        return;

    // A forced companion stays, and a full selection takes no one else.
    const bool changed = _selection.isSelected(_selectedNpc)
                             ? _selection.remove(_selectedNpc)
                             : _selection.add(_selectedNpc);
    if (changed) {
        refreshSelectionBorders();
        refreshAvailableCount();
        refreshAcceptButton();
    }
}

void PartySelection::clearSelection() {
    releaseMaterialized();
    GameGUI::clearSelection();
}

// Leaving the screen destroys each companion it brought into being that did
// not join the party, and each that left the party.
void PartySelection::releaseMaterialized() {
    for (int npc = 0; npc < npcCount(); ++npc) {
        if (_materialized[npc] && !_game.party().isMember(npc)) {
            _game.killRosterCreature({RosterKind::Npc, npc});
        }
        _materialized[npc] = false;
    }
}

void PartySelection::refreshAvailableCount() {
    _controls.LBL_COUNT->setTextMessage(std::to_string(_selection.availableSlots()));
}

void PartySelection::refreshAcceptButton() {
    std::string text(_services.resource.strings.getText(_selection.isSelected(_selectedNpc) ? g_strRefRemove : g_strRefAdd));
    _controls.BTN_ACCEPT->setTextMessage(text);
}

void PartySelection::onNpcButtonClick(int npc) {
    if (!isSupportedNpc(npc)) {
        return;
    }

    _selectedNpc = npc;
    refreshNpcButtons();
    refreshAcceptButton();
}

void PartySelection::refreshNpcButtons() {
    for (int i = 0; i < npcCount(); ++i) {
        ToggleButton &button = getNpcButton(i);

        if ((i == _selectedNpc && !button.isOn()) ||
            (i != _selectedNpc && button.isOn())) {

            button.toggle();
        }
    }
}

} // namespace game

} // namespace reone
