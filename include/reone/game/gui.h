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

#pragma once

#include "reone/audio/source.h"
#include "reone/game/di/services.h"
#include "reone/gui/gui.h"
#include "reone/input/event.h"

#include "types.h"

namespace reone {

namespace gui {

class ListBox;

}

namespace game {

class Game;

/** K1's combat font is substantially smaller than TSL's at the same GUI scale. */
constexpr float kK1CombatTextScale = 2.0f;

class GameGUI : public gui::IGUIEventListener, boost::noncopyable {
public:
    virtual void init();

    virtual bool handle(const input::Event &event);
    virtual void update(float dt);
    virtual void render();

    void clearSelection() {
        _gui->clearSelection();
    }

protected:
    friend class HUDTestAccess;

    Game &_game;
    ServicesView &_services;
    std::string _resRef;

    std::shared_ptr<gui::IGUI> _gui;
    std::shared_ptr<audio::AudioSource> _audioSource;

    glm::vec3 _baseColor {0.0f};
    glm::vec3 _disabledColor {0.0f};
    glm::vec3 _hilightColor {0.0f};

    GameGUI(Game &game, ServicesView &services);

    virtual void preload(gui::IGUI &gui);
    virtual void onGUILoaded() {}

    void loadBackground(BackgroundType type);

    /** Centers the root panel in its authored canvas and reapplies layout. */
    void centerRootInCanvas(int canvasWidth, int canvasHeight);

    /**
     * Points an item list at the slot silhouettes baked into this GUI's own
     * panel art, so the list can repaint them under its actual rows.
     *
     * K1's inventory and equipment panels share one authored slot strip and
     * the same measured slot geometry. K2 bakes no slots and needs none of
     * this; calling it there is a no-op.
     */
    void useBakedItemSlotArt(gui::ListBox &listBox);

    virtual void configureControls() {}
    void onClick(const std::string &control) override;
    void onSelectionChanged(const std::string &control, bool selected) override;

    std::string guiResRef(const std::string &base) const;

    template <class T>
    std::shared_ptr<T> findControl(const std::string &tag) const {
        auto ctrl = _gui->findControl(tag);
        return std::static_pointer_cast<T>(ctrl);
    }
};

} // namespace game

} // namespace reone
