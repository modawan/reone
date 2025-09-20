/*
 * Copyright (c) 2025 The reone project contributors
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
    AABB,
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

struct BasicAABB {
    glm::vec3 min;
    glm::vec3 max;
    glm::vec4 color;
};

struct Element {
    Element() :
        kind(Shape::Invalid) {}

    Element(Line line, const char *tag, float lifeTime) :
        kind(Shape::Line), tag(tag), lifeTime(lifeTime) {
        shape.line = line;
    }

    Element(Triangle triangle, const char *tag, float lifeTime) :
        kind(Shape::Triangle), tag(tag), lifeTime(lifeTime) {
        shape.triangle = triangle;
    }

    Element(Text text, const std::string &str, const char *tag, float lifeTime) :
        kind(Shape::Text), tag(tag), lifeTime(lifeTime), str(str) {
        shape.text = text;
    }

    Element(Point point, const char *tag, float lifeTime) :
        kind(Shape::Point), tag(tag), lifeTime(lifeTime) {
        shape.point = point;
    }

    Element(BasicAABB aabb, const char *tag, float lifeTime) :
        kind(Shape::AABB), tag(tag), lifeTime(lifeTime) {
        shape.aabb = aabb;
    }

    union {
        Line line;
        Triangle triangle;
        Text text;
        Point point;
        BasicAABB aabb;
    } shape;

    Shape kind;
    const char *tag;
    float lifeTime;
    std::string str; // only used for Text
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
    if ((1.0f - abs(dir.z)) < 0.001) {
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

static void renderAABB(const BasicAABB &aabb, const DrawContext &ctx, IRenderPass &pass) {
    ShaderProgram &program = ctx.aabbColor;
    ctx.services.context.useProgram(program);

    // FIXME: change setUniform to take an ArrayRef instead of a vector.
    // Then replace this vector with a C array.

    std::vector<glm::vec4> corners = {
        glm::vec4(aabb.min.x, aabb.min.y, aabb.min.z, 1.0f),
        glm::vec4(aabb.min.x, aabb.min.y, aabb.max.z, 1.0f),
        glm::vec4(aabb.min.x, aabb.max.y, aabb.min.z, 1.0f),
        glm::vec4(aabb.min.x, aabb.max.y, aabb.max.z, 1.0f),
        glm::vec4(aabb.max.x, aabb.min.y, aabb.min.z, 1.0f),
        glm::vec4(aabb.max.x, aabb.min.y, aabb.max.z, 1.0f),
        glm::vec4(aabb.max.x, aabb.max.y, aabb.min.z, 1.0f),
        glm::vec4(aabb.max.x, aabb.max.y, aabb.max.z, 1.0f),
    };
    program.setUniform("uCorners", corners);
    ctx.services.uniforms.setLocals([&](LocalUniforms &locals) {
        locals.reset();
        locals.color = aabb.color;
    });

    ctx.services.context.pushPolygonMode(PolygonMode::Line);
    ctx.aabb.draw(ctx.services.statistic);
    ctx.services.context.popPolygonMode();
}

static void doRender(const std::vector<Element> &elements, IRenderPass &pass, DrawContext &ctx) {
    ctx.services.context.pushFaceCullMode(FaceCullMode::None);
    ctx.services.context.pushBlendMode(BlendMode::Normal);
    ctx.services.context.pushPolygonMode(PolygonMode::Fill);

    for (const Element &e : elements) {
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
        case Shape::AABB:
            renderAABB(e.shape.aabb, ctx, pass);
            break;
        }
    }

    ctx.services.context.popPolygonMode();
    ctx.services.context.popBlendMode();
    ctx.services.context.popFaceCullMode();
}

class GlobalElements {
public:
    template <typename T>
    void add(const T &element, const char *tag, float lifeTime, const char *scene) {
        elementsByScene[scene].emplace_back(element, tag, lifeTime);
    }

    void add(const Text &element, const std::string &str, const char *tag, float lifeTime, const char *scene) {
        elementsByScene[scene].emplace_back(element, str, tag, lifeTime);
    }

    void clear(const char *tag, const char *scene) {
        for (Element &e : elementsByScene[scene]) {
            // Clear or skip elements with nullptr tags.
            if (!e.tag) {
                if (!tag) {
                    e.lifeTime = 0;
                }
                continue;
            }
            if (strcmp(e.tag, tag) == 0) {
                e.lifeTime = 0;
            }
        }
    }

    void update(float dt, const char *scene) {
        std::vector<Element> &elements = elementsByScene[scene];
        size_t numElements = elements.size();
        for (size_t i = 0; i < numElements;) {
            Element &element = elements[i];
            element.lifeTime -= dt;
            if (element.lifeTime > 0) {
                ++i;
                continue;
            }
            // Push expired elements to the end of the list and discard them all at
            // once.
            numElements -= 1;
            if (i < numElements) {
                std::swap(element, elements[numElements]);
                continue;
            }
        }
        elements.resize(numElements);
    }

    void render(IRenderPass &pass,
                GraphicsServices &services,
                resource::ResourceServices &resources,
                const char *scene) {
        if (!drawContext) {
            drawContext.reset(new DrawContext(services, resources));
        }
        doRender(elementsByScene[scene], pass, *drawContext);
    }

private:
    std::unique_ptr<DrawContext> drawContext;
    std::map<std::string, std::vector<Element>> elementsByScene;
};

static GlobalElements g_draw_elements;

void updateDrawDebug(float dt, const char *scene) {
    g_draw_elements.update(dt, scene);
}

void clearDrawDebug(const char *tag, const char *scene) {
    g_draw_elements.clear(tag, scene);
}

void renderDrawDebug(scene::IRenderPass &pass,
                     graphics::GraphicsServices &services,
                     resource::ResourceServices &resources,
                     const char *scene) {
    g_draw_elements.render(pass, services, resources, scene);
}

void drawDebugLine(glm::vec3 start, glm::vec3 end, glm::vec4 color,
                   float thickness, const char *tag, float lifeTime,
                   const char *scene) {
    g_draw_elements.add(Line {start, end, color, thickness}, tag, lifeTime, scene);
}

void drawDebugTriangle(glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec4 color,
                       const char *tag, float lifeTime, const char *scene) {
    g_draw_elements.add(Triangle {{v0, v1, v2}, color}, tag, lifeTime, scene);
}

void drawDebugText(const std::string &str, glm::vec3 position, glm::vec4 color, float scale,
                   const char *tag, float lifeTime, const char *scene) {
    g_draw_elements.add(Text {position, color, scale}, str, tag, lifeTime, scene);
}

void drawDebugPoint(glm::vec3 position, glm::vec4 color,
                    float scale, const char *tag, float lifeTime,
                    const char *scene) {
    g_draw_elements.add(Point {position, color, scale}, tag, lifeTime, scene);
}

void drawDebugAABB(glm::vec3 min, glm::vec3 max, glm::vec4 color,
                   const char *tag, float lifeTime, const char *scene) {
    g_draw_elements.add(BasicAABB {min, max, color}, tag, lifeTime, scene);
}

} // namespace reone
