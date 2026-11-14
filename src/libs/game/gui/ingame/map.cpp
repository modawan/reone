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
#include "reone/graphics/types.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/resource/provider/textures.h"
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

/** The static the map image shows while the party's map is disabled. */
static constexpr char kMapStaticResRef[] = "lbl_static";

void MapMenu::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();
    _mapFill = _controls.LBL_Map->border().fill;
    _mapFillStyle = _controls.LBL_Map->border().fillStyle;

    _controls.BTN_EXIT->setOnClick([this]() {
        _game.openInGame();
    });
    // Up moves on to the note further along world X, down back.
    _controls.BTN_UP->setOnClick([this]() {
        selectAdjacentNote(1);
    });
    _controls.BTN_DOWN->setOnClick([this]() {
        selectAdjacentNote(-1);
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

glm::vec4 MapMenu::mapBounds() const {
    const Control::Extent &extent = _controls.LBL_Map->extent();

    return glm::vec4(
        _gui->controlOffset().x + extent.left,
        _gui->controlOffset().y + extent.top,
        extent.width,
        extent.height);
}

void MapMenu::render() {
    GameGUI::render();

    _game.map().render(
        Map::Mode::Default, mapBounds(), _gui->scale(), _gui->pulsingAlpha());
}

bool MapMenu::handle(const input::Event &event) {
    // A map note answers a click on the marker the map drew for it. The
    // markers are drawn into the map control rather than being controls of
    // their own, so the menu tests them against the same rects it drew.
    if (event.type == input::EventType::MouseButtonUp &&
        event.button.button == input::MouseButton::Left) {
        for (const auto &marker :
             _game.map().noteMarkers(mapBounds(), _gui->scale())) {
            if (event.button.x >= marker.bounds[0] &&
                event.button.x <= marker.bounds[0] + marker.bounds[2] &&
                event.button.y >= marker.bounds[1] &&
                event.button.y <= marker.bounds[1] + marker.bounds[3]) {
                selectNote(marker.waypoint);
                return true;
            }
        }
    }
    return GameGUI::handle(event);
}

void MapMenu::refreshControls() {
    _controls.LBL_Area->setTextMessage(_game.module()->area()->localizedName());
    // While the party's map is disabled (TSL), the map image is the static,
    // stretched over it and cycled by the control.
    auto &mapImage = _controls.LBL_Map->border();
    if (_game.map().isDisabled()) {
        mapImage.fill = _services.resource.textures.get(kMapStaticResRef, TextureUsage::GUI);
        mapImage.fillStyle = Control::Border::FillStyle::Stretch;
    } else {
        mapImage.fill = _mapFill;
        mapImage.fillStyle = _mapFillStyle;
    }
    _notes.clear();

    // The map takes every note the area places, shown or not: a note can be
    // turned on, or its place explored, while the map is open.
    for (auto &object : _game.module()->area()->getObjectsByType(ObjectType::Waypoint)) {
        auto waypoint = std::static_pointer_cast<Waypoint>(object);
        if (waypoint->hasMapNote()) {
            _notes.push_back(waypoint);
        }
    }
    std::stable_sort(_notes.begin(), _notes.end(), [](const auto &left, const auto &right) {
        return left->position().x < right->position().x;
    });

    // The map opens on the first note it shows. When it shows none, nothing
    // is selected until a note is clicked.
    _selectedNoteIdx = -1;
    for (size_t index = 0; index < _notes.size(); ++index) {
        if (_game.map().isNoteVisible(*_notes[index])) {
            _selectedNoteIdx = static_cast<int>(index);
            break;
        }
    }
    refreshSelectedNote();
    if (!_game.isTSL()) {
        refreshTravel();
    }
}

void MapMenu::selectAdjacentNote(int step) {
    if (_selectedNoteIdx == -1) {
        return;
    }
    // Stepping passes over the notes the map does not show.
    const int count = static_cast<int>(_notes.size());
    for (int taken = 0; taken < count; ++taken) {
        _selectedNoteIdx = (_selectedNoteIdx + step + count) % count;
        if (_game.map().isNoteVisible(*_notes[_selectedNoteIdx])) {
            break;
        }
    }
    refreshSelectedNote();
}

void MapMenu::selectNote(const std::shared_ptr<Waypoint> &waypoint) {
    auto note = std::find(_notes.begin(), _notes.end(), waypoint);
    if (note == _notes.end()) {
        return;
    }
    _selectedNoteIdx = static_cast<int>(note - _notes.begin());
    refreshSelectedNote();
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

    // The label keeps the selected note's text until the selection changes.
    if (_selectedNoteIdx != -1) {
        note = _notes[_selectedNoteIdx];
    }
    if (note && !note->mapNote().empty()) {
        std::string text(_services.resource.strings.getText(kStrRefMapNote));
        text += ": ";
        text += note->mapNote();

        _controls.LBL_MapNote->setTextMessage(text);
    } else {
        _controls.LBL_MapNote->setTextMessage("");
    }

    _game.map().setSelectedNote(note);
}

} // namespace game

} // namespace reone
