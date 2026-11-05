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

#include "reone/game/gui/ingame/map.h"

#include <algorithm>
#include <limits>

#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/party.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/resource/strings.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kStrRefMapNote = 349;
static constexpr int kStrRefTravelUnavailable = 38451;
static constexpr int kStrRefRejoinPartyFirst = 38452;
static constexpr int kStrRefEnemiesNearby = 38462;

static constexpr int kReturnToBaseTutorial = 39;

/** The ranges.2da row a companion in combat spots at. */
static constexpr int kCombatSpotRangeRow = 18;

void MapMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    _controls.BTN_UP->setOnClick([this]() {
        if (--_selectedNoteIdx == -1) {
            _selectedNoteIdx = static_cast<int>(_notes.size() - 1);
        }
        refreshSelectedNote();
    });
    _controls.BTN_DOWN->setOnClick([this]() {
        if (++_selectedNoteIdx == static_cast<int>(_notes.size())) {
            _selectedNoteIdx = 0;
        }
        refreshSelectedNote();
    });

    if (_game.isTSL()) {
        // TSL's map offers no return.
        _controls.BTN_RETURN->setDisabled(true);
        fillK2SectionStrip(_controls.LBL_BAR1, _controls.LBL_BAR2);
        useK2ShellTitle(_controls.LBL_TITLE);
        enableK2ButtonBodyFill(_controls.BTN_RETURN);
        enableK2ButtonBodyFill(_controls.BTN_EXIT);
    }

    if (!_game.isTSL()) {
        // These two one-line buttons are authored as top-aligned because the
        // font filled their short controls. Independent text scaling
        // otherwise leaves the smaller glyphs pinned to one edge.
        _controls.BTN_PRTYSLCT->setTextAlignment(Control::TextAlign::CenterCenter);
        _controls.BTN_RETURN->setTextAlignment(Control::TextAlign::CenterCenter);
        _controls.BTN_PRTYSLCT->setOnClick([this]() {
            // The party selection opens only while travel is open.
            if (_travelRefusal != 0) {
                _game.showMessagePopup(_services.resource.strings.getText(_travelRefusal));
                return;
            }
            _game.openPartySelection(PartySelectionContext());
        });
        // The first press shows its tutorial, which then presses again.
        _controls.BTN_RETURN->setOnClick([this]() {
            if (!_game.requestTutorialWindow(kReturnToBaseTutorial, [this]() { offerReturn(); })) {
                offerReturn();
            }
        });
    }
}

void MapMenu::offerReturn() {
    if (!_returnOpen) {
        const int refusal = _travelRefusal != 0 ? _travelRefusal : kStrRefTravelUnavailable;
        _game.showMessagePopup(_services.resource.strings.getText(refusal));
        return;
    }
    _game.showConfirmPopup(_services.resource.strings.getText(_game.party().returnQueryStrRef()), [this]() {
        _game.returnToBase();
    });
}

void MapMenu::render() {
    GameGUI::render();

    const Control::Extent &extent = _controls.LBL_Map->extent();

    glm::vec4 bounds(
        _gui->controlOffset().x + extent.left,
        _gui->controlOffset().y + extent.top,
        extent.width,
        extent.height);

    _game.map().render(Map::Mode::Default, bounds, _gui->scale(), _gui->pulsingAlpha());
}

void MapMenu::refreshControls() {
    _controls.LBL_Area->setTextMessage(_game.module()->area()->localizedName());
    _notes.clear();

    for (auto &object : _game.module()->area()->getObjectsByType(ObjectType::Waypoint)) {
        auto waypoint = std::static_pointer_cast<Waypoint>(object);
        if (waypoint->isMapNoteEnabled() && !waypoint->mapNote().empty()) {
            _notes.push_back(waypoint);
        }
    }

    _selectedNoteIdx = 0;
    refreshSelectedNote();
    if (!_game.isTSL()) {
        refreshTravel();
    }
}

void MapMenu::refreshTravel() {
    const Party &party = _game.party();
    _travelRefusal = travelRefusal();
    const bool open = _travelRefusal == 0;
    _returnOpen = open && party.isReturnOffered();
    _controls.BTN_RETURN->setTextMessage(_services.resource.strings.getText(party.returnStrRef()));
    _controls.BTN_RETURN->setTextColor(_returnOpen ? kTravelOpenTextColor : kTravelRefusedTextColor);
    _controls.BTN_RETURN->setBorderColor(_returnOpen ? kTravelOpenBorderColor : kTravelRefusedBorderColor);
    _controls.BTN_PRTYSLCT->setTextColor(open ? kTravelOpenTextColor : kTravelRefusedTextColor);
    _controls.BTN_PRTYSLCT->setBorderColor(open ? kTravelOpenBorderColor : kTravelRefusedBorderColor);
}

// A creature spots as far as it sees, except a companion in combat, which
// spots at its own row's range.
float MapMenu::spotDistance(const Creature &creature) const {
    const float sight = creature.perception().sightRange;
    if (!creature.isInCombat() || &creature == _game.party().getLeader().get()) {
        return sight;
    }
    const auto range = _services.game.spells.findRange(kCombatSpotRangeRow);
    return range ? range->get().primary.value_or(sight) : sight;
}

// Travel is refused in an area the party can't leave, while the party is
// apart, and while a creature hostile to a member sees that member. The
// creatures looked at are those east of the westmost point a member spots to.
int MapMenu::travelRefusal() const {
    auto &area = *_game.module()->area();
    if (area.isUnescapable()) {
        return kStrRefTravelUnavailable;
    }
    const Party &party = _game.party();
    if (!party.isReadyToTravel()) {
        return kStrRefRejoinPartyFirst;
    }
    float westmost = std::numeric_limits<float>::max();
    for (auto &member : party.members()) {
        if (member.creature) {
            westmost = std::min(westmost, member.creature->position().x - spotDistance(*member.creature));
        }
    }
    for (auto &object : area.getObjectsByType(ObjectType::Creature)) {
        const auto &watcher = static_cast<const Creature &>(*object);
        if (watcher.position().x < westmost) {
            continue;
        }
        for (auto &member : party.members()) {
            if (member.creature && member.creature->getReputationToward(watcher) <= 10 &&
                watcher.perception().sees(member.creature->id())) {
                return kStrRefEnemiesNearby;
            }
        }
    }
    return 0;
}

void MapMenu::refreshSelectedNote() {
    std::shared_ptr<Waypoint> note;

    if (!_notes.empty()) {
        note = _notes[_selectedNoteIdx];

        std::string text(_services.resource.strings.getText(kStrRefMapNote));
        text += ": ";
        text += note->mapNote();

        _controls.LBL_MapNote->setTextMessage(text);
    }

    _game.map().setSelectedNote(note);
}

} // namespace game

} // namespace reone
