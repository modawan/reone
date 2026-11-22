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

#include "reone/gui/control/listbox.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/font.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/textutil.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/imagebutton.h"
#include "reone/gui/control/scrollbar.h"
#include "reone/gui/control/togglebutton.h"
#include "reone/gui/gui.h"
#include "reone/resource/gff.h"
#include "reone/resource/resources.h"
#include "reone/scene/render/pass.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace gui {

static constexpr glm::vec3 kInvalidItemBorderColor {1.0f, 0.0f, 0.0f};

void ListBox::clearItems() {
    _items.clear();
    _textBlockLines.clear();
    _itemOffset = 0;
    _selectedItemIndex = -1;
}

void ListBox::addItem(Item &&item) {
    if (!_protoItem)
        return;

    if (const auto &font = _protoItem->text().font) {
        item._textLines = breakText(item.text, *font, getItemTextWidth(), _protoItem->scale());
    }
    _items.push_back(std::move(item));

    updateItemSlots();
}

void ListBox::addTextLinesAsItems(const std::string &text) {
    if (!_protoItem)
        return;

    // The block is as wide as a row and wraps there.
    matchProtoToRows();
    std::istringstream stream(text);
    std::string logicalLine;
    while (std::getline(stream, logicalLine)) {
        std::vector<std::string> lines;
        if (logicalLine.empty()) {
            lines.push_back("");
        } else if (const auto &font = _protoItem->text().font) {
            lines = breakText(logicalLine, *font, getItemTextWidth(), _protoItem->scale());
        } else {
            lines.push_back(logicalLine);
        }

        for (auto &line : lines) {
            Item item;
            item.text = line;
            item.textLine = true;
            item._textLines = std::vector<std::string> {line};
            _items.push_back(std::move(item));
        }
    }

    if (!text.empty() && text.back() == '\n') {
        Item item;
        item.textLine = true;
        item._textLines = std::vector<std::string> {""};
        _items.push_back(std::move(item));
    }

    updateItemSlots();
}

void ListBox::clearSelection() {
    _selectedItemIndex = -1;
}

void ListBox::load(const resource::generated::GUI_BASECONTROL &gui, bool protoItem) {
    Control::load(gui, protoItem);

    auto &controlStruct = *static_cast<const resource::generated::GUI_CONTROLS *>(&gui);
    if (controlStruct.PROTOITEM) {
        _protoItem = _gui.newControl(getType(*controlStruct.PROTOITEM), getTag(*controlStruct.PROTOITEM));
        _protoItem->load(*controlStruct.PROTOITEM, true);
    }
    if (controlStruct.SCROLLBAR) {
        _scrollBar = _gui.newControl(getType(*controlStruct.SCROLLBAR), getTag(*controlStruct.SCROLLBAR));
        _scrollBar->load(*controlStruct.SCROLLBAR);
        // A list's scroll arrows pulse.
        if (auto scrollBar = ScrollBar::of(_scrollBar)) {
            scrollBar->setArrowsPulsing(true);
        }
    }
    _leftScrollBar = controlStruct.LEFTSCROLLBAR != 0;
    _looping = (controlStruct.LOOPING & 1) != 0;
}

bool ListBox::handleMouseMotion(int x, int y) {
    if (!_itemsInteractive) {
        return false;
    }
    if (_selectionMode == SelectionMode::OnHover) {
        int itemIdx = getDrawnItemIndex(x, y);
        if (itemIdx != -1 && !_items[itemIdx].textLine) {
            selectByPointer(itemIdx);
        }
    }
    return false;
}

void ListBox::selectByPointer(int index) {
    if (index != _selectedItemIndex) {
        _selectionEntered = true;
    }
    selectItem(index);
}

void ListBox::selectItem(int index) {
    _selectedItemIndex = index;
    _selectionUnhilighted = false;
}

bool ListBox::takeSelectionEntered() {
    const bool entered = _selectionEntered;
    _selectionEntered = false;
    return entered;
}

bool ListBox::hasSelectableItemAt(int x, int y) const {
    return getPressableItemIndex(x, y) != -1;
}

// The drawn row under a point, or -1. Each row takes its own extent, so the
// padding between two rows, rows below the drawn ones, the space beside the
// rows and the scroll bar are no row.
int ListBox::getDrawnItemIndex(int x, int y) const {
    if (!_protoItem) {
        return -1;
    }
    if (_scrollBar && _scrollBar->isVisible() && _scrollBar->extent().contains(x, y)) {
        return -1;
    }
    const Control::Extent &protoExtent = _protoItem->extent();
    if (x < protoExtent.left || x >= protoExtent.left + protoExtent.width) {
        return -1;
    }
    const int end = std::min(_itemOffset + _slotCount, getItemCount());
    float itemTop = static_cast<float>(protoExtent.top);
    for (int i = _itemOffset; i < end; ++i) {
        const Item &item = _items[i];
        const int top = static_cast<int>(std::lround(itemTop));
        if (y >= top && y < top + getItemHeight(item)) {
            return i;
        }
        itemTop += getItemPitch(item);
    }
    return -1;
}

// The wheel scrolls a position a step, as the scroll bar's arrows do, up
// scrolling back; a list takes it at either end too.
bool ListBox::handleMouseWheel(int x, int y) {
    if (y == 0) {
        return false;
    }
    scrollBy(-y);
    return true;
}

void ListBox::updateItemSlots() {
    _slotCount = 0;

    if (isTextBlock()) {
        _textBlockLines.clear();
        for (const Item &item : _items) {
            _textBlockLines.insert(_textBlockLines.end(), item._textLines.begin(), item._textLines.end());
        }
        // The block overflows when it and the padding are taller than the
        // room, which then shows the lines it holds whole.
        _overflowing = getTextBlockHeight() + static_cast<int>(_padding * _layoutScale) > getInnerHeight();
        _slotCount = _overflowing ? static_cast<int>(getTextBlockLinesShown()) : static_cast<int>(_items.size());
        _itemOffset = std::min(_itemOffset, getScrollPositions() - 1);
        if (_scrollBar) {
            _scrollBar->setVisible(_scrollBarEnabled);
        }
        return;
    }

    // Increase the number of slots until they no longer fit vertically.
    // KOTOR lays out unequal rows inside the list border, with one padding
    // interval accounted for by every visible row.
    float y = 0.0f;
    int innerHeight = getInnerHeight();
    for (size_t i = _itemOffset; i < _items.size(); ++i) {
        y += getItemPitch(_items[i]);

        if (static_cast<int>(std::lround(y)) > innerHeight)
            break;

        ++_slotCount;
    }
    _overflowing = _items.size() > _slotCount;

    if (_scrollBar) {
        _scrollBar->setVisible(_scrollBarEnabled);
    }
}

int ListBox::getPressableItemIndex(int x, int y) const {
    if (!_itemsInteractive) {
        return -1;
    }
    const int itemIdx = getDrawnItemIndex(x, y);
    return itemIdx != -1 && !_items[itemIdx].textLine ? itemIdx : -1;
}

bool ListBox::handleMouseDown(int x, int y) {
    _pressedItemIndex = getPressableItemIndex(x, y);
    auto scrollBar = ScrollBar::of(_scrollBar);
    if (scrollBar && scrollBar->pressIfOn(x, y, *this)) {
        return true;
    }
    // The press stays taken even when choosing the row puts the GUI away,
    // which releases the list.
    const bool holdsRow = getDrawnItemIndex(x, y) != -1;
    _holdsRow = holdsRow;
    if (_pressedItemIndex != -1 && _selectionMode == SelectionMode::OnClick && _pressedItemIndex != _selectedItemIndex) {
        selectByPointer(_pressedItemIndex);
        if (_onItemClick) {
            _onItemClick(_items[_pressedItemIndex].tag);
        }
    }
    return holdsRow;
}

bool ListBox::handleMouseDrag(int x, int y) {
    auto scrollBar = ScrollBar::of(_scrollBar);
    if (scrollBar && scrollBar->dragIfHeld(x, y, *this)) {
        return true;
    }
    return _holdsRow;
}

bool ListBox::handleMouseUp(int x, int y, bool over, int clicks) {
    const int pressedItemIdx = _pressedItemIndex;
    _pressedItemIndex = -1;
    _holdsRow = false;
    if (auto scrollBar = ScrollBar::of(_scrollBar)) {
        scrollBar->release();
    }
    if (pressedItemIdx == -1 || !over) {
        return false;
    }
    // The click goes to the selected row; with none, nothing takes it.
    const int itemIdx = getPressableItemIndex(x, y);
    if (itemIdx == -1 || _selectedItemIndex == -1) {
        return false;
    }
    return _selectionMode == SelectionMode::OnHover ? itemIdx == pressedItemIdx : clicks > 1;
}

bool ListBox::handleClick(int x, int y, int clicks) {
    if (!_itemsInteractive || _selectedItemIndex == -1) {
        return false;
    }
    const std::string tag = _items[_selectedItemIndex].tag;
    if (_selectionMode == SelectionMode::OnHover && _onItemClick) {
        _onItemClick(tag);
    }
    if (clicks > 1 && _onItemDoubleClick) {
        _onItemDoubleClick(tag);
    }
    return true;
}

void ListBox::update(float dt) {
    Control::update(dt);
    if (!_visible) {
        return;
    }
    if (auto scrollBar = ScrollBar::of(_scrollBar)) {
        scrollBar->hold(*this);
    }
}

void ListBox::scrollToPosition(int position) {
    _itemOffset = position;
    updateItemSlots();
}

void ListBox::scrollPage(bool next) {
    const int lastPosition = getScrollPositions() - 1;
    const int page = isTextBlock() ? _slotCount : getScrollState().visibleValue;
    if (!next) {
        _itemOffset = std::max(_itemOffset - page, 0);
    } else if (_itemOffset < lastPosition) {
        _itemOffset = std::min(_itemOffset + page, lastPosition);
    }
    updateItemSlots();
}

bool ListBox::isTextBlock() const {
    return !_items.empty() && _items.front().textLine;
}

int ListBox::getScrollPositions() const {
    if (isTextBlock()) {
        return _overflowing ? std::max(getTextBlockPositions(), 1) : 1;
    }
    return std::max(getItemCount() - _slotCount, 0) + 1;
}

int ListBox::getTextBlockLineCount() const {
    int lineCount = 0;
    for (const Item &item : _items) {
        lineCount += static_cast<int>(item._textLines.size());
    }
    return lineCount;
}

int ListBox::getTextBlockHeight() const {
    return _protoItem->textBlockHeight(getTextBlockLineCount());
}

float ListBox::getTextBlockLinesShown() const {
    const float advance = _protoItem->textLineAdvance();
    return advance > 0.0f ? static_cast<float>(getInnerHeight()) / advance : 0.0f;
}

int ListBox::getTextBlockPositions() const {
    if (getInnerHeight() == 0 || _protoItem->textLineAdvance() <= 0.0f) {
        return 1;
    }
    // The block's lines, less the whole ones the room shows, and one.
    return getTextBlockLineCount() - static_cast<int>(getTextBlockLinesShown()) + 1;
}

ScrollBar::ScrollState ListBox::getScrollState() const {
    ScrollBar::ScrollState state;
    const int positions = getScrollPositions();
    if (isTextBlock() && _overflowing) {
        const int overrun = getTextBlockHeight() - getInnerHeight();
        state.maxValue = overrun + 1;
        state.visibleValue = getInnerHeight();
        state.curValue = _itemOffset >= positions - 1
                             ? state.maxValue
                             : static_cast<int>(static_cast<float>(overrun) * (static_cast<float>(_itemOffset) / static_cast<float>(positions - 1)));
        state.extendedRange = _scrollOnly;
        return state;
    }
    state.maxValue = positions;
    state.curValue = _itemOffset;
    state.visibleValue = _scrollOnly ? 1 : std::min(_slotCount, getItemCount());
    state.extendedRange = _scrollOnly;
    return state;
}

void ListBox::updateScrollBarState() {
    if (auto scrollBar = ScrollBar::of(_scrollBar)) {
        scrollBar->setScrollState(getScrollState());
    }
}

// The prototype is every row's: a row's own colours, opacities and state
// are put on it while the row is drawn and taken off after, whatever the
// drawing does.
class ListBox::RowLook {
public:
    RowLook(Control &proto, const Item &item) :
        _proto(proto),
        _textColor(proto.text().color),
        _usesFrameColor(proto.usesBorderColorOverride()),
        _frameColor(proto.borderColorOverride()),
        _borderAlpha(proto.frameAlpha()) {
        if (item.textColor) {
            proto.setTextColor(*item.textColor);
        }
        if (item.frameColor) {
            proto.setBorderColorOverride(*item.frameColor);
            proto.setUseBorderColorOverride(true);
        }
        if (item.frameAlpha) {
            proto.setBorderAlpha(*item.frameAlpha);
        }
        if (item.borderColor && proto.hasBorder()) {
            _borderColor = proto.border().color;
            proto.setBorderColor(*item.borderColor);
        }
        if (item.hilightColor && proto.hasHilight()) {
            _hilightColor = proto.hilight().color;
            _selectedTextColor = proto.text().selectedColor;
            proto.setHilightColor(*item.hilightColor);
        }
        if (item.on && proto.type() == ControlType::ToggleButton) {
            auto &toggle = static_cast<ToggleButton &>(proto);
            _on = toggle.isOn();
            toggle.setOn(*item.on);
        }
    }

    ~RowLook() {
        _proto.setTextColor(_textColor);
        _proto.setBorderColorOverride(_frameColor);
        _proto.setUseBorderColorOverride(_usesFrameColor);
        _proto.setBorderAlpha(_borderAlpha);
        if (_borderColor) {
            _proto.setBorderColor(*_borderColor);
        }
        // Only colours the row changed are put back: the hilight's, then the
        // selected text's, which setting the hilight's also sets.
        if (_hilightColor) {
            _proto.setHilightColor(*_hilightColor);
            _proto.setSelectedTextColor(_selectedTextColor);
        }
        if (_on) {
            static_cast<ToggleButton &>(_proto).setOn(*_on);
        }
    }

    RowLook(const RowLook &) = delete;
    RowLook &operator=(const RowLook &) = delete;

private:
    Control &_proto;
    glm::vec3 _textColor;
    bool _usesFrameColor;
    glm::vec3 _frameColor;
    float _borderAlpha;
    std::optional<glm::vec3> _borderColor;
    std::optional<glm::vec3> _hilightColor;
    std::optional<glm::vec3> _selectedTextColor;
    std::optional<bool> _on;
};

void ListBox::render(const glm::ivec2 &screenSize,
                     const glm::ivec2 &offset,
                     IRenderPass &pass) {
    if (!_visible)
        return;

    if (_backgroundRenderer) {
        _backgroundRenderer(*this, offset, pass);
    }
    // The scroll bar is drawn first, then the list's border, then the rows.
    renderScrollBar(screenSize, offset, pass);
    Control::render(screenSize, offset, pass);

    if (!_protoItem)
        return;

    if (isTextBlock()) {
        renderTextBlock(screenSize, offset, pass);
        return;
    }

    float itemY = 0.0f;
    for (int i = 0; i < _slotCount; ++i) {
        int itemIdx = i + _itemOffset;
        if (itemIdx >= _items.size())
            break;

        const Item &item = _items[itemIdx];
        glm::ivec2 itemOffset(offset);
        itemOffset.y += static_cast<int>(std::lround(itemY));
        if (_protoMatchContent) {
            _protoItem->setExtentHeight(getItemHeight(item));
        }
        _protoItem->setSelected(_selectedItemIndex == itemIdx && !_selectionUnhilighted);

        {
            const RowLook look(*_protoItem, item);
            if (_protoItem->type() == ControlType::ImageButton) {
                static_cast<ImageButton &>(*_protoItem).render(itemOffset, item._textLines, item.iconText, item.iconTexture, item.iconFrame, pass, item.iconTextColor);
            } else if (shouldRenderItemIconsForButtonProto()) {
                renderItemWithButtonProtoIcon(screenSize, itemOffset, item, pass);
            } else {
                _protoItem->setTextLines(item._textLines);
                _protoItem->render(screenSize, itemOffset, pass);
            }
        }

        // Round the accumulated authored stride, not every row separately.
        // At fractional layout factors, truncating height and padding for each
        // row makes the error grow down the list relative to the scaled art.
        itemY += getItemPitch(item);
    }
}

void ListBox::renderScrollBar(const glm::ivec2 &screenSize, const glm::ivec2 &offset, IRenderPass &pass) {
    if (_scrollBar && _scrollBarEnabled) {
        updateScrollBarState();
        _scrollBar->render(screenSize, offset, pass);
    }
}

void ListBox::stretch(float x, float y, int mask) {
    Control::stretch(x, y, mask);

    float listLayoutScale = x == y ? x : 1.0f;
    _layoutScale = listLayoutScale * _gui.listScale();

    if (_protoItem) {
        // The viewport and row width remain in the normal GUI coordinate
        // system. Row height, icon art, frame slices, and pitch form a
        // separate density unit so a smaller value exposes more items without
        // shrinking the surrounding panel. Text stays at the GUI text scale
        // so dense rows do not also make it smaller.
        _protoItem->stretch(x, y, mask & ~kStretchHeight);
        if (mask & kStretchHeight) {
            auto extent = _protoItem->extent();
            extent.height = static_cast<int>(_protoItem->authoredExtent().height * _layoutScale);
            _protoItem->setExtent(std::move(extent));
        }
        _protoItem->setPresentationScale(_layoutScale, listLayoutScale);
    }
    if (_scrollBar) {
        if (x == y) {
            _scrollBar->stretch(x, y, mask);
            // The bar serves rows drawn at the density unit, so its width
            // follows that unit too; at the full layout factor it comes out
            // twice the authored bar-to-row proportion once rows compress.
            if (mask & kStretchWidth) {
                auto extent = _scrollBar->extent();
                extent.width = static_cast<int>(_scrollBar->authoredExtent().width * _layoutScale);
                _scrollBar->setExtent(std::move(extent));
            }
        } else {
            _scrollBar->stretch(x, y, mask & ~kStretchWidth);
        }
        if (_border && _border->dimension > 0) {
            insetScrollBar(*_scrollBar, _extent, _border->dimension, _leftScrollBar, x);
        }
    }
    updateItemsLayout();
}

void ListBox::insetScrollBar(Control &scrollBar,
                             const Extent &panel,
                             int borderDimension,
                             bool leftSide,
                             float layoutScale) {
    auto extent = scrollBar.extent();
    int inset = std::max(1, static_cast<int>(std::lround(kScrollBarEdgeInset * layoutScale)));
    extent.left = leftSide
                      ? panel.left + inset
                      : panel.left + panel.width - inset - extent.width;
    extent.top = panel.top + borderDimension;
    extent.height = panel.height - 2 * borderDimension;
    scrollBar.setExtent(std::move(extent));
}

void ListBox::setExtent(Extent extent) {
    Control::setExtent(extent);
    updateItemsLayout();
}

void ListBox::setExtentHeight(int height) {
    Control::setExtentHeight(height);
    updateItemSlots();
}

void ListBox::setSelectionMode(SelectionMode mode) {
    _selectionMode = mode;
}

void ListBox::setSelectedItemIndex(int index) {
    selectItem(index >= 0 && index < getItemCount() ? index : -1);
    if (_selectedItemIndex >= 0) {
        scrollToItem(_selectedItemIndex);
    }
}

bool ListBox::selectAdjacentItem(bool next, bool repeat) {
    if (!repeat) {
        _gui.setKeyRepeatStopped(false);
    } else if (_gui.keyRepeatStopped()) {
        return false;
    }
    const int count = getItemCount();
    if (count == 0) {
        return false;
    }
    int index = 0;
    if (_selectedItemIndex >= 0) {
        index = _selectedItemIndex + (next ? 1 : -1);
        if (index < 0 || index >= count) {
            if (!_looping) {
                return false;
            }
            index = index < 0 ? count - 1 : 0;
            _gui.setKeyRepeatStopped(true);
        }
    }
    selectItem(index);
    scrollToItem(index);
    return true;
}

bool ListBox::stepSelection(bool next, bool repeat) {
    if (!_itemsInteractive) {
        const int offset = std::clamp(_itemOffset + (next ? 1 : -1), 0, std::max(getScrollPositions() - 1, 0));
        if (offset == _itemOffset) {
            return false;
        }
        _itemOffset = offset;
        updateItemSlots();
        return true;
    }
    if (!selectAdjacentItem(next, repeat)) {
        return false;
    }
    if (_selectionMode == SelectionMode::OnClick && _onItemClick) {
        _onItemClick(_items[_selectedItemIndex].tag);
    }
    return true;
}

bool ListBox::activateSelectedItem() {
    return handleClick(0, 0, _selectionMode == SelectionMode::OnClick ? 2 : 1);
}

void ListBox::takeKeyboard() {
    _selectionUnhilighted = false;
    if (!_itemsInteractive || _selectedItemIndex >= 0 || _items.empty()) {
        return;
    }
    selectItem(0);
    scrollToItem(0);
    if (_selectionMode == SelectionMode::OnClick && _onItemClick) {
        _onItemClick(_items.front().tag);
    }
}

void ListBox::loseKeyboard() {
    _selectionUnhilighted = true;
}

void ListBox::setItemsInteractive(bool interactive) {
    _itemsInteractive = interactive;
    if (interactive) {
        _scrollOnly = false;
    } else {
        clearSelection();
    }
}

void ListBox::setScrollOnly(bool scrollOnly) {
    setItemsInteractive(!scrollOnly);
    _scrollOnly = scrollOnly;
}

void ListBox::setScrollBarEnabled(bool enabled) {
    _scrollBarEnabled = enabled;
    updateItemSlots();
}

void ListBox::shareProtoItem(const ListBox &list) {
    if (!list._protoItem) {
        return;
    }
    _protoItem = list._protoItem;
    updateItemsLayout();
}

void ListBox::setProtoMatchContent(bool match) {
    _protoMatchContent = match;
    updateItemsLayout();
}

// The block is one label with all the lines, the padding in from the room's
// left. One that fits sits the padding below the room's top, in the first of
// the rows of its own height and the padding that the room holds, stretched
// by its share of the spare room, a pixel more while any is left over. One
// taller than the room sits the padding below its top less a line advance
// for each position scrolled, and at the last position the padding above its
// bottom. Without a font there is nothing to draw.
void ListBox::renderTextBlock(const glm::ivec2 &screenSize, const glm::ivec2 &offset, IRenderPass &pass) {
    if (!_protoItem->text().font) {
        return;
    }
    const Extent inner = getRowsArea();
    const int content = getTextBlockHeight();
    const int padding = static_cast<int>(_padding * _layoutScale);
    int top = padding;
    int height = content;
    if (_overflowing) {
        if (_itemOffset == getTextBlockPositions() - 1) {
            top = inner.height - content - padding;
        } else {
            top = padding - static_cast<int>(static_cast<float>(_itemOffset) * _protoItem->textLineAdvance());
        }
    } else if (content != 0) {
        const int pitch = content + padding;
        const int rows = inner.height / pitch;
        const int spare = inner.height - padding - rows * pitch;
        height = content + spare / rows + (spare % rows > 0 ? 1 : 0);
    }
    const glm::ivec4 clip {offset.x + inner.left, offset.y + inner.top, inner.width, inner.height};
    withScissorRect(_graphicsSvc.context, screenSize.y, clip, [&]() {
        glm::ivec2 blockOffset(offset);
        blockOffset.x += inner.left + padding - _protoItem->extent().left;
        blockOffset.y += inner.top + top - _protoItem->extent().top;
        const int protoHeight = _protoItem->extent().height;
        _protoItem->setExtentHeight(height);
        _protoItem->setSelected(false);
        _protoItem->setTextLines(_textBlockLines);
        _protoItem->render(screenSize, blockOffset, pass);
        _protoItem->setExtentHeight(protoHeight);
    });
}

void ListBox::setRenderItemIconsForButtonProto(bool render) {
    _renderItemIconsForButtonProto = render;
    updateItemsLayout();
}

ImageButton *ListBox::imageButtonProto() const {
    return _protoItem && _protoItem->type() == ControlType::ImageButton ? static_cast<ImageButton *>(_protoItem.get()) : nullptr;
}

void ListBox::setIconTextFont(std::shared_ptr<Font> font) {
    // An image-button prototype draws the icon text itself.
    if (auto imageButton = imageButtonProto()) {
        imageButton->setIconFont(std::move(font));
    } else {
        _iconTextFont = std::move(font);
    }
}

void ListBox::setItemOffset(int offset) {
    if (_items.size()) {
        _itemOffset = std::min(offset, static_cast<int>(_items.size() - 1));
    }
}

void ListBox::scrollToItem(int index) {
    if (index < _itemOffset) {
        _itemOffset = index;
    }
    updateItemSlots();
    while (index >= _itemOffset + _slotCount && _itemOffset < index) {
        ++_itemOffset;
        updateItemSlots();
    }
}

void ListBox::scrollToBottom() {
    if (!_protoItem || _items.empty()) {
        _itemOffset = 0;
        updateItemSlots();
        return;
    }

    float height = 0.0f;
    size_t offset = _items.size();
    while (offset > 0) {
        const Item &item = _items[offset - 1];
        float itemHeight = getItemPitch(item);

        if (height > 0.0f &&
            static_cast<int>(std::lround(height + itemHeight)) > getInnerHeight()) {
            break;
        }
        height += itemHeight;
        --offset;
    }

    _itemOffset = static_cast<int>(offset);
    updateItemSlots();
}

void ListBox::updateItemsLayout() {
    if (!_protoItem) {
        return;
    }

    if (_protoMatchContent) {
        matchProtoToRows();
    }

    int textWidth = getItemTextWidth();
    const auto &font = _protoItem->text().font;
    for (auto &item : _items) {
        item._textLines = font ? breakText(item.text, *font, textWidth, _protoItem->scale()) : std::vector<std::string> {};
    }
    updateItemSlots();
}

int ListBox::getInnerHeight() const {
    return std::max(getRowsArea().height, 0);
}

Control::Extent ListBox::getRowsArea() const {
    const int scrollBarWidth = _scrollBar && _scrollBarEnabled ? _scrollBar->extent().width : 0;
    return innerExtent({_extent.left + (_leftScrollBar ? scrollBarWidth : 0),
                        _extent.top,
                        _extent.width - scrollBarWidth,
                        _extent.height});
}

Control::Extent ListBox::itemsViewport() const {
    if (!_protoItem) {
        return {};
    }
    const Extent inner = innerExtent(_extent);
    int top = _protoItem->extent().top;
    return {
        inner.left,
        top,
        inner.width,
        inner.top + inner.height - top};
}

int ListBox::visibleItemCount() const {
    return std::max(0, std::min(_slotCount, static_cast<int>(_items.size()) - _itemOffset));
}

std::vector<Control::Extent> ListBox::visibleItemExtents() const {
    std::vector<Extent> extents;
    if (!_protoItem) {
        return extents;
    }
    // Accumulate the fractional pitch and round each row once, so rows do
    // not creep away from the artwork behind them as the list gets longer.
    const Extent &proto = _protoItem->extent();
    const int count = visibleItemCount();
    extents.reserve(count);
    float y = 0.0f;
    for (int i = 0; i < count; ++i) {
        extents.emplace_back(proto.left, proto.top + static_cast<int>(std::lround(y)), proto.width, proto.height);
        y += getItemPitch(_items[i + _itemOffset]);
    }
    return extents;
}

float ListBox::getItemPitch(const Item &item) const {
    float padding = _padding * _layoutScale;
    if (_protoMatchContent) {
        return getItemHeight(item) + padding;
    }
    return _protoItem->authoredExtent().height * _layoutScale + padding;
}

int ListBox::getItemWidth() const {
    const int width = getRowsArea().width - 2 * static_cast<int>(_padding * _layoutScale);
    return std::max(width, 0);
}

void ListBox::matchProtoToRows() {
    const Extent rows = getRowsArea();
    const int padding = static_cast<int>(_padding * _layoutScale);
    auto extent = _protoItem->extent();
    extent.left = rows.left + padding;
    extent.top = rows.top + padding;
    extent.width = getItemWidth();
    _protoItem->setExtent(std::move(extent));
}

int ListBox::getItemHeight(const Item &item) const {
    if (!_protoItem) {
        return 0;
    }
    // Without a font the text has no height, so the row keeps the prototype's.
    const auto &font = _protoItem->text().font;
    if (!_protoMatchContent || !font) {
        return _protoItem->extent().height;
    }

    return _protoItem->textBlockHeight(static_cast<int>(item._textLines.size()));
}

int ListBox::getItemTextWidth() const {
    if (!_protoItem)
        return 0;

    int width = _protoItem->innerExtent(_protoItem->textExtent()).width;
    if (shouldRenderItemIconsForButtonProto()) {
        // Button-proto item icons render in a square gutter sized from the row height.
        int itemIconWidth = _protoItem->extent().height;
        width -= itemIconWidth;
        if (width < 0) {
            width = 0;
        }
    }
    return width;
}

bool ListBox::shouldRenderItemIconsForButtonProto() const {
    return _renderItemIconsForButtonProto &&
           (_protoItem->type() == ControlType::Button || _protoItem->type() == ControlType::ToggleButton);
}

void ListBox::renderItemWithButtonProtoIcon(
    const glm::ivec2 &screenSize,
    const glm::ivec2 &offset,
    const Item &item,
    IRenderPass &pass) {

    Control::Extent originalExtent(_protoItem->extent());
    int itemIconWidth = originalExtent.height;
    Control::Extent textExtent(originalExtent);
    textExtent.left += itemIconWidth;
    textExtent.width -= itemIconWidth;
    if (textExtent.width < 0) {
        textExtent.width = 0;
    }

    _protoItem->setExtent(textExtent);
    _protoItem->setTextLines(item._textLines);
    if (item.invalid) {
        _protoItem->setBorderColorOverride(kInvalidItemBorderColor);
        _protoItem->setUseBorderColorOverride(true);
    }
    _protoItem->render(screenSize, offset, pass);
    _protoItem->setExtent(originalExtent);

    if (!item.iconFrame && !item.iconTexture)
        return;

    glm::ivec2 iconPosition(offset.x + originalExtent.left, offset.y + originalExtent.top);
    glm::ivec2 iconSize(itemIconWidth, originalExtent.height);

    // The icon frame shows the colour the row's frames show, and their
    // opacity while the row is not selected.
    if (item.iconFrame) {
        const bool selected = _protoItem->isSelected();
        glm::vec3 frameColor(selected ? _protoItem->hilight().color : _protoItem->border().color);
        if (_protoItem->usesBorderColorOverride()) {
            frameColor = _protoItem->borderColorOverride();
        }
        float frameAlpha = _protoItem->border().alpha;
        if (selected) {
            frameAlpha = _iconFrameHilightPulsing ? _gui.pulsingAlpha() : 1.0f;
        }
        pass.drawImage(*item.iconFrame, iconPosition, iconSize, glm::vec4(frameColor, frameAlpha));
    }
    if (item.iconTexture) {
        pass.drawIcon(*item.iconTexture, iconPosition, iconSize);
    }
    const auto &iconTextFont = _iconTextFont ? _iconTextFont : _protoItem->text().font;
    if (!item.iconText.empty() && iconTextFont) {
        glm::vec3 position(0.0f);
        position.x = static_cast<float>(iconPosition.x + iconSize.x);
        position.y = static_cast<float>(iconPosition.y + iconSize.y - 0.5f * iconTextFont->height() * _protoItem->scale());
        iconTextFont->render(item.iconText, position, item.iconTextColor.value_or(_protoItem->text().color), TextGravity::LeftCenter, _protoItem->scale());
    }
}

const ListBox::Item &ListBox::getItemAt(int index) const {
    return _items[index];
}

int ListBox::getItemCount() const {
    return static_cast<int>(_items.size());
}

} // namespace gui

} // namespace reone
