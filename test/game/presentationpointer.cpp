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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "reone/game/presentationpointer.h"
#include "reone/graphics/context.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/shaderprogram.h"
#include "reone/graphics/statistic.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"

using namespace reone;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::resource;
using namespace testing;

namespace {
class RecordingContext : public Context {
public:
    using Context::Context;
    std::vector<Texture *> bound;
    void bindTexture(Texture &texture, int) override { bound.push_back(&texture); }
    void useProgram(ShaderProgram &) override {}
    // Stop at raster submission: texture choice and transform are real Cursor code.
    void withBlendMode(BlendMode, const std::function<void()> &) override {}
};
class RecordingUniforms : public Uniforms {
public:
    using Uniforms::Uniforms;
    LocalUniforms locals;
    void setLocals(const std::function<void(LocalUniforms &)> &f) override { f(locals); }
};
class PointerShaders : public ShaderRegistry {
public:
    ShaderProgram shader {std::vector<std::shared_ptr<Shader>> {}};
    ShaderProgram &get(const std::string &) override { return shader; }
};
class CursorProvider : public ICursors {
public:
    std::shared_ptr<Cursor> cursor;
    std::vector<CursorType> requests;
    std::shared_ptr<Cursor> get(CursorType type) override { requests.push_back(type); return cursor; }
};
}

TEST(PresentationPointer, uses_provider_and_preserves_visibility_position_pressed_state_and_scale_without_game) {
    GraphicsOptions options;
    options.width = 1024; options.height = 768; options.guiScale = 1.0f;
    RecordingContext context(options);
    Statistic statistic;
    MeshRegistry meshes(statistic);
    PointerShaders shaders;
    RecordingUniforms uniforms(context);
    auto up = std::make_shared<Texture>("up", TextureType::TwoDim, Texture::Properties {});
    auto down = std::make_shared<Texture>("down", TextureType::TwoDim, Texture::Properties {});
    auto pixels = std::make_shared<ByteBuffer>(32 * 32 * 4, 0);
    up->setPixels(32, 32, PixelFormat::RGBA8, Texture::Layer {pixels});
    down->setPixels(32, 32, PixelFormat::RGBA8, Texture::Layer {pixels});
    CursorProvider provider;
    provider.cursor = std::make_shared<Cursor>(up, down, context, meshes, shaders, uniforms, statistic);
    PresentationPointer pointer(provider);
    pointer.setPosition({5, 6}); pointer.setPressed(true);
    pointer.render(options, false);
    EXPECT_TRUE(context.bound.empty());
    pointer.setType(CursorType::Default);
    pointer.setType(CursorType::Default);
    EXPECT_EQ((std::vector<CursorType> {CursorType::Default}), provider.requests);
    pointer.setPosition({50, 60}); pointer.setPressed(false);
    pointer.render(options, false);
    ASSERT_EQ(1u, context.bound.size());
    EXPECT_EQ(up.get(), context.bound.back());
    EXPECT_FLOAT_EQ(50.0f, uniforms.locals.model[3].x);
    EXPECT_FLOAT_EQ(60.0f, uniforms.locals.model[3].y);
    EXPECT_FLOAT_EQ(32 * 0.64f, uniforms.locals.model[0].x);
    pointer.setPressed(true);
    pointer.render(options, false);
    ASSERT_EQ(2u, context.bound.size());
    EXPECT_EQ(down.get(), context.bound.back());
    pointer.render(options, true);
    EXPECT_EQ(2u, context.bound.size());
    pointer.setType(CursorType::None);
    pointer.render(options, false);
    EXPECT_EQ(2u, context.bound.size());
    pointer.setType(CursorType::Talk);
    pointer.setPressed(false);
    pointer.render(options, false);
    EXPECT_EQ(3u, context.bound.size());
    EXPECT_EQ(up.get(), context.bound.back());
}

TEST(PresentationPointer, preserves_the_existing_contextual_decision_table) {
    EXPECT_EQ(CursorType::Talk, contextualCursor(ObjectType::Creature, false, false));
    EXPECT_EQ(CursorType::Attack, contextualCursor(ObjectType::Creature, false, true));
    EXPECT_EQ(CursorType::Pickup, contextualCursor(ObjectType::Creature, true, false));
    EXPECT_EQ(CursorType::Pickup, contextualCursor(ObjectType::Creature, true, true));
    EXPECT_EQ(CursorType::Door, contextualCursor(ObjectType::Door, false, false));
    EXPECT_EQ(CursorType::Pickup, contextualCursor(ObjectType::Placeable, false, false));
    EXPECT_EQ(CursorType::Default, contextualCursor(ObjectType::Item, false, false));
}
