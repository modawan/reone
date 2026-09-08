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
    Context context(options);
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
} // namespace
