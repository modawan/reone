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
#include "scrollbar.h"

#include <optional>

namespace reone {

namespace gui {

class ImageButton;

constexpr int kDefaultSlotCount = 6;

class ListBox : public Control, private ScrollBar::IScrollable {
public:
    enum class SelectionMode {
        /** The row under the pointer is selected and stays so when the pointer leaves the rows. */
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
        /** The row's frames take this colour, selected or not. */
        std::optional<glm::vec3> frameColor;
        /** The opacity of the row's frames while the row is not selected. */
        std::optional<float> frameAlpha;
        /** The row's border colour, and its icon frame's, in place of the prototype's. */
        std::optional<glm::vec3> borderColor;
        /**
         * The row's hilight colour, and its icon frame's while hilighted, in
         * place of the prototype's, which has a hilight.
         */
        std::optional<glm::vec3> hilightColor;
        /** The colour of the text beside the row's icon, whether or not the row is selected. */
        std::optional<glm::vec3> iconTextColor;
        /** A row drawn through a toggle-button prototype shows this state. */
        std::optional<bool> on;
        bool invalid {false};
        /** A line of a block of text, which the pointer does not enter. */
        bool textLine {false};

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
        _takesKeyboardOnHover = true;
    }

    void clearItems();
    void addItem(Item &&item);
    /** Change a row's on state without rebuilding the list. */
    void setItemOn(int index, bool on) { _items.at(static_cast<size_t>(index)).on = on; }
    /** The row at \p index shows the prototype's frames again. */
    void clearItemFrameStyle(int index) {
        auto &item = _items.at(static_cast<size_t>(index));
        item.frameColor.reset();
        item.frameAlpha.reset();
    }
    /**
     * Adds \p text as rows, one for each line, that the list draws as one
     * block of text: a single label holding every line, as tall as the
     * lines' block height (Control::textBlockHeight).
     *
     * The block is drawn in the room inside the list's border, less an
     * enabled scroll bar, and is clipped to it, the list's padding in from
     * the room's left. When the block and the padding fit, the block starts
     * the padding below the room's top, stretched by its share of the room
     * left over, and the prototype's alignment places the lines in it. When
     * they do not fit, the room shows as many whole lines as it holds
     * (Control::textLineAdvance), the block scrolls one line at a time, and
     * at the last position its bottom is the padding above the room's
     * bottom.
     */
    void addTextLinesAsItems(const std::string &text);

    void clearSelection();

    void load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) override;
    bool handleMouseMotion(int x, int y) override;
    bool handleMouseWheel(int x, int y) override;
    /**
     * A press on a drawn row or on the enabled scroll bar, shown whether or
     * not the rows fit, keeps the mouse with the list until the release; a
     * press elsewhere does nothing. In a list that selects on click, a press
     * on another row than the selected one selects it and chooses it. On the
     * scroll bar, an arrow scrolls a row as
     * the wheel does, the track before or after the thumb a page back or on
     * (scrollPage), and the thumb drags (ScrollBar::press).
     *
     * @return whether the list keeps the mouse
     */
    bool handleMouseDown(int x, int y) override;
    /**
     * While the list keeps the mouse it takes every move, so the pointer
     * moves onto nothing else; a dragged thumb scrolls the list to the
     * position it is dragged to.
     */
    bool handleMouseDrag(int x, int y) override;
    /**
     * The release after a press on a row, over the list, clicks the list
     * when it is on the pressed row of a list that selects on hover, or on
     * any row of a list that selects on click when it ends a double click; a
     * list with no row selected takes no click.
     */
    bool handleMouseUp(int x, int y, bool over, int clicks) override;
    /**
     * A click acts on the selected row: a list that selects on hover calls the
     * item-click handler, and a double click the item-double-click handler.
     */
    bool handleClick(int x, int y, int clicks = 1) override;
    /** While the list is shown, an arrow or the track of the scroll bar held pressed acts again (ScrollBar::hold). */
    void update(float dt) override;
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;
    void stretch(float x, float y, int mask) override;

    void setExtent(Extent extent) override;
    void setExtentHeight(int height) override;
    void setSelectionMode(SelectionMode mode);
    /** Selects the row, or none for an index out of range; the selected row is scrolled into view. */
    void setSelectedItemIndex(int index);
    /**
     * Moves the selection a row down or up, as the arrow keys do: with no row
     * selected either way selects the first row; past either end the
     * selection wraps round when the list loops, and stays otherwise. The
     * selected row is scrolled into view. A held key stops at a wrap: its
     * repeats move nothing until it is pressed again.
     *
     * @param next whether the selection moves down
     * @param repeat whether the key is held, repeating
     * @return whether a row was selected
     */
    bool selectAdjacentItem(bool next, bool repeat);
    /**
     * Moves the selection as selectAdjacentItem does, for an arrow key; in a
     * list that selects on click, the row it moves to is then chosen as a
     * click on it chooses it. A list whose rows are not interactive scrolls a
     * row instead, as the wheel does.
     *
     * @return whether a row was selected, or the rows scrolled
     */
    bool stepSelection(bool next, bool repeat);
    /**
     * Acts on the selected row as the click that activates it does: a list
     * that selects on hover calls the item-click handler, one that selects on
     * click the item-double-click handler.
     *
     * @return whether a row is selected
     */
    bool activateSelectedItem();
    /**
     * The list takes the keyboard, and its selected row is hilighted again.
     * With no row selected, its first row is selected silently and scrolled
     * into view, unless the rows are not interactive; in a list that selects
     * on click, the row is then chosen as a click on it chooses it.
     */
    void takeKeyboard();
    /** The list loses the keyboard: its selected row is drawn without its hilight until a row is selected again or the list takes the keyboard. */
    void loseKeyboard();
    /**
     * Whether the list sounds at every move of an arrow key: Up with no row
     * selected sounds as Down does, and so does a scroll of rows that are
     * not interactive. Off unless set.
     */
    void setOwnSound(bool own) { _ownSound = own; }
    bool hasOwnSound() const { return _ownSound; }
    /**
     * Whether the pointer has moved the selection to another row since the
     * last call, a line of text excepted; the call clears it.
     */
    bool takeSelectionEntered();
    /** Whether a row the pointer can select, a line of text excepted, is drawn at the point. Control-space. */
    bool hasSelectableItemAt(int x, int y) const;
    /** Whether the rows take the pointer and keys; turning them on also ends scroll-only. */
    void setItemsInteractive(bool interactive);
    /**
     * Whether the rows only scroll: none takes the pointer or keys, and the
     * scroll bar steps one row at a time, its arrows showing while rows
     * remain before or after those drawn.
     */
    void setScrollOnly(bool scrollOnly);
    void setScrollBarEnabled(bool enabled);
    /**
     * Whether each row is as high as its text, and the rows are as wide as
     * the list's room less its padding, the padding in from its left and top.
     * Off unless set.
     */
    void setProtoMatchContent(bool match);
    void setRenderItemIconsForButtonProto(bool render);
    /** Sets the font the text beside the rows' icons is drawn in, in place of the prototype's. */
    void setIconTextFont(std::shared_ptr<graphics::Font> font);
    void scrollToBottom();
    /**
     * Scroll the least that brings row \p index into view: up to it when it
     * is above the visible rows, or down until it is the last visible row.
     */
    void scrollToItem(int index);
    /** A button prototype's icon frame takes the GUI's pulsing alpha while its row is hilighted. */
    void setIconFrameHilightPulsing(bool pulsing) { _iconFrameHilightPulsing = pulsing; }

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
    /** The rows' room: the area inside the border of the list less an enabled scroll bar on its side. */
    Extent getRowsArea() const;
    int visibleItemCount() const;
    /** Control-space rectangles of the rows shown, in order. */
    std::vector<Extent> visibleItemExtents() const;
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
    void shareProtoItem(const ListBox &list);
    Control &scrollBar() const { return *_scrollBar; }
    std::shared_ptr<Control> scrollBarOrNull() const { return _scrollBar; }
    int selectedItemIndex() const { return _selectedItemIndex; }

    // Event listeners

    void setOnItemClick(std::function<void(const std::string &)> fn) { _onItemClick = std::move(fn); }
    void setOnItemDoubleClick(std::function<void(const std::string &)> fn) { _onItemDoubleClick = std::move(fn); }

    // END Event listeners

private:
    class RowLook;

    /** The prototype as an image button; null when it is another type. */
    ImageButton *imageButtonProto() const;

    SelectionMode _selectionMode {SelectionMode::OnHover};
    std::shared_ptr<Control> _protoItem;
    bool _iconFrameHilightPulsing {false};
    std::shared_ptr<Control> _scrollBar;
    std::vector<Item> _items;
    int _slotCount {0};
    int _itemOffset {0};
    int _selectedItemIndex {-1};
    /** The lines of a block of text, all its rows' in turn (see addTextLinesAsItems). */
    std::vector<std::string> _textBlockLines;
    /** The row the left button was pressed on, until the release; -1 for none. */
    int _pressedItemIndex {-1};
    /** The left button was pressed on a drawn row, and is not yet released. */
    bool _holdsRow {false};
    /** The selected row is drawn without its hilight, the list having lost the keyboard. */
    bool _selectionUnhilighted {false};
    /** The list sounds at every move of an arrow key (setOwnSound). */
    bool _ownSound {false};
    bool _itemsInteractive {true};
    bool _scrollOnly {false}; /**< the rows only scroll (see setScrollOnly) */
    bool _scrollBarEnabled {true};
    bool _leftScrollBar {false};     /**< authored side for the scroll bar */
    bool _looping {false};           /**< the selection wraps round past either end */
    bool _selectionEntered {false};  /**< the pointer moved the selection to another row */
    bool _protoMatchContent {false}; /**< proto item height must match its content */
    bool _overflowing {false};       /**< the rows need more room than the list has */
    bool _renderItemIconsForButtonProto {false};
    /** The font of the text beside the rows' icons drawn through a button prototype; none for the prototype's. */
    std::shared_ptr<graphics::Font> _iconTextFont;
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
    /** The height of the block of text the rows hold (see addTextLinesAsItems). */
    int getTextBlockHeight() const;
    /** The lines the rows of a block of text hold. */
    int getTextBlockLineCount() const;
    /** The room's height in the prototype's lines, unrounded; 0 for a prototype without a font. */
    float getTextBlockLinesShown() const;
    /**
     * How many positions a block of text taller than the room scrolls
     * through: one more than the number of its lines that do not fit in the
     * room as whole lines. It can be less than one when only the padding
     * makes the block overflow.
     */
    int getTextBlockPositions() const;
    /** Draws the rows' block of text as one label, clipped inside the border. */
    void renderTextBlock(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass);
    /** Draws the scroll bar, when the list has one it shows; before the list's border and rows. */
    void renderScrollBar(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass);
    int getItemHeight(const Item &item) const;
    int getItemTextWidth() const;
    /** The prototype takes a row's place: the padding in from the room's left and top, a row wide (getItemWidth). */
    void matchProtoToRows();
    /** The drawn row whose own extent holds the point, or -1. Control-space. */
    int getDrawnItemIndex(int x, int y) const;
    /** The drawn row a press at the point acts on, a line of text excepted, or -1. Control-space. */
    int getPressableItemIndex(int x, int y) const;
    /** The pointer selects the row at \p index, which isn't a line of text. */
    void selectByPointer(int index);
    /** The row at \p index, or none for -1, becomes the selected one, hilighted. */
    void selectItem(int index);
    /** Whether the rows are the lines of one block of text. */
    bool isTextBlock() const;
    /**
     * The positions the list scrolls through: the rows beyond those shown,
     * and one; for a block of text, its positions, at least one.
     */
    int getScrollPositions() const override;
    /**
     * The values the scroll bar shows. Rows: the positions, the first row
     * shown and the rows shown. A block of text taller than the list: in
     * pixels, the height it overruns the room by and one, the share of that
     * it is scrolled by (all of it at the last position), and the room's
     * height.
     */
    ScrollBar::ScrollState getScrollState() const override;
    /** The position the list is scrolled to: the first row shown, or the line units a block of text is scrolled by. */
    int getScrollPosition() const override { return _itemOffset; }
    /** Shows the rows, or the block of text, from the position on. */
    void scrollToPosition(int position) override;
    /** The scroll bar shows the list as it is scrolled now. */
    void updateScrollBarState();
    /**
     * Scrolls a page back or on. A block of text pages by the lines shown,
     * other lists by the scroll bar's visible value, not past the last
     * position.
     */
    void scrollPage(bool next) override;
    bool shouldRenderItemIconsForButtonProto() const;
    void renderItemWithButtonProtoIcon(
        const glm::ivec2 &screenSize,
        const glm::ivec2 &offset,
        const Item &item,
        scene::IRenderPass &pass);
};

} // namespace gui

} // namespace reone
