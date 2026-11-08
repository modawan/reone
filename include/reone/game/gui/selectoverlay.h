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

#include <array>
#include <map>
#include <optional>

#include "reone/graphics/font.h"
#include "reone/graphics/texture.h"
#include "reone/gui/control.h"
#include "reone/input/event.h"

#include "../contextaction.h"

#include "actionslot.h"

namespace reone {

namespace game {

struct ServicesView;

class Game;
class Object;

class SelectionOverlay {
    friend class TestGameModule;

public:
    SelectionOverlay(
        Game &game,
        ServicesView &services);

    /**
     * \p arrowMargin is the HUD control whose edges bound where the hover
     * reticle may show.
     */
    void init(std::shared_ptr<gui::Control> arrowMargin);

    bool handle(const input::Event &event);
    void update(float dt);
    /** Draw the reticles and the target menu, whose icons all grey while \p leaderDown. */
    void render(bool leaderDown);

private:
    /** The target menu's columns. */
    static constexpr int kNumActionSlots = 3;

    enum class ActionBand {
        None,
        Previous,
        Icon,
        Next
    };

    /** Targets that share a column memory. */
    enum class SelectionGroup {
        HostileCreature,
        OtherCreatureOrFixture,
        DoorOrContainer,
        Mine,
        None
    };

    Game &_game;
    ServicesView &_services;

    std::shared_ptr<graphics::Font> _font;
    std::shared_ptr<graphics::Texture> _friendlyReticle;
    std::shared_ptr<graphics::Texture> _friendlyReticle2;
    std::shared_ptr<graphics::Texture> _hostileReticle;
    std::shared_ptr<graphics::Texture> _hostileReticle2;
    std::shared_ptr<graphics::Texture> _friendlyScroll;
    std::shared_ptr<graphics::Texture> _hostileScroll;
    std::shared_ptr<graphics::Texture> _hilightedScroll;
    std::shared_ptr<graphics::Texture> _actionArrow;
    std::shared_ptr<graphics::Texture> _hilightedActionArrow;
    /** The HUD control whose edges bound where the hover reticle shows. */
    std::shared_ptr<gui::Control> _arrowMargin;
    std::shared_ptr<Object> _hilightedObject;
    std::shared_ptr<Object> _selectedObject;
    std::vector<ActionSlot> _actionSlots;
    glm::vec3 _hilightedScreenCoords {0.0f};
    glm::vec3 _selectedScreenCoords {0.0f};
    int _reticleHeight {0};
    /** The hover reticle's size in pixels, by the object's distance from the leader. */
    int _hilightedReticleSize {0};
    int _selectedActionSlot {-1};
    ActionBand _hilightedActionBand {ActionBand::None};
    bool _hilightedHostile {false};
    /**
     * Whether the selected target is a creature shown and offered as hostile:
     * one hostile to the leader, or any while the dead-target hold runs.
     */
    bool _selectedHostile {false};
    bool _hasActions {false};
    /** The group of the selected target, whose entries the columns show. */
    SelectionGroup _selectionGroup {SelectionGroup::None};
    /**
     * For each group of targets, the entry each column is set to. While a
     * column does not offer its entry, it shows its first.
     */
    std::map<SelectionGroup, std::array<std::optional<EntryIdentity>, kNumActionSlots>> _selections;
    ActionMenuFeedback _feedback;

    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleMouseWheel(const input::MouseWheelEvent &event);

    void renderReticle(graphics::Texture &texture, const glm::vec3 &screenCoords, float width, float height, float alpha);
    void renderTitleBar();
    void renderHealthBar();
    void renderActionBar(bool leaderDown);

    void renderActionFrame(int index);
    void renderActionArrows(int index);
    void renderActionArrow(int index, bool previous);
    void renderActionIcon(int index, bool leaderDown);

    bool getActionScreenCoords(int index, float &x, float &y) const;
    float layoutScale() const;
    glm::vec3 getColorFromSelectedObject() const;
};

} // namespace game

} // namespace reone
