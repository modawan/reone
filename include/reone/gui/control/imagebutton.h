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

#include <optional>
#include <string_view>

#include "../control.h"

namespace reone {

namespace gui {

/**
 * The box a row's icon text is drawn in, at the lower right of its icon: the
 * text sits on the box's bottom, centred in the narrow box and centred or at
 * the right in the wide one. Lengths are in the units of \p iconSize, and
 * the box is drawn at the size the icon is drawn at.
 */
struct IconTextBox {
    float iconSize {0.0f};         /**< the icon's size; the box ends at the icon's right and bottom edges */
    float narrowWidth {0.0f};      /**< the box's width for text of fewer than three characters */
    float wideWidth {0.0f};        /**< the box's width for longer text */
    float growth {0.0f};           /**< what the box's width and height grow by past those edges */
    bool wideRightAligned {false}; /**< whether the wide box's text is at its right rather than centred */
};

class ImageButton : public Control {
public:
    ImageButton(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Control(
            gui,
            ControlType::ImageButton,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {

        _selectable = true;
        _takesKeyboardOnHover = true;
    }

    /** The part the inner extents of its border and its hilight share. */
    Extent innerExtent(const Extent &area) const override { return innerExtentWithHilight(area); }

    /**
     * Draws the button as a row: its frame, its icon in its frame with the
     * icon text beside it, and its text.
     *
     * @param iconTextColor the icon text's colour; none for the colour of the
     *        frame drawn
     */
    void render(
        const glm::ivec2 &offset,
        const std::vector<std::string> &text,
        const std::string &iconText,
        const std::shared_ptr<graphics::Texture> &iconTexture,
        const std::shared_ptr<graphics::Texture> &iconFrame,
        scene::IRenderPass &pass,
        const std::optional<glm::vec3> &iconTextColor = std::nullopt);

    /** Sets whether the selected icon frame is drawn at the GUI's pulsing alpha, keeping its colour. */
    void setIconFrameHilightPulsing(bool pulsing) { _iconFrameHilightPulsing = pulsing; }
    /**
     * While selected, the icon frame takes this colour (unset, the hilight's)
     * and, if pulsing, the GUI's pulsing alpha.
     */
    void setIconFrameHilight(std::optional<glm::vec3> color, bool pulsing) {
        _iconFrameHilightColor = std::move(color);
        _iconFrameHilightPulsing = pulsing;
    }
    /** Sets the font the icon text is named in; it shows the variant the GUI's font size picks. */
    void setIconFont(std::string baseFont);
    /** Shows the text and the icon text in the variants of their fonts the GUI's font size now picks. */
    void resetFont() override;
    /** Sets the box the icon text is drawn in; without one it sits in the icon's lower right corner. */
    void setIconTextBox(IconTextBox box) { _iconTextBox = box; }
    /** Sets whether the icon is drawn centred in its square, at its size, rather than stretched over it (see iconRect). */
    void setIconCentred(bool centred) { _iconCentred = centred; }

    /**
     * The screen rect, as position and size, an icon is drawn over in a
     * square \p side across at \p position: the whole square or, when
     * \p centred, the icon's art at \p artScale, at most the square on each
     * axis, centred in it.
     */
    static std::pair<glm::ivec2, glm::ivec2> iconRect(const graphics::Texture &icon, bool centred, float artScale,
                                                      const glm::ivec2 &position, int side);

    /**
     * Draws \p text in \p font at \p scale, in \p box at the lower right of
     * an icon drawn \p iconSize pixels square at \p iconPosition.
     */
    static void renderIconText(graphics::Font &font, std::string_view text, const glm::vec3 &color, float scale,
                               const glm::ivec2 &iconPosition, int iconSize, const IconTextBox &box);

    /** What an icon square shows: a frame, an icon and a count, any of them none. */
    struct IconSlot {
        graphics::Texture *frame {nullptr};
        graphics::Texture *icon {nullptr};
        std::string_view text;
    };

    /** How an icon square's frame, icon and count are drawn. */
    struct IconSlotLook {
        /** The frame's colour and opacity (iconFrameColor). */
        glm::vec4 frameColor {1.0f};
        /** The count's colour. */
        glm::vec3 textColor {1.0f};
        /** Whether the icon is centred at \p artScale rather than stretched (iconRect). */
        bool centred {false};
        float artScale {1.0f};
        /**
         * The count's font, box and scale; without a font no count is drawn,
         * and without a box it sits in the icon's lower right corner,
         * right-aligned.
         */
        graphics::Font *textFont {nullptr};
        const IconTextBox *textBox {nullptr};
        float textScale {1.0f};
    };

    /**
     * The colour and opacity an icon frame takes from \p row, the control it
     * stands in: the colour the row's frames show and their opacity, or
     * while the row is hilighted, \p hilightColor when given and full
     * opacity, or \p pulse when given.
     */
    static glm::vec4 iconFrameColor(const Control &row, const std::optional<glm::vec3> &hilightColor, std::optional<float> pulse);

    /**
     * Draws \p slot in a square \p side across at \p position: its frame,
     * its icon (iconRect) and its count (renderIconText). A square with
     * neither frame nor icon draws nothing.
     */
    static void renderIconSlot(scene::IRenderPass &pass, const IconSlot &slot, const IconSlotLook &look,
                               const glm::ivec2 &position, int side);

private:
    /** The selected icon frame's colour; unset, the hilight's. */
    std::optional<glm::vec3> _iconFrameHilightColor;
    /** Whether the selected icon frame is drawn at the GUI's pulsing alpha. */
    bool _iconFrameHilightPulsing {false};
    std::shared_ptr<graphics::Texture> _iconFrame;
    /** The icon text's font as named (setIconFont). */
    std::string _iconBaseFont {"dialogfont10x10"};
    /** The icon text's font in the variant the GUI's font size picks; the button's own without it. */
    std::shared_ptr<graphics::Font> _iconFont;
    std::optional<IconTextBox> _iconTextBox;
    /** The icon is drawn centred in its square at its size (setIconCentred). */
    bool _iconCentred {false};

    /** Draws the icon in its frame and the icon text, in the given colour or the frame's. */
    void renderIcon(
        const glm::ivec2 &offset,
        const std::string &iconText,
        const std::shared_ptr<graphics::Texture> &iconTexture,
        const std::shared_ptr<graphics::Texture> &iconFrame,
        scene::IRenderPass &pass,
        const std::optional<glm::vec3> &iconTextColor) const;
};

} // namespace gui

} // namespace reone
