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

#include "reone/graphics/texture.h"

namespace reone {

namespace game {

struct ServicesView;

class AreaMapState;
class Game;
class Waypoint;

/**
 * Draws the active area's map, the party's place on it, its map notes and the
 * fog over what the party has not explored.
 *
 * The map and its markers are drawn into a control's rect but not by the
 * control, so the owning GUI passes its layout scale in.
 */
class Map {
public:
    enum class Mode {
        Default,
        Minimap
    };

    /** A map note the map draws, and the rect it answers a click in. */
    struct NoteMarker {
        std::shared_ptr<Waypoint> waypoint;
        glm::vec4 bounds {0.0f};
    };

    Map(Game &game, ServicesView &services);

    void load(const std::string &area);

    void retireRuntimeSession();

    /**
     * @param scale the factor the owning GUI draws its layout at.
     * @param pulsingAlpha the owning GUI's pulsing alpha, which the party
     *                     leader's arrow is drawn at on the map screen.
     */
    void render(Mode mode, const glm::vec4 &bounds, float scale, float pulsingAlpha);

    bool isLoaded() const { return static_cast<bool>(_areaTexture); }

    /** The map notes the map shows, with the rect each one occupies. */
    std::vector<NoteMarker> noteMarkers(const glm::vec4 &bounds, float scale) const;

    void setSelectedNote(std::shared_ptr<Waypoint> waypoint) { _selectedNote = std::move(waypoint); }

    /** Whether the map shows \p waypoint: its note is on and its place explored. */
    bool isNoteVisible(const Waypoint &waypoint) const;

private:
    Game &_game;
    ServicesView &_services;

    std::shared_ptr<graphics::Texture> _areaTexture;
    std::shared_ptr<graphics::Texture> _arrowTexture;
    std::shared_ptr<graphics::Texture> _noteTexture;
    std::shared_ptr<graphics::Texture> _companionTexture;
    std::shared_ptr<graphics::Texture> _fogTexture;

    std::string _arrowResRef;
    std::shared_ptr<Waypoint> _selectedNote;

    void loadTextures(const std::string &area);

    /** The area's map, or none when no area is active or it carries no map. */
    const AreaMapState *mapState() const;

    void renderArea(const glm::vec4 &rect);
    void renderFog(const glm::vec4 &bounds, const glm::vec4 &rect);
    void renderNotes(Mode mode, const glm::vec4 &rect, float scale);
    void renderParty(Mode mode, const glm::vec4 &rect, float scale, float pulsingAlpha);

    /**
     * The rect the area image is drawn into. On the map screen that is the
     * control's rect; the minimap centres the image on the party leader.
     */
    glm::vec4 mapRect(Mode mode, const glm::vec4 &bounds, float scale) const;

    /** Draws \p texture over \p rect, tinted \p color, through \p program. */
    void renderMarker(
        const glm::vec4 &rect,
        graphics::Texture &texture,
        const glm::vec4 &color,
        const char *program);

    void renderScissored(const glm::vec4 &bounds, const std::function<void()> &block);
};

} // namespace game

} // namespace reone
