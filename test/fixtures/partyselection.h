/*
 * Copyright (c) 2026 The reone project contributors
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

#pragma once

#include "engine.h"
#include "gui.h"

#include "reone/game/gui/partyselect.h"

namespace reone::game {

/**
 * The one test accessor PartySelection befriends. Every test file that needs
 * the screen's internals uses this definition, so there is only one.
 */
class PartySelectionTestAccess {
public:
    // Presentation (test/game/partyselection.cpp).
    static glm::vec3 frameColor(
        const glm::vec3 &baseColor,
        const glm::vec3 &inPartyColor,
        bool inParty,
        bool chosen) {
        return PartySelection::tslFrameColor(baseColor, inPartyColor, inParty, chosen);
    }

    static std::pair<bool, bool> sharedSlot(bool firstAvailable, bool secondAvailable) {
        return PartySelection::sharedSlotVisibility(firstAvailable, secondAvailable);
    }

    static std::string count(int count, bool tsl, const std::string &label) {
        return PartySelection::availableCountText(count, tsl, label);
    }

    // The screen driven with stand-in controls (test/game/rostermaterialization.cpp).
    /** Builds the screen's controls a loaded layout would otherwise provide. */
    static void bind(PartySelection &screen, std::shared_ptr<gui::IGUI> gui) {
        auto &engine = testEngine();
        auto button = [&]() {
            return std::make_shared<gui::Button>(*gui, engine.sceneModule().graphs(),
                                                 engine.graphicsModule().services(), engine.resourceModule().services());
        };
        auto toggle = [&]() {
            return std::make_shared<gui::ToggleButton>(*gui, engine.sceneModule().graphs(),
                                                       engine.graphicsModule().services(), engine.resourceModule().services());
        };
        // Every label of the loaded layout has a border, which the screen
        // fills with a portrait and fades.
        auto label = [&]() {
            auto control = std::make_shared<gui::Label>(*gui, engine.sceneModule().graphs(),
                                                        engine.graphicsModule().services(), engine.resourceModule().services());
            control->setBorder(gui::Control::Border());
            return control;
        };
        auto &c = screen._controls;
        c.BTN_ACCEPT = button();
        c.BTN_BACK = button();
        c.BTN_DONE = button();
        for (auto *npc : {&c.BTN_NPC0, &c.BTN_NPC1, &c.BTN_NPC2, &c.BTN_NPC3, &c.BTN_NPC4, &c.BTN_NPC5,
                          &c.BTN_NPC6, &c.BTN_NPC7, &c.BTN_NPC8, &c.BTN_NPC9, &c.BTN_NPC10, &c.BTN_NPC11}) {
            *npc = toggle();
        }
        for (auto *lbl : {&c.LBL_3D, &c.LBL_BAR1, &c.LBL_BAR2, &c.LBL_BAR3, &c.LBL_BAR4, &c.LBL_BAR5,
                          &c.LBL_AVAILABLE, &c.LBL_BEVEL_L, &c.LBL_BEVEL_M, &c.LBL_BEVEL_R,
                          &c.LBL_CHAR0, &c.LBL_CHAR1, &c.LBL_CHAR2, &c.LBL_CHAR3, &c.LBL_CHAR4, &c.LBL_CHAR5,
                          &c.LBL_CHAR6, &c.LBL_CHAR7, &c.LBL_CHAR8, &c.LBL_CHAR9, &c.LBL_CHAR10, &c.LBL_CHAR11,
                          &c.LBL_COUNT,
                          &c.LBL_NA0, &c.LBL_NA1, &c.LBL_NA2, &c.LBL_NA3, &c.LBL_NA4, &c.LBL_NA5,
                          &c.LBL_NA6, &c.LBL_NA7, &c.LBL_NA8, &c.LBL_NA9, &c.LBL_NA10, &c.LBL_NA11,
                          &c.LBL_NAMEBACK, &c.LBL_NPC_LEVEL, &c.LBL_NPC_NAME, &c.LBL_TITLE}) {
            *lbl = label();
        }
        screen._gui = std::move(gui);
    }

    static std::string count(PartySelection &screen) { return screen._controls.LBL_COUNT->text().text; }
    // Those the screen shows as picked.
    static std::vector<int> selection(PartySelection &screen) {
        std::vector<int> result;
        for (int npc = 0; npc < screen.npcCount(); ++npc) {
            if (screen.getNpcButton(npc).usesBorderColorOverride()) {
                result.push_back(npc);
            }
        }
        return result;
    }
    static bool backDisabled(PartySelection &screen) { return screen._controls.BTN_BACK->isDisabled(); }

    static void pick(PartySelection &screen, int npc) {
        screen.getNpcButton(npc).handleClick(0, 0);
        screen._controls.BTN_ACCEPT->handleClick(0, 0);
    }
    static void done(PartySelection &screen) { screen._controls.BTN_DONE->handleClick(0, 0); }
    static void back(PartySelection &screen) { screen._controls.BTN_BACK->handleClick(0, 0); }
};

} // namespace reone::game
