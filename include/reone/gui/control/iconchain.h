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

#pragma once

#include "../control.h"
#include "scrollbar.h"

#include <unordered_map>

namespace reone {

namespace gui {

class IconChain : public Control, private ScrollBar::IScrollable {
public:
    enum class State {
        Owned,
        Selectable,
        Locked
    };

    struct Item {
        std::string tag;
        int row {0};
        int column {0};
        std::shared_ptr<graphics::Texture> iconTexture;
        State state {State::Owned};
        bool selected {false};
    };

    struct Link {
        std::string sourceTag;
        std::string targetTag;
    };

    struct CellStyle {
        struct BorderColors {
            glm::vec3 locked {1.0f};
            glm::vec3 selectable {1.0f};
            glm::vec3 owned {1.0f};
            glm::vec3 selected {1.0f};
        };

        struct FocusedBorderColors {
            glm::vec3 locked {1.0f};
            glm::vec3 selectable {1.0f};
            glm::vec3 owned {1.0f};
            glm::vec3 selected {1.0f};
        };

        std::shared_ptr<graphics::Texture> backgroundTexture;
        std::shared_ptr<graphics::Texture> linkTexture;
        std::shared_ptr<Border> itemBorder;
        std::shared_ptr<BorderColors> borderColors;
        std::shared_ptr<FocusedBorderColors> focusedBorderColors;
        glm::ivec2 linkSize {0};
        int iconSize {0};
        bool dimLockedBackground {false};
        bool drawItemBorderFill {true};
        bool onlyDrawItemBorderWhenBright {false};
        bool drawItemBorderBeforeIcon {false};
    };

    IconChain(
        IGUI &gui,
        scene::ISceneGraphs &sceneGraphs,
        graphics::GraphicsServices &graphicsSvc,
        resource::ResourceServices &resourceSvc) :
        Control(
            gui,
            ControlType::IconChain,
            sceneGraphs,
            graphicsSvc,
            resourceSvc) {

        _selectable = true;
    }

    void clearItems();
    void addItem(Item item);
    void clearLinks();
    void addLink(Link link);
    void setItemSelected(const std::string &tag, bool selected);
    void setItemState(const std::string &tag, State state);
    void focusItem(const std::string &tag);
    const Item *focusedItem() const;

    bool handleMouseMotion(int x, int y) override;
    bool handleMouseWheel(int x, int y) override;
    bool handleClick(int x, int y, int clicks = 1) override;
    /**
     * The arrow keys step the focus, which the chart scrolls to show: Up and
     * Down to the row above or below, the last row above the first and the
     * first below the last, onto that row's nearest item at or before the
     * focused column; Left and Right to the item beside the focused one in
     * its row, when there is one.
     *
     * @return whether the key is an arrow key, with an item focused
     */
    bool handleKeyDown(input::KeyCode key) override;
    /**
     * A press on the scroll bar, shown whether or not every row fits, keeps
     * the mouse until the release: an arrow scrolls a row as the wheel does,
     * the track before or after the thumb a page of the rows shown back or
     * on, and the thumb drags (ScrollBar::press). A press elsewhere is left
     * to the click.
     *
     * @return whether the press was on the scroll bar
     */
    bool handleMouseDown(int x, int y) override;
    /** While a press on the scroll bar keeps the mouse it takes every move; a dragged thumb scrolls the rows. */
    bool handleMouseDrag(int x, int y) override;
    /** The release after a press on the scroll bar clicks nothing. */
    bool handleMouseUp(int x, int y, bool over, int clicks) override;
    /** While the chart is shown, an arrow or the track of the scroll bar held pressed acts again (ScrollBar::hold). */
    void update(float dt) override;
    void render(const glm::ivec2 &screenSize, const glm::ivec2 &offset, scene::IRenderPass &pass) override;

    void setColumnCount(int count);
    void setCellSize(int size);
    void setCellSpacing(int x, int y);
    void setCellOrigin(int x, int y);
    void setCellStep(int x, int y);
    void setRowOffsets(std::vector<int> offsets);
    void setScrollBar(std::shared_ptr<Control> scrollBar);
    void setCellStyle(CellStyle style);

    // Event listeners

    void setOnItemFocus(std::function<void(const std::string &)> fn) { _onItemFocus = std::move(fn); }
    void setOnItemFocusCleared(std::function<void()> fn) { _onItemFocusCleared = std::move(fn); }
    void setOnItemDoubleClick(std::function<void(const std::string &)> fn) { _onItemDoubleClick = std::move(fn); }

    // END Event listeners

private:
    std::vector<Item> _items;
    /** Each item's index in _items by its tag; of two with one tag, the first. */
    std::unordered_map<std::string, size_t> _itemIndexByTag;
    std::vector<Link> _links;
    int _columnCount {0};
    int _cellSize {0};
    glm::ivec2 _cellSpacing {0};
    glm::ivec2 _cellOrigin {-1};
    glm::ivec2 _cellStep {0};
    std::vector<int> _rowOffsets;
    std::shared_ptr<Control> _scrollBar;
    CellStyle _cellStyle;

    int _rowOffset {0};
    int _focusedItemIndex {-1};

    // Event listeners

    std::function<void(const std::string &)> _onItemFocus;
    std::function<void()> _onItemFocusCleared;
    std::function<void(const std::string &)> _onItemDoubleClick;

    // END Event listeners

    bool hasValidPosition(const Item &item) const;
    bool isItemVisible(const Extent &extent) const;
    int getCellOriginX() const;
    int getCellOriginY() const;
    int getCellStepX() const;
    int getCellStepY() const;
    int getVisibleRowCount() const;
    int getMaxRow() const;
    /** The positions the rows scroll through: the rows beyond those shown, and one. */
    int getScrollPositions() const override;
    /** The values the scroll bar shows: the positions, the first row shown and the rows shown. */
    ScrollBar::ScrollState getScrollState() const override;
    /** The first row shown. */
    int getScrollPosition() const override { return _rowOffset; }
    /** Shows the rows from \p rowOffset on; scrolling drops the focus. */
    void scrollToPosition(int rowOffset) override;
    /** Scrolls a page of the rows shown back or on, not past the last position. */
    void scrollPage(bool next) override;
    int getItemIndex(int x, int y) const;
    /** The index of the item at \p column of \p row; -1 when there is none. */
    int getItemIndexAt(int row, int column) const;
    /** Focuses the item of \p index, scrolling its row into view. */
    void stepFocus(int index);
    /** The index of the item tagged \p tag, the first of two with one tag; -1 for none. */
    int findItemIndex(const std::string &tag) const;
    const Item *findItem(const std::string &tag) const;
    void clearFocusedItem();
    Extent getItemExtent(const Item &item) const;
    Extent getItemIconExtent(const Extent &itemExtent) const;
    bool isItemBright(const Item &item) const;
    glm::vec4 getCellBackgroundColor(const Item &item) const;
    glm::vec3 getItemBorderColor(const Item &item, bool focused) const;
    glm::vec4 getItemIconColor(const Item &item) const;
    std::optional<glm::vec3> getFocusedBorderColor(const Item &item, bool focused) const;
    void renderLink(
        const Link &link,
        const glm::ivec2 &offset,
        scene::IRenderPass &pass) const;
    void renderItemBorder(
        const Item &item,
        bool focused,
        const Extent &extent,
        const glm::ivec2 &offset,
        scene::IRenderPass &pass);
    void renderFocusedBorder(
        const Item &item,
        bool focused,
        const Extent &extent,
        const glm::ivec2 &offset,
        scene::IRenderPass &pass);
};

} // namespace gui

} // namespace reone
