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

#include "reone/gui/controlrender.h"

using namespace reone;
using namespace reone::gui;

namespace {

/** Stands in for Control::Border, distinguishable by name in a trace. */
struct FakeBorder {
    std::string name;
};

/** What a render produced, in the order it produced it. */
struct RenderTrace {
    std::vector<std::string> layers;
    std::vector<std::string> fillBorders;
    std::vector<std::string> frameBorders;
};

RenderTrace trace(std::vector<const FakeBorder *> borders, bool hasScene) {
    RenderTrace result;
    renderControlLayers<FakeBorder>(
        ArrayRef<const FakeBorder *>(borders.data(), borders.size()),
        hasScene,
        [&result](const FakeBorder &border, BorderRenderPart part) {
            switch (part) {
            case BorderRenderPart::All:
                result.layers.push_back("border:" + border.name);
                break;
            case BorderRenderPart::Fill:
                result.layers.push_back("fill:" + border.name);
                result.fillBorders.push_back(border.name);
                break;
            case BorderRenderPart::Frame:
                result.layers.push_back("frame:" + border.name);
                result.frameBorders.push_back(border.name);
                break;
            }
        },
        [&result]() { result.layers.push_back("scene"); },
        [&result]() { result.layers.push_back("text"); });
    return result;
}

} // namespace

TEST(ControlRender, a_scene_is_composited_between_the_border_fill_and_its_frame) {
    FakeBorder border {"normal"};

    auto rendered = trace({&border}, true);

    EXPECT_EQ(
        (std::vector<std::string> {"fill:normal", "scene", "frame:normal", "text"}),
        rendered.layers);
}

TEST(ControlRender, a_control_without_a_scene_draws_its_border_whole_then_its_text) {
    FakeBorder border {"normal"};

    auto rendered = trace({&border}, false);

    EXPECT_EQ((std::vector<std::string> {"border:normal", "text"}), rendered.layers);
}

TEST(ControlRender, a_hilighted_scene_control_is_filled_and_framed_by_the_hilight) {
    // A control showing its HILIGHT state instead of its own border.
    FakeBorder hilight {"hilight"};

    auto rendered = trace({&hilight}, true);

    EXPECT_EQ(
        (std::vector<std::string> {"fill:hilight", "scene", "frame:hilight", "text"}),
        rendered.layers);
    EXPECT_EQ(rendered.fillBorders, rendered.frameBorders);
}

TEST(ControlRender, splitting_a_border_cannot_mix_the_normal_and_hilight_states) {
    // A hilight authored to sit over the border rather than replace it: both
    // are showing, so both are split, and neither borrows from the other.
    FakeBorder normal {"normal"};
    FakeBorder hilight {"hilight"};

    auto rendered = trace({&normal, &hilight}, true);

    EXPECT_EQ(
        (std::vector<std::string> {
            "fill:normal",
            "fill:hilight",
            "scene",
            "frame:normal",
            "frame:hilight",
            "text"}),
        rendered.layers);
    // Whatever was filled is exactly what gets framed, in the same order.
    EXPECT_EQ((std::vector<std::string> {"normal", "hilight"}), rendered.fillBorders);
    EXPECT_EQ(rendered.fillBorders, rendered.frameBorders);
}

TEST(ControlRender, a_scene_control_with_no_border_still_draws_its_scene_under_its_text) {
    auto rendered = trace({}, true);

    EXPECT_EQ((std::vector<std::string> {"scene", "text"}), rendered.layers);
}

TEST(ControlRender, a_borderless_control_without_a_scene_draws_only_its_text) {
    auto rendered = trace({}, false);

    EXPECT_EQ((std::vector<std::string> {"text"}), rendered.layers);
}
