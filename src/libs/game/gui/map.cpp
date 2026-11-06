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

#include "reone/game/gui/map.h"

#include "reone/game/areamapstate.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/provider/textures.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kArrowSize = 32;
static constexpr int kCompanionSize = 16;
static constexpr int kMapNoteSize = 14;
static constexpr int kSelectedMapNoteSize = 20;

/**
 * The fog blot is drawn several cells wide so that neighbouring blots overlap
 * into continuous cover rather than a grid of dots.
 */
static constexpr float kFogBlotCells = 4.0f;

/** The orange the map marks a companion in. */
static const glm::vec3 kCompanionColor {1.0f, 0.5f, 0.0f};

Map::Map(Game &game, ServicesView &services) :
    _game(game),
    _services(services) {
    if (game.isTSL()) {
        _arrowResRef = "mm_barrow_p";
    } else {
        _arrowResRef = "mm_barrow";
    }
}

void Map::load(const std::string &area) {
    loadTextures(area);
}

void Map::retireRuntimeSession() {
    _areaTexture.reset();
    _selectedNote.reset();
}

void Map::loadTextures(const std::string &area) {
    std::string resRef("lbl_map" + area);
    _areaTexture = _services.resource.textures.get(resRef, TextureUsage::GUI);

    if (!_arrowTexture) {
        _arrowTexture = _services.resource.textures.get(_arrowResRef, TextureUsage::GUI);
    }
    if (!_noteTexture) {
        _noteTexture = _services.resource.textures.get("whitetarget", TextureUsage::GUI);
    }
    if (!_companionTexture) {
        _companionTexture = _services.resource.textures.get("lbl_mapcircle", TextureUsage::GUI);
    }
    if (!_fogTexture) {
        _fogTexture = _services.resource.textures.get("blackdot", TextureUsage::GUI);
    }
}

const AreaMapState *Map::mapState() const {
    auto module = _game.module();
    if (!module) {
        return nullptr;
    }
    auto area = module->area();
    if (!area) {
        return nullptr;
    }
    const AreaMapState &state = area->mapState();
    return state.isLoaded() ? &state : nullptr;
}

void Map::render(Mode mode, const glm::vec4 &bounds, float scale, float pulsingAlpha) {
    if (!isLoaded()) {
        return;
    }
    const glm::vec4 rect(mapRect(mode, bounds, scale));
    _services.graphics.context.withBlendMode(BlendMode::Normal, [&]() {
        renderScissored(bounds, [&]() {
            renderArea(rect);
            renderFog(bounds, rect);
            renderNotes(mode, rect, scale);
            renderParty(mode, rect, scale, pulsingAlpha);
        });
    });
}

void Map::renderScissored(const glm::vec4 &bounds, const std::function<void()> &block) {
    const int height = _game.options().graphics.height;
    const glm::ivec4 scissorBounds(
        bounds[0], height - (bounds[1] + bounds[3]), bounds[2], bounds[3]);
    _services.graphics.context.withScissorTest(scissorBounds, block);
}

glm::vec4 Map::mapRect(Mode mode, const glm::vec4 &bounds, float scale) const {
    if (mode == Mode::Default) {
        // The map screen lays the area's map over its map control.
        return bounds;
    }
    // The minimap shows the map at the image's own size, at the layout's
    // scale, and keeps the party leader under the centre of its frame.
    auto state = mapState();
    const float height = _areaTexture->height() * scale;
    const float width = height * state->canvasWidth() / state->canvasHeight();

    glm::vec2 mapPos(0.0f);
    auto leader = _game.party().getLeader();
    if (leader) {
        if (auto position = state->toMapPosition(glm::vec2(leader->position()))) {
            mapPos = *position;
        }
    }
    return glm::vec4(
        bounds[0] + 0.5f * bounds[2] - mapPos.x * width,
        bounds[1] + 0.5f * bounds[3] - mapPos.y * height,
        width,
        height);
}

void Map::renderArea(const glm::vec4 &rect) {
    _services.graphics.context.bindTexture(*_areaTexture);

    // The image keeps its own proportions, scaled so its height spans the
    // map. KotOR's images are wider than the map they carry, and the empty
    // strip past the map's right edge falls outside the frame.
    const float width = rect[3] * _areaTexture->width() / _areaTexture->height();

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(rect[0], rect[1], 0.0f));
    transform = glm::scale(transform, glm::vec3(width, rect[3], 1.0f));

    _services.graphics.uniforms.setLocals([transform](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
    });
    _services.graphics.context.useProgram(
        _services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void Map::renderFog(const glm::vec4 &bounds, const glm::vec4 &rect) {
    auto state = mapState();
    if (!state) {
        return;
    }
    const float cellWidth = rect[2] / state->gridWidth();
    const float cellHeight = rect[3] / state->gridHeight();
    const float blotWidth = cellWidth * kFogBlotCells;
    const float blotHeight = cellHeight * kFogBlotCells;

    for (int gridY = 0; gridY <= state->gridHeight(); ++gridY) {
        for (int gridX = 0; gridX <= state->gridWidth(); ++gridX) {
            if (state->isCellExplored(gridX, gridY)) {
                continue;
            }
            const float centerX = rect[0] + gridX * cellWidth;
            const float centerY = rect[1] + gridY * cellHeight;
            if (centerX + 0.5f * blotWidth < bounds[0] ||
                centerX - 0.5f * blotWidth > bounds[0] + bounds[2] ||
                centerY + 0.5f * blotHeight < bounds[1] ||
                centerY - 0.5f * blotHeight > bounds[1] + bounds[3]) {
                continue;
            }
            // The blot carries its own colour: a dark centre crossed by the
            // grid lines that neighbouring blots join up into a lattice. It
            // is drawn untinted, and through the plain textured program, so
            // that its soft edge survives being magnified to cell size.
            renderMarker(
                glm::vec4(
                    centerX - 0.5f * blotWidth,
                    centerY - 0.5f * blotHeight,
                    blotWidth,
                    blotHeight),
                *_fogTexture,
                glm::vec4(1.0f),
                ShaderProgramId::mvpTexture);
        }
    }
}

bool Map::isNoteVisible(const Waypoint &waypoint) const {
    if (!waypoint.hasMapNote() || !waypoint.isMapNoteEnabled()) {
        return false;
    }
    auto state = mapState();
    return state && state->isExplored(glm::vec2(waypoint.position()));
}

std::vector<Map::NoteMarker> Map::noteMarkers(const glm::vec4 &bounds, float scale) const {
    std::vector<NoteMarker> result;
    auto state = mapState();
    auto module = _game.module();
    if (!state || !module || !module->area()) {
        return result;
    }
    for (auto &object : module->area()->getObjectsByType(ObjectType::Waypoint)) {
        auto waypoint = std::static_pointer_cast<Waypoint>(object);
        if (!isNoteVisible(*waypoint)) {
            continue;
        }
        auto mapPos = state->toMapPosition(glm::vec2(waypoint->position()));
        if (!mapPos) {
            continue;
        }
        const float size =
            (waypoint == _selectedNote ? kSelectedMapNoteSize : kMapNoteSize) * scale;
        const float centerX = bounds[0] + mapPos->x * bounds[2];
        const float centerY = bounds[1] + mapPos->y * bounds[3];
        result.push_back(
            NoteMarker {
                waypoint,
                glm::vec4(
                    centerX - 0.5f * size,
                    centerY - 0.5f * size,
                    size,
                    size)});
    }
    return result;
}

void Map::renderNotes(Mode mode, const glm::vec4 &rect, float scale) {
    if (mode != Mode::Default) {
        return;
    }
    const glm::vec3 selectedColor(_game.isTSL() ? kTSLGUIColorHilight : kGUIColorHilight);
    const glm::vec3 color(_game.isTSL() ? kTSLGUIColorBase : kGUIColorBase);

    for (const auto &marker : noteMarkers(rect, scale)) {
        const bool selected = marker.waypoint == _selectedNote;
        renderMarker(
            marker.bounds,
            *_noteTexture,
            glm::vec4(selected ? selectedColor : color, 1.0f),
            ShaderProgramId::mvpIcon);
    }
}

void Map::renderParty(Mode mode, const glm::vec4 &rect, float scale, float pulsingAlpha) {
    auto state = mapState();
    if (!state) {
        return;
    }
    auto &party = _game.party();

    // The map screen marks where the companions stand; the minimap shows the
    // leader alone. Both carry the leader's arrow.
    for (int index = 1; mode == Mode::Default && index < party.getSize(); ++index) {
        auto member = party.getMember(index);
        if (!member) {
            continue;
        }
        auto mapPos = state->toMapPosition(glm::vec2(member->position()));
        if (!mapPos) {
            continue;
        }
        const float size = kCompanionSize * scale;
        renderMarker(
            glm::vec4(
                rect[0] + mapPos->x * rect[2] - 0.5f * size,
                rect[1] + mapPos->y * rect[3] - 0.5f * size,
                size,
                size),
            *_companionTexture,
            glm::vec4(kCompanionColor, 1.0f),
            ShaderProgramId::mvpIcon);
    }

    auto leader = party.getLeader();
    if (!leader) {
        return;
    }
    auto mapPos = state->toMapPosition(glm::vec2(leader->position()));
    if (!mapPos) {
        return;
    }
    const float size = kArrowSize * scale;
    const glm::vec3 arrowPos(
        rect[0] + mapPos->x * rect[2],
        rect[1] + mapPos->y * rect[3],
        0.0f);

    // The map's angles run counter-clockwise, the GUI's clockwise.
    const float facing = -state->toMapFacing(leader->getFacing());

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, arrowPos);
    transform = glm::rotate(transform, facing, glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::translate(transform, glm::vec3(-0.5f * size, -0.5f * size, 0.0f));
    transform = glm::scale(transform, glm::vec3(size, size, 1.0f));

    // On the map screen the arrow pulses; on the minimap it is steady.
    const float alpha = mode == Mode::Default ? pulsingAlpha : 1.0f;

    _services.graphics.context.bindTexture(*_arrowTexture);
    _services.graphics.uniforms.setLocals([transform, alpha](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
        locals.color.a = alpha;
    });
    _services.graphics.context.useProgram(
        _services.graphics.shaderRegistry.get(ShaderProgramId::mvpIcon));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void Map::renderMarker(
    const glm::vec4 &rect,
    graphics::Texture &texture,
    const glm::vec4 &color,
    const char *program) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(rect[0], rect[1], 0.0f));
    transform = glm::scale(transform, glm::vec3(rect[2], rect[3], 1.0f));

    _services.graphics.context.bindTexture(texture);
    _services.graphics.uniforms.setLocals([transform, color](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
        locals.color = color;
    });
    _services.graphics.context.useProgram(
        _services.graphics.shaderRegistry.get(program));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

} // namespace game

} // namespace reone
