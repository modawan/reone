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

#include <gtest/gtest.h>

#include "reone/graphics/shaderprogram.h"
#include "reone/gui/control/listbox.h"
#include "reone/gui/gui.h"
#include "reone/resource/parser/gff/gui.h"
#include "reone/scene/render/pass.h"

#include "../../fixtures/engine.h"

using namespace reone;
using namespace reone::gui;
using namespace testing;

namespace {

// Graphics mocks of a test's own, so that what it expects of them ends with it.
struct LocalGraphics {
    graphics::TestGraphicsModule module;

    LocalGraphics() { module.init(); }
};

// A list box whose prototype item has no text font, as when the GUI names a
// font that does not load.
struct FontlessListBox {
    TestEngine &engine = testEngine();
    LocalGraphics localGraphics;
    graphics::GraphicsOptions options;
    PulsingAlpha pulsingAlpha;
    GUI gui {options, engine.services().scene.graphs, localGraphics.module.services(), engine.services().resource, pulsingAlpha};
    ListBox listBox {gui, engine.services().scene.graphs, localGraphics.module.services(), engine.services().resource};

    FontlessListBox() {
        resource::generated::GUI_CONTROLS definition;
        definition.CONTROLTYPE = static_cast<int>(ControlType::ListBox);
        definition.EXTENT = {0, 0, 100, 100};
        resource::generated::GUI_CONTROLS_PROTOITEM proto;
        proto.CONTROLTYPE = static_cast<int>(ControlType::Button);
        proto.EXTENT = {0, 0, 100, 20};
        definition.PROTOITEM = std::move(proto);
        listBox.load(definition, false);
    }
};

class NullRenderPass : public scene::IRenderPass {
public:
    void draw(graphics::Mesh &, graphics::Material &, const glm::mat4 &, const glm::mat4 &) override {}
    void drawSkinned(graphics::Mesh &, graphics::Material &, const glm::mat4 &, const glm::mat4 &,
                     const std::vector<glm::mat4> &) override {}
    void drawDangly(graphics::Mesh &, graphics::Material &, const glm::mat4 &, const glm::mat4 &,
                    const std::vector<glm::vec4> &) override {}
    void drawSaber(graphics::Mesh &, graphics::Material &, const glm::mat4 &, const glm::mat4 &,
                   const glm::mat4 &) override {}
    void drawBillboard(graphics::Texture &, const glm::vec4 &, const glm::mat4 &, const glm::mat4 &,
                       std::optional<float>) override {}
    void drawParticles(graphics::Texture &, graphics::FaceCullMode, bool, const glm::ivec2 &,
                       const std::vector<scene::ParticleInstance> &) override {}
    void drawProjectedBeam(graphics::Mesh &, const glm::mat4 &, const glm::mat4 &, const glm::vec4 &) override {}
    void drawGrass(float, float, graphics::Texture &, std::optional<std::reference_wrapper<graphics::Texture>> &,
                   const std::vector<scene::GrassInstance> &) override {}
    void drawAABB(const std::vector<glm::vec4> &) override {}
    void drawImage(graphics::Texture &, const glm::ivec2 &, const glm::ivec2 &, glm::vec4, glm::mat3x4,
                   scene::ImageAlphaMode, int) override {}
};

} // namespace

TEST(ListBox, adds_an_item_with_text_when_the_prototype_has_no_font) {
    FontlessListBox fixture;
    ListBox::Item item;
    item.text = "Unbreakable";

    fixture.listBox.addItem(std::move(item));

    ASSERT_EQ(1, fixture.listBox.getItemCount());
    EXPECT_EQ("Unbreakable", fixture.listBox.getItemAt(0).text);
}

TEST(ListBox, adds_each_line_of_text_as_an_item_when_the_prototype_has_no_font) {
    FontlessListBox fixture;

    fixture.listBox.addTextLinesAsItems("first\nsecond");

    ASSERT_EQ(2, fixture.listBox.getItemCount());
    EXPECT_EQ("first", fixture.listBox.getItemAt(0).text);
    EXPECT_EQ("second", fixture.listBox.getItemAt(1).text);
}

TEST(ListBox, keeps_its_items_through_a_stretch_when_the_prototype_has_no_font) {
    FontlessListBox fixture;
    ListBox::Item item;
    item.text = "Unbreakable";
    fixture.listBox.addItem(std::move(item));

    fixture.listBox.stretch(2.0f, 2.0f, Control::kStretchAll);

    EXPECT_EQ(1, fixture.listBox.getItemCount());
}

TEST(ListBox, sizes_content_rows_when_the_prototype_has_no_font) {
    FontlessListBox fixture;
    fixture.listBox.setProtoMatchContent(true);
    ListBox::Item item;
    item.text = "Unbreakable";

    fixture.listBox.addItem(std::move(item));

    EXPECT_EQ(1, fixture.listBox.getItemCount());
}

TEST(ListBox, lays_out_text_line_rows_when_the_prototype_has_no_font) {
    FontlessListBox fixture;
    fixture.listBox.setTextLineRows(true);

    fixture.listBox.addTextLinesAsItems("first\nsecond");

    EXPECT_EQ(2, fixture.listBox.getItemCount());
}

TEST(ListBox, renders_text_line_rows_when_the_prototype_has_no_font) {
    graphics::ShaderProgram shader {std::vector<std::shared_ptr<graphics::Shader>> {}};
    FontlessListBox fixture;
    fixture.listBox.setTextLineRows(true);
    fixture.listBox.addTextLinesAsItems("first\nsecond");
    EXPECT_CALL(fixture.localGraphics.module.shaderRegistry(), get(graphics::ShaderProgramId::mvpTexture))
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(shader));
    NullRenderPass pass;

    fixture.listBox.render({640, 480}, {0, 0}, pass);
}
