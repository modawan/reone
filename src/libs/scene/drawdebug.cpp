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

#include "reone/scene/drawdebug.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/font.h"
#include "reone/graphics/material.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/render/pass.h"

#include <limits>
#include <map>
#include <string>
#include <vector>

namespace reone {

using namespace graphics;
using namespace scene;

enum class Shape {
    Invalid = 0,
    Line,
    Triangle,
    Text,
    Point,
    Box,
};

struct Line {
    glm::vec3 start;
    glm::vec3 end;
    glm::vec4 color;
    float thickness;
};

struct Triangle {
    glm::vec3 v[3];
    glm::vec4 color;
};

struct Text {
    glm::vec3 position;
    glm::vec4 color;
    float scale;
};

struct Point {
    glm::vec3 position;
    glm::vec4 color;
    float scale;
};

struct Box {
    glm::vec3 min;
    glm::vec3 max;
    glm::vec4 color;
};

struct Element {
    Element() :
        kind(Shape::Invalid) {}

    Element(Line line, float lifeTime) :
        kind(Shape::Line), lifeTime(lifeTime) {
        shape.line = line;
    }

    Element(Triangle triangle, float lifeTime) :
        kind(Shape::Triangle), lifeTime(lifeTime) {
        shape.triangle = triangle;
    }

    Element(Text text, const std::string &str, float lifeTime) :
        kind(Shape::Text), lifeTime(lifeTime), str(str) {
        shape.text = text;
    }

    Element(Point point, float lifeTime) :
        kind(Shape::Point), lifeTime(lifeTime) {
        shape.point = point;
    }

    Element(Box box, float lifeTime) :
        kind(Shape::Box), lifeTime(lifeTime) {
        shape.box = box;
    }

    union {
        Line line;
        Triangle triangle;
        Text text;
        Point point;
        Box box;
    } shape;

    Shape kind;
    std::string str; // only used for Text
    float lifeTime;
};

struct DrawContext {
    DrawContext(GraphicsServices &s, resource::ResourceServices &res) :
        services(s),
        mvpColor(s.shaderRegistry.get(ShaderProgramId::mvpColor)),
        aabbColor(s.shaderRegistry.get(ShaderProgramId::aabbColor)),
        textBillboard(s.shaderRegistry.get(ShaderProgramId::textBillboard)),
        textureBillboard(s.shaderRegistry.get(ShaderProgramId::billboard)),
        billboard(s.meshRegistry.get(MeshName::billboard)),
        aabb(s.meshRegistry.get(MeshName::aabb)),
        font(res.fonts.get("fnt_console")),
        point(res.textures.get("whitetarget")) {}

    ShaderProgram &mvpColor;
    ShaderProgram &aabbColor;
    ShaderProgram &textBillboard;
    ShaderProgram &textureBillboard;
    Mesh &billboard;
    Mesh &aabb;
    GraphicsServices &services;
    std::shared_ptr<graphics::Font> font;
    std::shared_ptr<graphics::Texture> point;
};

glm::mat4 transformLine(glm::vec3 start, glm::vec3 end, float thickness) {
    glm::vec3 seg = end - start;
    float scaleY = glm::length(seg);
    glm::vec3 dir = seg / glm::vec3(scaleY);

    // Change of basis to align Y-axis to dir.
    glm::vec3 newY, newX, newZ;
    if ((1.0f - std::abs(dir.z)) < 0.001) {
        // When a direction is parallel to the UP unit vector, the cross product
        // with UP is invalid.
        if (dir.z < 0.0f) {
            newY = -dir;
            newX = glm::vec3(1.0f, 0.0f, 0.0f);
            newZ = glm::vec3(0.0f, 0.0f, -1.0f);
        } else {
            newY = dir;
            newX = glm::vec3(1.0f, 0.0f, 0.0f);
            newZ = glm::vec3(0.0f, 0.0f, 1.0f);
        }
    } else {
        newY = dir;
        newX = glm::normalize(cross(newY, glm::vec3(0.0f, 0.0f, 1.0f)));
        newZ = glm::normalize(cross(newY, newX));
    }

    // Billboard mesh is centered at 0, so translate it to the center of the line.
    glm::vec3 pos = glm::mix(start, end, glm::vec3(0.5f, 0.5f, 0.5f));

    // Combine change of basis, scale to length of the segment, and translate.
    return glm::mat4(
        glm::vec4(newX * thickness, 0.0f),
        glm::vec4(newY * scaleY, 0.0f),
        glm::vec4(newZ, 0.0f),
        glm::vec4(pos, 1.0f));
}

static void renderLine(const Line &line, const DrawContext &ctx, IRenderPass &pass) {
    ctx.services.context.useProgram(ctx.mvpColor);
    ctx.services.uniforms.setLocals([&line](LocalUniforms &locals) {
        locals.reset();
        locals.model = transformLine(line.start, line.end, line.thickness); // glm::translate(glm::mat4(1.0), line.start);
        locals.color = line.color;
    });

    ctx.billboard.draw(ctx.services.statistic);
}

static void renderTriangle(const Triangle &tri, const DrawContext &ctx, IRenderPass &pass) {
    ShaderProgram &program = ctx.aabbColor;
    ctx.services.context.useProgram(program);

    // FIXME: change setUniform to take an ArrayRef instead of a vector.
    // Then replace this vector with a C array.

    std::vector<glm::vec4> corners = {glm::vec4(tri.v[0], 1.0f), glm::vec4(tri.v[1], 1.0f),
                                      glm::vec4(tri.v[2], 1.0f), glm::vec4(tri.v[0], 1.0f)};
    program.setUniform("uCorners", corners);
    ctx.services.uniforms.setLocals([&](LocalUniforms &locals) {
        locals.reset();
        locals.color = tri.color;
    });

    ctx.billboard.draw(ctx.services.statistic);
}

static void renderText(const Text &text, const std::string &str, const DrawContext &ctx, IRenderPass &pass) {
    ctx.services.context.useProgram(ctx.textBillboard);
    ctx.services.context.bindTexture(ctx.font->texture());

    float scale = text.scale * 0.2 / ctx.font->height();
    ctx.services.uniforms.setLocals([&](LocalUniforms &locals) {
        locals.reset();
        locals.color = text.color;
        locals.model = glm::translate(locals.model, text.position);
        locals.model = glm::scale(locals.model, glm::vec3(scale, -scale, 1.0f));
    });

    glm::vec3 zeroPosition(0.0f);
    glm::vec3 zeroOffset(0.0f);
    ctx.font->renderLine(str, zeroPosition, zeroOffset);
}

static void renderPoint(const Point &point, const DrawContext &ctx, IRenderPass &pass) {
    ctx.services.context.useProgram(ctx.textureBillboard);
    ctx.services.context.bindTexture(*ctx.point);

    ctx.services.uniforms.setLocals([&](LocalUniforms &locals) {
        locals.reset();
        locals.color = point.color;
        locals.model = glm::translate(locals.model, point.position);
        locals.model = glm::scale(locals.model, glm::vec3(point.scale * 0.15f));
    });

    ctx.billboard.draw(ctx.services.statistic);
}

static void renderBox(const Box &box, const DrawContext &ctx, IRenderPass &pass) {
    ShaderProgram &program = ctx.aabbColor;
    ctx.services.context.useProgram(program);

    // FIXME: change setUniform to take an ArrayRef instead of a vector.
    // Then replace this vector with a C array.

    std::vector<glm::vec4> corners = {
        glm::vec4(box.min.x, box.min.y, box.min.z, 1.0f),
        glm::vec4(box.min.x, box.min.y, box.max.z, 1.0f),
        glm::vec4(box.min.x, box.max.y, box.min.z, 1.0f),
        glm::vec4(box.min.x, box.max.y, box.max.z, 1.0f),
        glm::vec4(box.max.x, box.min.y, box.min.z, 1.0f),
        glm::vec4(box.max.x, box.min.y, box.max.z, 1.0f),
        glm::vec4(box.max.x, box.max.y, box.min.z, 1.0f),
        glm::vec4(box.max.x, box.max.y, box.max.z, 1.0f),
    };
    program.setUniform("uCorners", corners);
    ctx.services.uniforms.setLocals([&](LocalUniforms &locals) {
        locals.reset();
        locals.color = box.color;
    });

    ctx.services.context.pushPolygonMode(PolygonMode::Line);
    ctx.aabb.draw(ctx.services.statistic);
    ctx.services.context.popPolygonMode();
}

static void doRender(const Element &e, IRenderPass &pass, DrawContext &ctx) {
    switch (e.kind) {
    case Shape::Invalid:
        break;
    case Shape::Line:
        renderLine(e.shape.line, ctx, pass);
        break;
    case Shape::Triangle:
        renderTriangle(e.shape.triangle, ctx, pass);
        break;
    case Shape::Text:
        renderText(e.shape.text, e.str, ctx, pass);
        break;
    case Shape::Point:
        renderPoint(e.shape.point, ctx, pass);
        break;
    case Shape::Box:
        renderBox(e.shape.box, ctx, pass);
        break;
    }
}

/// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
static uint64_t hash64(uint64_t id, uint64_t basis) {
    constexpr uint64_t prime = 0x00000100000001B3;
    uint64_t result = basis;
    for (uint64_t shift = 0; shift < 64; shift += 8) {
        uint64_t byte = (id >> shift) & 0xFF;
        result ^= byte;
        result *= prime;
    }
    return result;
}

static glm::vec4 rgbaToVec(uint32_t argb) {
    uint32_t r = 0xFF & (argb >> 24);
    uint32_t g = 0xFF & (argb >> 16);
    uint32_t b = 0xFF & (argb >> 8);
    uint32_t a = 0xFF & argb;
    return glm::vec4(r, g, b, a) / 255.0f;
}

using ElementsMap = std::map<uint64_t, std::vector<Element>>;
using SceneElements = std::map<std::string, ElementsMap, std::less<>>;

struct DrawDebugState {
    std::unique_ptr<DrawContext> ctx;
    SceneElements sceneElements;
    std::deque<std::string> sceneStack;
    std::deque<uint64_t> idStack;
    std::deque<float> lifetimeStack;

    ElementsMap &elementsMap(std::string_view scene) {
        auto it = sceneElements.find(scene);
        if (it == sceneElements.end()) {
            return sceneElements[std::string(scene)];
        }
        return it->second;
    }

    std::vector<Element> &elements() {
        return elementsMap(scene())[hashId()];
    }

    uint64_t hashId() {
        uint64_t result = 0xCBF29CE484222325;
        for (const uint64_t &id : idStack) {
            result = hash64(id, result);
        }
        return result;
    }

    float lifetime() {
        if (lifetimeStack.empty()) {
            return std::numeric_limits<float>::max();
        }
        return lifetimeStack.back();
    }

    std::string scene() {
        if (sceneStack.empty()) {
            return "main";
        }
        return sceneStack.back();
    }
};

static DrawDebugState g_state;

/// Update and remove expired elements.
void updateDrawDebug(float dt) {
    for (auto &[sceneName, elementsById] : g_state.sceneElements) {
        for (auto &[id, elements] : elementsById) {
            for (auto &element : elements) {
                element.lifeTime -= dt;
            }
            auto it = std::remove_if(elements.begin(), elements.end(), [dt](const auto &element) {
                return element.lifeTime < 0;
            });
            elements.erase(it, elements.end());
        }
    }
}

/// Render elements for a scene.
void renderDrawDebug(scene::IRenderPass &pass,
                     graphics::GraphicsServices &services,
                     resource::ResourceServices &resources,
                     std::string_view scene) {
    if (!g_state.ctx) {
        g_state.ctx.reset(new DrawContext(services, resources));
    }

    auto &renderCtx = g_state.ctx->services.context;

    renderCtx.pushFaceCullMode(FaceCullMode::None);
    renderCtx.pushBlendMode(BlendMode::Normal);
    renderCtx.pushPolygonMode(PolygonMode::Fill);

    for (auto &[id, elements] : g_state.elementsMap(scene)) {
        for (auto &element : elements) {
            doRender(element, pass, *g_state.ctx);
        }
    }

    renderCtx.popPolygonMode();
    renderCtx.popBlendMode();
    renderCtx.popFaceCullMode();
}

namespace drawdebug {
void pushId(uint64_t id) {
    g_state.idStack.push_back(id);
}

void pushId(const void *id) {
    pushId(reinterpret_cast<uint64_t>(id));
}

void popId() {
    g_state.idStack.pop_back();
}

void pushLifetime(float lifetime) {
    g_state.lifetimeStack.push_back(lifetime);
}

void popLifetime() {
    g_state.lifetimeStack.pop_back();
}

void pushScene(std::string sceneName) {
    g_state.sceneStack.push_back(std::move(sceneName));
}

void popScene() {
    g_state.sceneStack.pop_back();
}

void clear() {
    g_state.elements().clear();
}

void line(glm::vec3 start, glm::vec3 end, uint32_t colorRgba, float thickness) {
    g_state.elements().emplace_back(
        Line {start, end, rgbaToVec(colorRgba), thickness}, g_state.lifetime());
}

void triangle(glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, uint32_t colorRgba) {
    g_state.elements().emplace_back(
        Triangle {{v0, v1, v2}, rgbaToVec(colorRgba)}, g_state.lifetime());
}

void text(const std::string &str, glm::vec3 position, uint32_t colorRgba, float scale) {
    g_state.elements().emplace_back(
        Text {position, rgbaToVec(colorRgba), scale}, str, g_state.lifetime());
}

void point(glm::vec3 position, uint32_t colorRgba, float scale) {
    g_state.elements().emplace_back(
        Point {position, rgbaToVec(colorRgba), scale}, g_state.lifetime());
}

void box(glm::vec3 min, glm::vec3 max, uint32_t colorRgba) {
    g_state.elements().emplace_back(
        Box {min, max, rgbaToVec(colorRgba)}, g_state.lifetime());
}

} // namespace drawdebug
} // namespace reone
