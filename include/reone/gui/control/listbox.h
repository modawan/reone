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

#include "../control.h"

#include <optional>

namespace reone {

namespace gui {

constexpr int kDefaultSlotCount = 6;

class ListBox : public Control {
public:
    enum class SelectionMode {
        OnHover,
        OnClick
    };

    struct Item {
        std::string tag;
        std::string text;
        std::string iconText;
        std::shared_ptr<graphics::Texture> iconTexture;
        std::shared_ptr<graphics::Texture> iconFrame;
        std::optional<glm::vec3> textColor;
        /** A row drawn through a toggle-button prototype shows this state. */
        std::optional<bool> on;
        bool invalid {false};

        std::vector<std::string> _textLines;
    };

    ListBox(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Control(
            gui,
            ControlType::ListBox,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {

        _selectable = true;
    }

    void clearItems();
    void addItem(Item &&item);
    /** Change a row's on state without rebuilding the list. */
    void setItemOn(int index, bool on) { _items.at(static_cast<size_t>(index)).on = on; }
    void addTextLinesAsItems(const std::string &text);

    void clearSelection();

    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) override;
    bool handleMouseMotion(int x, int y) override;
    bool handleMouseWheel(int x, int y) override;
    bool handleClick(int x, int y, int clicks = 1) override;
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;
    void stretch(float x, float y, int mask) override;

    void changeProtoItemType(ControlType type);

    void setSelected(bool selected) override;
    void setExtent(Extent extent) override;
    void setExtentHeight(int height) override;
    void setSelectionMode(SelectionMode mode);
    void setSelectedItemIndex(int index);
    void setItemsInteractive(bool interactive);
    void setScrollBarEnabled(bool enabled);
    void setProtoMatchContent(bool match);
    /**
     * Make the rows the lines of one block of text, a line of the
     * prototype's font high each, drawn as one block inside the list's
     * border, less an enabled scroll bar, and clipped to that room. The block
     * sits the list's padding in from the room's left and below its top; a
     * block that fits is placed by the prototype's vertical alignment in the
     * row the room stretches it to. The prototype's own position plays no
     * part. It overflows when its height and the padding exceed the room
     * inside the border; it then scrolls a line at a time, one position per
     * line beyond those the room holds whole, and the last position puts its
     * bottom the padding above the room's bottom.
     */
    void setTextLineRows(bool lines);
    void setRenderItemIconsForButtonProto(bool render);
    void scrollToBottom();

    /**
     * Draws underneath the list's own frame and rows. Lets a GUI repaint
     * artwork that its panel bakes in at the authored row pitch, which no
     * longer registers with the rows once they are laid out at a different
     * density.
     */
    using BackgroundRenderer = std::function<void(const ListBox &, const glm::ivec2 &offset, scene::IRenderPass &)>;
    void setBackgroundRenderer(BackgroundRenderer renderer) { _backgroundRenderer = std::move(renderer); }

    /**
     * Places a scroll bar inside a framed panel, against the edge it was
     * authored on. The frame art draws its line inside the extent rather than
     * on its boundary, so the bar is inset past the line by
     * kScrollBarEdgeInset authored pixels and held clear of the corner slices
     * vertically.
     */
    static void insetScrollBar(Control &scrollBar,
                               const Extent &panel,
                               int borderDimension,
                               bool leftSide,
                               float layoutScale);

    /**
     * Measured at 1024x768 against the 16-pixel TSL panel art: the frame line
     * occupies the outermost authored pixels of the extent, and this inset
     * leaves roughly two authored pixels of background between it and the bar.
     */
    static constexpr int kScrollBarEdgeInset = 5;

    /** The area the rows occupy, inside the frame. Control-space. */
    Extent itemsViewport() const;
    int visibleItemCount() const;
    /** Control-space rectangle of the n-th currently visible row. */
    Extent visibleItemExtent(int index) const;
    /** Row density relative to the authored layout. */
    float layoutScale() const { return _layoutScale; }

    /**
     * The width of a row: the list's width inside its frame and padding, less
     * the scroll bar while it is enabled.
     */
    int getItemWidth() const;
    int getItemCount() const;
    /** Whether the rows need more room than the list has, so that it scrolls. */
    bool isOverflowing() const { return _overflowing; }
    const Item &getItemAt(int index) const;
    int getItemOffset() const { return _itemOffset; }
    void setItemOffset(int offset);

    Control &protoItem() const { return *_protoItem; }
    Control *protoItemOrNull() const { return _protoItem.get(); }
    Control &scrollBar() const { return *_scrollBar; }
    std::shared_ptr<Control> scrollBarOrNull() const { return _scrollBar; }
    int selectedItemIndex() const { return _selectedItemIndex; }

    // Event listeners

    void setOnItemClick(std::function<void(const std::string &)> fn) { _onItemClick = std::move(fn); }
    void setOnItemDoubleClick(std::function<void(const std::string &)> fn) { _onItemDoubleClick = std::move(fn); }

    // END Event listeners

private:
    SelectionMode _selectionMode {SelectionMode::OnHover};
    std::shared_ptr<Control> _protoItem;
    std::shared_ptr<Control> _scrollBar;
    std::vector<Item> _items;
    int _slotCount {0};
    int _itemOffset {0};
    int _selectedItemIndex {-1};
    bool _itemsInteractive {true};
    bool _scrollBarEnabled {true};
    bool _leftScrollBar {false}; /**< authored side for the scroll bar */
    bool _protoMatchContent {false}; /**< proto item height must match its content */
    bool _textLineRows {false};      /**< rows are the lines of one block of text */
    bool _overflowing {false};       /**< the rows need more room than the list has */
    bool _renderItemIconsForButtonProto {false};
    float _layoutScale {1.0f};
    BackgroundRenderer _backgroundRenderer;

    // Event listeners

    std::function<void(const std::string &)> _onItemClick;
    std::function<void(const std::string &)> _onItemDoubleClick;

    // END Event listeners

    void updateItemSlots();
    void updateItemsLayout();
    int getInnerHeight() const;
    float getItemPitch(const Item &item) const;
    /**
     * How far below the padding a block of text-line rows that fits starts:
     * the offset of its \p content height inside the row the room stretches
     * it to, under the prototype's vertical alignment.
     */
    int getTextBlockAlignmentOffset(int content, int room, int padding) const;
    /** The height of a line of text-line rows: the prototype's font height. */
    int getTextLineHeight() const;
    /** Draws text-line rows as one block, clipped inside the border. */
    void renderTextLineRows(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass);
    int getItemHeight(const Item &item) const;
    int getItemTextWidth() const;
    int getItemIndex(int y) const;
    bool shouldRenderItemIconsForButtonProto() const;
    void renderItemWithButtonProtoIcon(
        const glm::ivec2 &screenSize,
        const glm::ivec2 &offset,
        const Item &item,
        scene::IRenderPass &pass);
};

} // namespace gui

} // namespace reone
