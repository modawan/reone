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

#include "reone/gui/control/listbox.h"
#include "reone/gui/gui.h"
#include "reone/resource/parser/gff/gui.h"

#include "../../fixtures/engine.h"

using namespace reone;
using namespace reone::gui;
using namespace testing;

namespace {

// A list box whose prototype item has no text font, as when the GUI names a
// font that does not load.
struct FontlessListBox {
    TestEngine &engine = testEngine();
    graphics::GraphicsOptions options;
    PulsingAlpha pulsingAlpha;
    GUI gui {options, engine.services().scene.graphs, engine.services().graphics, engine.services().resource, pulsingAlpha};
    ListBox listBox {gui, engine.services().scene.graphs, engine.services().graphics, engine.services().resource};

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
