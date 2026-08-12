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

#include "reone/graphics/font.h"

#include "reone/graphics/context.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"

namespace reone {

namespace graphics {

void Font::load(std::shared_ptr<Texture> texture) {
    _texture = texture;

    const Texture::Features &features = texture->features();
    _height = features.fontHeight * 100.0f;
    _spacingR = features.spacingR * 100.0f;
    _glyphs.reserve(features.numChars);

    float textureAspect = static_cast<float>(texture->width()) / texture->height();

    for (int i = 0; i < features.numChars; ++i) {
        glm::vec2 ul(features.upperLeftCoords[i]);
        glm::vec2 lr(features.lowerRightCoords[i]);
        float w = lr.x - ul.x;
        float h = ul.y - lr.y;
        float aspect = h != 0.0f ? (w / h) * textureAspect : 0.0f;

        Glyph glyph;
        glyph.ul = std::move(ul);
        glyph.lr = std::move(lr);
        glyph.size = glm::vec2(aspect * _height, _height);

        _glyphs.push_back(std::move(glyph));
    }
}

void Font::render(std::string_view text, const glm::vec3 &position, const glm::vec3 &color, TextGravity gravity, float scale) {
    render(text, position, glm::vec4(color, 1.0f), gravity, scale);
}

void Font::render(std::string_view text, const glm::vec3 &position, const glm::vec4 &color, TextGravity gravity, float scale) {
    if (text.empty()) {
        return;
    }

    _context.useProgram(_shaderRegistry.get(ShaderProgramId::text));
    _context.bindTexture(*_texture);

    _uniforms.setLocals([this, &color](auto &locals) {
        locals.reset();
        locals.color = color;
    });

    int numBlocks = static_cast<int>(text.size()) / kMaxTextChars;
    if (text.size() % kMaxTextChars > 0) {
        ++numBlocks;
    }
    glm::vec3 textOffset(getTextOffset(text, gravity, scale), 0.0f);
    for (int i = 0; i < numBlocks; ++i) {
        int numChars = glm::min(kMaxTextChars, static_cast<int>(text.size()) - i * kMaxTextChars);
        std::string_view line = text.substr(i * kMaxTextChars, numChars);
        renderLine(line, position, textOffset, scale);
    }
}

glm::vec2 Font::getTextOffset(std::string_view text, TextGravity gravity, float scale) const {
    // Both metrics are in glyph space, so the gravity offset takes the same
    // scale the glyphs are drawn at; otherwise scaled text drifts off its
    // anchor by the difference.
    float w = measure(text, scale);
    float h = _height * scale;

    switch (gravity) {
    case TextGravity::LeftCenter:
        return glm::vec2(-w, -0.5f * h);
    case TextGravity::LeftTop:
        return glm::vec2(-w, -h);
    case TextGravity::CenterBottom:
        return glm::vec2(-0.5f * w, 0.0f);
    case TextGravity::CenterTop:
        return glm::vec2(-0.5f * w, -h);
    case TextGravity::RightBottom:
        return glm::vec2(0.0f, 0.0f);
    case TextGravity::RightCenter:
        return glm::vec2(0.0f, -0.5f * h);
    case TextGravity::RightTop:
        return glm::vec2(0.0f, -h);
    case TextGravity::CenterCenter:
    default:
        return glm::vec2(-0.5f * w, -0.5f * h);
    }
};

float Font::measure(std::string_view text, float scale) const {
    float w = 0.0f;
    for (const char &glyph : text) {
        w += glyphAdvance(_glyphs[reinterpret_cast<const unsigned char &>(glyph)], scale);
    }
    return w;
}

void Font::renderLine(std::string_view line, const glm::vec3 &position, glm::vec3 &textOffset, float scale) {
    if (line.empty()) {
        return;
    }

    _uniforms.setText([this, &line, &position, &textOffset, scale](auto &uniforms) {
        for (int j = 0; j < line.size(); ++j) {
            const Glyph &glyph = _glyphs[static_cast<unsigned char>(line[j])];

            glm::vec4 posScale;
            posScale[0] = position.x + textOffset.x;
            posScale[1] = position.y + textOffset.y;
            posScale[2] = glyph.size.x * scale;
            posScale[3] = glyph.size.y * scale;

            uniforms.chars[j].posScale = std::move(posScale);
            uniforms.chars[j].uv = glm::vec4(glyph.ul.x, glyph.lr.y, glyph.lr.x - glyph.ul.x, glyph.ul.y - glyph.lr.y);

            textOffset.x += glyphAdvance(glyph, scale);
        }
    });
    _meshRegistry.get(MeshName::quad).drawInstanced(line.size(), _statistic);
}

float Font::glyphAdvance(const Glyph &glyph, float scale) const {
    return (glyph.size.x + _spacingR) * scale;
}

} // namespace graphics

} // namespace reone
