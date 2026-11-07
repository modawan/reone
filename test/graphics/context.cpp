/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <gtest/gtest.h>

#include "reone/graphics/context.h"
#include "reone/graphics/framebuffer.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/statistic.h"

using namespace reone::graphics;

namespace {
// Exercise the real binding cache without a window, driver, or retail assets.
struct ReadbackGL {
    decltype(glad_glBindFramebuffer) bind {glad_glBindFramebuffer};
    decltype(glad_glReadBuffer) read {glad_glReadBuffer};
    decltype(glad_glPixelStorei) store {glad_glPixelStorei};
    decltype(glad_glReadPixels) pixels {glad_glReadPixels};
    ~ReadbackGL() {
        glad_glBindFramebuffer = bind;
        glad_glReadBuffer = read;
        glad_glPixelStorei = store;
        glad_glReadPixels = pixels;
    }
};

TEST(GraphicsContext, readback_invalidates_read_framebuffer_binding) {
    ReadbackGL restore;
    static int bindings;
    bindings = 0;
    glad_glBindFramebuffer = [](GLenum target, GLuint) {
        EXPECT_EQ(GL_READ_FRAMEBUFFER, target);
        ++bindings;
    };
    glad_glReadBuffer = [](GLenum) {};
    glad_glPixelStorei = [](GLenum, GLint) {};
    glad_glReadPixels = [](GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) {};

    GraphicsOptions options;
    Statistic statistic;
    Context context(options, statistic);
    Framebuffer scene;
    context.bindReadFramebuffer(scene, 0);
    EXPECT_EQ(1, bindings);
    context.captureScreen(1, 1);
    EXPECT_EQ(2, bindings);
    // The next scene blit must rebind its source, not read the window because
    // the cache still claims that source was left bound before the screenshot.
    context.bindReadFramebuffer(scene, 0);
    EXPECT_EQ(3, bindings);
}

struct DrawGL {
    decltype(glad_glBindVertexArray) bindVao {glad_glBindVertexArray};
    decltype(glad_glDrawElements) elements {glad_glDrawElements};
    decltype(glad_glDrawElementsInstanced) instanced {glad_glDrawElementsInstanced};
    ~DrawGL() {
        glad_glBindVertexArray = bindVao;
        glad_glDrawElements = elements;
        glad_glDrawElementsInstanced = instanced;
    }
};

struct DrawCalls {
    int vaoBinds {0};
    int elements {0};
    int instanced {0};
    GLsizei count {0};
    GLsizei instances {0};
};

DrawCalls g_drawCalls;

Mesh makeTriangle() {
    return Mesh(
        std::vector<Mesh::Vertex> {
            Mesh::VertexBuilder().position({0.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({1.0f, 0.0f, 0.0f}).build(),
            Mesh::VertexBuilder().position({0.0f, 1.0f, 0.0f}).build()},
        Mesh::VertexLayoutBuilder().stride(3 * sizeof(float)).offPosition(0).build(),
        std::vector<Mesh::Face> {Mesh::Face({0, 1, 2})});
}

void stubDrawGL() {
    g_drawCalls = DrawCalls();
    glad_glBindVertexArray = [](GLuint) { ++g_drawCalls.vaoBinds; };
    glad_glDrawElements = [](GLenum mode, GLsizei count, GLenum type, const void *) {
        EXPECT_EQ(GL_TRIANGLES, mode);
        EXPECT_EQ(GL_UNSIGNED_SHORT, type);
        ++g_drawCalls.elements;
        g_drawCalls.count = count;
    };
    glad_glDrawElementsInstanced = [](GLenum mode, GLsizei count, GLenum type, const void *, GLsizei instances) {
        EXPECT_EQ(GL_TRIANGLES, mode);
        EXPECT_EQ(GL_UNSIGNED_SHORT, type);
        ++g_drawCalls.instanced;
        g_drawCalls.count = count;
        g_drawCalls.instances = instances;
    };
}

TEST(GraphicsContext, draw_mesh_once_issues_one_plain_draw_and_counts_it) {
    DrawGL restore;
    stubDrawGL();
    GraphicsOptions options;
    Statistic statistic;
    Context context(options, statistic);
    auto mesh = makeTriangle();

    context.drawMesh(mesh);

    EXPECT_EQ(1, g_drawCalls.vaoBinds);
    EXPECT_EQ(1, g_drawCalls.elements);
    EXPECT_EQ(0, g_drawCalls.instanced);
    EXPECT_EQ(3, g_drawCalls.count);
    EXPECT_EQ(1, statistic.numDrawCalls());
}

TEST(GraphicsContext, draw_mesh_instanced_issues_one_instanced_draw_and_counts_it_once) {
    DrawGL restore;
    stubDrawGL();
    GraphicsOptions options;
    Statistic statistic;
    Context context(options, statistic);
    auto mesh = makeTriangle();

    context.drawMesh(mesh, 3);

    EXPECT_EQ(1, g_drawCalls.vaoBinds);
    EXPECT_EQ(0, g_drawCalls.elements);
    EXPECT_EQ(1, g_drawCalls.instanced);
    EXPECT_EQ(3, g_drawCalls.count);
    EXPECT_EQ(3, g_drawCalls.instances);
    // An instanced submission counts as one draw call, not one per instance.
    EXPECT_EQ(1, statistic.numDrawCalls());
}
TEST(GraphicsContext, draw_mesh_with_no_instances_draws_nothing) {
    DrawGL restore;
    stubDrawGL();
    GraphicsOptions options;
    Statistic statistic;
    Context context(options, statistic);
    auto mesh = makeTriangle();

    context.drawMesh(mesh, 0);

    // Only an instanced draw of zero instances renders nothing; a plain
    // draw would put the mesh on screen once.
    EXPECT_EQ(0, g_drawCalls.elements);
    EXPECT_EQ(1, g_drawCalls.instanced);
    EXPECT_EQ(0, g_drawCalls.instances);
}
} // namespace
