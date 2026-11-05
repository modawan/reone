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

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/textures.h"

using namespace reone::graphics;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kArrowSize = 32;
static constexpr int kMapNoteSize = 16;
static constexpr float kSelectedMapNoteScale = 1.5f;

Map::Map(Game &game, ServicesView &services) :
    _game(game),
    _services(services) {
    if (game.isTSL()) {
        _arrowResRef = "mm_barrow_p";
    } else {
        _arrowResRef = "mm_barrow";
    }
}

void Map::load(const std::string &area, const resource::generated::ARE_Map &map) {
    loadProperties(map);
    loadTextures(area);
}

void Map::retireRuntimeSession() {
    _areaTexture.reset();
    _selectedNote.reset();
    _unexploredTexture.reset();
    _unexploredRevision = 0;
}

void Map::loadProperties(const resource::generated::ARE_Map &map) {
    _properties = map;
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
}

void Map::render(Mode mode, const glm::vec4 &bounds, float scale, float pulsingAlpha) {
    if (!_areaTexture) {
        return;
    }
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this, &mode, &bounds, scale, pulsingAlpha]() {
        renderArea(mode, bounds, scale);
        // Drawn over the unexplored cells too, which is reone's choice.
        renderNotes(mode, bounds, scale);
        renderPartyLeader(mode, bounds, scale, pulsingAlpha);
    });
}

void Map::renderArea(Mode mode, const glm::vec4 &bounds, float scale) {
    if (mode == Mode::Minimap) {
        std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
        if (!partyLeader) {
            return;
        }
        _services.graphics.context.bindTexture(*_areaTexture);

        glm::vec2 worldPos(partyLeader->position());
        glm::vec2 mapPos(getMapPosition(worldPos));

        // Drawn at the layout's scale, not the texture's own size: the
        // minimap frame is a control and shrinks with the GUI, so an unscaled
        // map inside it is the wrong zoom and slides under its own scissor.
        // The offsets that centre the party leader are in the same pixel space
        // and take the same factor.
        const float mapWidth = _areaTexture->width() * scale;
        const float mapHeight = _areaTexture->height() * scale;

        glm::vec3 topLeft(0.0f);
        float logicalMapWidth = _game.isTSL() ? kTSLMapWidth : kKotorMapWidth;
        topLeft.x = bounds[0] + 0.5f * bounds[2] - mapPos.x * logicalMapWidth * scale;
        topLeft.y = bounds[1] + 0.5f * bounds[3] - mapPos.y * mapHeight;

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, topLeft);
        transform = glm::scale(transform, glm::vec3(mapWidth, mapHeight, 1.0f));

        _services.graphics.uniforms.setLocals([transform](auto &locals) {
            locals.reset();
            locals.model = std::move(transform);
        });
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));

        int height = _game.options().graphics.height;
        glm::ivec4 scissorBounds(bounds[0], height - (bounds[1] + bounds[3]), bounds[2], bounds[3]);
        _services.graphics.context.withScissorTest(scissorBounds, [&]() {
            _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
            // Sized as the map is placed above: the canvas width across, the
            // texture's height down.
            renderUnexplored(topLeft, glm::vec2(logicalMapWidth * scale, mapHeight));
        });

    } else {
        _services.graphics.context.bindTexture(*_areaTexture);

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3(bounds[0], bounds[1], 0.0f));
        transform = glm::scale(transform, glm::vec3(bounds[2], bounds[3], 1.0f));

        _services.graphics.uniforms.setLocals([transform](auto &locals) {
            locals.reset();
            locals.model = std::move(transform);
        });
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));
        _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);

        // The texture is wider than the logical canvas, which takes its left
        // part, as normalizeMapPosition has it.
        float logicalMapWidth = _game.isTSL() ? kTSLMapWidth : kKotorMapWidth;
        glm::vec2 canvasSize(
            bounds[2] * logicalMapWidth / static_cast<float>(_areaTexture->width()),
            bounds[3] * kMapHeight / static_cast<float>(_areaTexture->height()));
        renderUnexplored(glm::vec2(bounds[0], bounds[1]), canvasSize);
    }
}

void Map::renderUnexplored(const glm::vec2 &topLeft, const glm::vec2 &size) {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area || area->mapExploration().empty()) {
        return;
    }
    const auto &exploration = area->mapExploration();
    if (!_unexploredTexture || _unexploredRevision != exploration.revision()) {
        refreshUnexploredTexture(exploration);
    }
    _services.graphics.context.bindTexture(*_unexploredTexture);

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(topLeft, 0.0f));
    transform = glm::scale(transform, glm::vec3(size, 1.0f));

    _services.graphics.uniforms.setLocals([transform](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpTexture));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void Map::refreshUnexploredTexture(const MapExploration &exploration) {
    if (!_unexploredTexture) {
        // Nearest filtering keeps each cell a hard-edged square, a look that
        // is reone's choice.
        Texture::Properties properties;
        properties.minFilter = Texture::Filtering::Nearest;
        properties.magFilter = Texture::Filtering::Nearest;
        properties.wrap = Texture::Wrapping::ClampToEdge;
        _unexploredTexture = std::make_shared<Texture>("map_unexplored", TextureType::TwoDim, properties);
        _unexploredTexture->clear(1, 1, PixelFormat::RGBA8);
        _unexploredTexture->init();
    }
    int columns = exploration.columns();
    int rows = exploration.rows();
    auto pixels = std::make_shared<ByteBuffer>(static_cast<size_t>(4 * columns * rows), 0);
    for (int row = 0; row < rows; ++row) {
        // The quad samples the last texel row at its top, and grid row 0 is
        // the top of the map.
        int texelRow = rows - 1 - row;
        for (int column = 0; column < columns; ++column) {
            // reone's choice: an unexplored cell hides the map completely.
            if (!exploration.isExplored(column, row)) {
                (*pixels)[4 * (texelRow * columns + column) + 3] = static_cast<char>(0xff);
            }
        }
    }
    _services.graphics.context.bindTexture(*_unexploredTexture);
    _unexploredTexture->setPixels(columns, rows, PixelFormat::RGBA8, Texture::Layer {std::move(pixels)}, true);
    _unexploredRevision = exploration.revision();
}

void Map::renderNotes(Mode mode, const glm::vec4 &bounds, float scale) {
    if (mode != Mode::Default) {
        return;
    }
    _services.graphics.context.bindTexture(*_noteTexture);

    for (auto &object : _game.module()->area()->getObjectsByType(ObjectType::Waypoint)) {
        auto waypoint = std::static_pointer_cast<Waypoint>(object);
        if (!waypoint->isMapNoteEnabled() || waypoint->mapNote().empty())
            continue;

        glm::vec2 mapPos(getMapPosition(waypoint->position()));
        normalizeMapPosition(mapPos);

        glm::vec2 notePos;
        notePos.x = bounds[0] + mapPos.x * bounds[2];
        notePos.y = bounds[1] + mapPos.y * bounds[3];

        bool selected = waypoint == _selectedNote;
        float noteSize = (selected ? kSelectedMapNoteScale : 1.0f) * kMapNoteSize * scale;

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3(notePos.x - 0.5f * noteSize, notePos.y - 0.5f * noteSize, 0.0f));
        transform = glm::scale(transform, glm::vec3(noteSize, noteSize, 1.0f));

        auto guiColorHilight = _game.isTSL() ? kTSLGUIColorHilight : kGUIColorHilight;
        auto guiColorBase = _game.isTSL() ? kTSLGUIColorBase : kGUIColorBase;

        _services.graphics.uniforms.setLocals([&](auto &locals) {
            locals.reset();
            locals.model = std::move(transform);
            locals.color = glm::vec4(selected ? guiColorHilight : guiColorBase, 1.0f);
        });
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpIcon));
        _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
    }
}

void Map::normalizeMapPosition(glm::vec2 &mapPos) const {
    mapPos.x *= (_game.isTSL() ? kTSLMapWidth : kKotorMapWidth) /
                static_cast<float>(_areaTexture->width());
    mapPos.y *= kMapHeight / static_cast<float>(_areaTexture->height());
}

glm::vec2 Map::getMapPosition(const glm::vec2 &world) const {
    return worldToMapPosition(_properties, world);
}

void Map::renderPartyLeader(Mode mode, const glm::vec4 &bounds, float scale, float pulsingAlpha) {
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    if (!partyLeader) {
        return;
    }
    _services.graphics.context.bindTexture(*_arrowTexture);

    glm::vec3 arrowPos(0.0f);

    if (mode == Mode::Default) {
        glm::vec2 worldPos(partyLeader->position());
        glm::vec2 mapPos(getMapPosition(worldPos));
        normalizeMapPosition(mapPos);

        arrowPos.x = bounds[0] + mapPos.x * bounds[2];
        arrowPos.y = bounds[1] + mapPos.y * bounds[3];

    } else {
        arrowPos.x = bounds[0] + 0.5f * bounds[2];
        arrowPos.y = bounds[1] + 0.5f * bounds[3];
    }

    float facing;
    switch (_properties.NorthAxis) {
    case 0:
        facing = -partyLeader->getFacing();
        break;
    case 1:
        facing = glm::pi<float>() - partyLeader->getFacing();
        break;
    case 2:
        facing = glm::three_over_two_pi<float>() - partyLeader->getFacing();
        break;
    default:
        facing = glm::half_pi<float>() - partyLeader->getFacing();
        break;
    }
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, arrowPos);
    transform = glm::rotate(transform, facing, glm::vec3(0.0f, 0.0f, 1.0f));
    const float arrowSize = kArrowSize * scale;
    transform = glm::translate(transform, glm::vec3(-0.5f * arrowSize, -0.5f * arrowSize, 0.0f));
    transform = glm::scale(transform, glm::vec3(arrowSize, arrowSize, 1.0f));

    // On the map screen the arrow pulses; on the minimap it is steady.
    const float alpha = mode == Mode::Default ? pulsingAlpha : 1.0f;
    _services.graphics.uniforms.setLocals([this, transform, alpha](auto &locals) {
        locals.reset();
        locals.model = std::move(transform);
        locals.color.a = alpha;
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpIcon));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

} // namespace game

} // namespace reone
