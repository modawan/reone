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

#include "types.h"

namespace reone {

namespace graphics {

class IStatistic;

class Context;
class MeshRegistry;
class ShaderRegistry;
class Texture;
class Uniforms;

class Font {
public:
    Font(
        Context &context,
        MeshRegistry &meshRegistry,
        ShaderRegistry &shaderRegistry,
        IStatistic &statistic,
        Uniforms &uniforms) :
        _context(context),
        _meshRegistry(meshRegistry),
        _shaderRegistry(shaderRegistry),
        _statistic(statistic),
        _uniforms(uniforms) {
    }

    void load(std::shared_ptr<Texture> texture);

    /** @param scale multiplies glyph size and advance, for scaled layouts. */
    void render(
        std::string_view text,
        const glm::vec3 &position,
        const glm::vec3 &color = glm::vec3(1.0f, 1.0f, 1.0f),
        TextGravity align = TextGravity::CenterCenter,
        float scale = 1.0f);

    /**
     * Draws \p text at \p position, placed by \p align against its width
     * (measure), each glyph a glyph advance (glyphAdvance) after the one
     * before.
     *
     * @param scale multiplies glyph size and advance, for scaled layouts
     */
    void render(
        std::string_view text,
        const glm::vec3 &position,
        const glm::vec4 &color,
        TextGravity align = TextGravity::CenterCenter,
        float scale = 1.0f);

    /** The width \p text takes when drawn at \p scale: the sum of its glyph advances. */
    float measure(std::string_view text, float scale = 1.0f) const;

    /** Draws \p line from \p position offset by \p textOffset, which each glyph moves on by its glyph advance. */
    void renderLine(std::string_view line,
                    const glm::vec3 &position,
                    glm::vec3 &textOffset,
                    float scale = 1.0f);

    float height() const { return _height; }

    /**
     * The height of \p lineCount lines of text measured as one block, in
     * whole pixels: the glyphs' height and the spacing below a line, scaled,
     * over all the lines, rounded once.
     *
     * @param lineCount the number of lines
     * @param scale the scale the text is drawn at
     */
    int blockHeight(int lineCount, float scale) const;

    /**
     * How far each line of drawn text is below the one before, unrounded:
     * the glyphs' height and the spacing below a line, at \p scale.
     */
    float lineAdvance(float scale) const;

    Texture &texture() { return *_texture; }

private:
    struct Glyph {
        glm::vec2 ul {0.0f};
        glm::vec2 lr {0.0f};
        glm::vec2 size {0.0f};
    };

    std::shared_ptr<Texture> _texture;
    float _height {0.0f};
    float _spacingR {0.0f};
    /** The height a line takes unscaled, the spacing below it included. */
    float _lineSpan {0.0f};
    std::vector<Glyph> _glyphs;

    // Services

    Context &_context;
    MeshRegistry &_meshRegistry;
    ShaderRegistry &_shaderRegistry;
    IStatistic &_statistic;
    Uniforms &_uniforms;

    // END Services

    glm::vec2 getTextOffset(std::string_view text, TextGravity gravity, float scale = 1.0f) const;
    /** How far the pen moves on after drawing \p glyph, unrounded: its width and the spacing after it, at \p scale. */
    float glyphAdvance(const Glyph &glyph, float scale) const;
};

} // namespace graphics

} // namespace reone
