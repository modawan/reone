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

#include "reone/graphics/font.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/textutil.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/imagebutton.h"
#include "reone/gui/control/scrollbar.h"
#include "reone/gui/gui.h"
#include "reone/resource/gff.h"
#include "reone/resource/resources.h"
#include "reone/scene/render/pass.h"
#include "reone/system/logutil.h"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace gui {

static constexpr glm::vec3 kInvalidItemBorderColor {1.0f, 0.0f, 0.0f};

void ListBox::clearItems() {
    _items.clear();
    _itemOffset = 0;
    _selectedItemIndex = -1;
}

void ListBox::addItem(Item &&item) {
    if (!_protoItem)
        return;

    item._textLines = breakText(item.text, *_protoItem->text().font, getItemTextWidth(), _protoItem->scale());
    _items.push_back(item);

    updateItemSlots();
}

void ListBox::addTextLinesAsItems(const std::string &text) {
    if (!_protoItem)
        return;

    std::istringstream stream(text);
    std::string logicalLine;
    while (std::getline(stream, logicalLine)) {
        std::vector<std::string> lines;
        if (logicalLine.empty()) {
            lines.push_back("");
        } else {
            lines = breakText(logicalLine, *_protoItem->text().font, getItemTextWidth(), _protoItem->scale());
        }

        for (auto &line : lines) {
            Item item;
            item.text = line;
            item._textLines = std::vector<std::string> {line};
            _items.push_back(std::move(item));
        }
    }

    if (!text.empty() && text.back() == '\n') {
        Item item;
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
    }

}

bool ListBox::handleMouseMotion(int x, int y) {
    if (_itemsInteractive && _selectionMode == SelectionMode::OnHover) {
        _selectedItemIndex = getItemIndex(y);
    }
    return false;
}

int ListBox::getItemIndex(int y) const {
    if (!_protoItem)
        return -1;

    const Control::Extent &protoExtent = _protoItem->extent();
    if (protoExtent.height == 0)
        return -1;

    float itemy = static_cast<float>(protoExtent.top);
    if (y < itemy)
        return -1;

    for (size_t i = _itemOffset; i < _items.size(); ++i) {
        const Item &item = _items[i];
        itemy += getItemPitch(item);
        if (y < static_cast<int>(std::lround(itemy)))
            return static_cast<int>(i);
    }

    return -1;
}

bool ListBox::handleMouseWheel(int x, int y) {
    if (y < 0) {
        if (_items.size() - _itemOffset > _slotCount) {
            _itemOffset++;
            updateItemSlots();
        }
        return true;
    } else if (y > 0) {
        if (_itemOffset > 0) {
            _itemOffset--;
            updateItemSlots();
        }
        return true;
    }

    return false;
}

void ListBox::updateItemSlots() {
    _slotCount = 0;

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

    if (_scrollBar) {
        _scrollBar->setVisible(_scrollBarEnabled && _items.size() > _slotCount);
    }
}

bool ListBox::handleClick(int x, int y, int clicks) {
    if (!_itemsInteractive)
        return false;

    int itemIdx = getItemIndex(y);
    if (itemIdx == -1)
        return false;

    if (_selectionMode == SelectionMode::OnClick) {
        _selectedItemIndex = itemIdx;
    }
    if (_onItemClick) {
        _onItemClick(_items[itemIdx].tag);
    }
    if (clicks > 1 && _onItemDoubleClick) {
        _onItemDoubleClick(_items[itemIdx].tag);
    }

    return true;
}

void ListBox::render(const glm::ivec2 &screenSize,
                     const glm::ivec2 &offset,
                     IRenderPass &pass) {
    if (!_visible)
        return;

    if (_backgroundRenderer) {
        _backgroundRenderer(*this, offset, pass);
    }
    Control::render(screenSize, offset, pass);

    if (!_protoItem)
        return;

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
        _protoItem->setSelected(_selectedItemIndex == itemIdx);

        glm::vec3 originalTextColor(_protoItem->text().color);
        if (item.textColor) {
            _protoItem->setTextColor(*item.textColor);
        }

        auto imageButton = std::dynamic_pointer_cast<ImageButton>(_protoItem);
        if (imageButton) {
            imageButton->render(itemOffset, item._textLines, item.iconText, item.iconTexture, item.iconFrame, pass);
        } else if (shouldRenderItemIconsForButtonProto()) {
            renderItemWithButtonProtoIcon(screenSize, itemOffset, item, pass);
        } else {
            _protoItem->setTextLines(item._textLines);
            _protoItem->render(screenSize, itemOffset, pass);
        }

        _protoItem->setTextColor(originalTextColor);

        // Round the accumulated authored stride, not every row separately.
        // At fractional layout factors, truncating height and padding for each
        // row makes the error grow down the list relative to the scaled art.
        itemY += getItemPitch(item);
    }

    if (_scrollBar && _scrollBarEnabled) {
        ScrollBar::ScrollState state;
        state.count = static_cast<int>(_items.size());
        state.numVisible = this->_slotCount;
        state.offset = _itemOffset;
        auto &scrollBar = static_cast<ScrollBar &>(*_scrollBar);
        scrollBar.setScrollState(std::move(state));
        scrollBar.render(screenSize, offset, pass);
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
        } else {
            _scrollBar->stretch(x, y, mask & ~kStretchWidth);
        }
    }
    updateItemsLayout();
}

void ListBox::setSelected(bool selected) {
    Control::setSelected(selected);
    if (!selected && _selectionMode == SelectionMode::OnHover) {
        _selectedItemIndex = -1;
    }
}

void ListBox::setExtent(Extent extent) {
    Control::setExtent(extent);
    updateItemsLayout();
}

void ListBox::setExtentHeight(int height) {
    Control::setExtentHeight(height);
    updateItemSlots();
}

void ListBox::changeProtoItemType(ControlType type) {
    // TODO: debug CTD
    return;

    if (!_protoItem) {
        return;
    }
    auto &oldProtoItem = *_protoItem;

    _protoItem = _gui.newControl(type, oldProtoItem.tag());
    _protoItem->setId(oldProtoItem.id());
    _protoItem->setPadding(oldProtoItem.padding());
    _protoItem->setExtent(oldProtoItem.extent());
    _protoItem->setBorder(oldProtoItem.border());
    _protoItem->setText(oldProtoItem.text());
    _protoItem->setHilight(oldProtoItem.hilight());

    _protoItem->updateTextLines();
    _protoItem->updateTransform();
}

void ListBox::setSelectionMode(SelectionMode mode) {
    _selectionMode = mode;
}

void ListBox::setSelectedItemIndex(int index) {
    _selectedItemIndex = index >= 0 && index < getItemCount() ? index : -1;
}

void ListBox::setItemsInteractive(bool interactive) {
    _itemsInteractive = interactive;
    if (!interactive) {
        clearSelection();
    }
}

void ListBox::setScrollBarEnabled(bool enabled) {
    _scrollBarEnabled = enabled;
    updateItemSlots();
}

void ListBox::setProtoMatchContent(bool match) {
    _protoMatchContent = match;
    updateItemsLayout();
}

void ListBox::setRenderItemIconsForButtonProto(bool render) {
    _renderItemIconsForButtonProto = render;
    updateItemsLayout();
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
        auto extent = _protoItem->extent();
        extent.width = getItemWidth();
        _protoItem->setExtent(std::move(extent));
    }

    int textWidth = getItemTextWidth();
    for (auto &item : _items) {
        item._textLines = breakText(item.text, *_protoItem->text().font, textWidth, _protoItem->scale());
    }
    updateItemSlots();
}

int ListBox::getInnerHeight() const {
    int height = _extent.height;
    if (_border) {
        height -= 2 * _border->dimension;
    }
    return std::max(height, 0);
}

Control::Extent ListBox::itemsViewport() const {
    if (!_protoItem) {
        return {};
    }
    int border = _border ? _border->dimension : 0;
    int top = _protoItem->extent().top;
    return {
        _extent.left + border,
        top,
        _extent.width - 2 * border,
        _extent.top + _extent.height - border - top};
}

int ListBox::visibleItemCount() const {
    return std::max(0, std::min(_slotCount, static_cast<int>(_items.size()) - _itemOffset));
}

Control::Extent ListBox::visibleItemExtent(int index) const {
    if (!_protoItem) {
        return {};
    }
    // Accumulate the fractional pitch and round once, so rows do not creep
    // away from the artwork behind them as the list gets longer.
    float y = 0.0f;
    for (int i = 0; i < index && i + _itemOffset < static_cast<int>(_items.size()); ++i) {
        y += getItemPitch(_items[i + _itemOffset]);
    }
    const Extent &proto = _protoItem->extent();
    return {
        proto.left,
        proto.top + static_cast<int>(std::lround(y)),
        proto.width,
        proto.height};
}

float ListBox::getItemPitch(const Item &item) const {
    float padding = _padding * _layoutScale;
    if (_protoMatchContent) {
        return getItemHeight(item) + padding;
    }
    return _protoItem->authoredExtent().height * _layoutScale + padding;
}

int ListBox::getItemWidth() const {
    int width = _extent.width;
    if (_scrollBar && _scrollBarEnabled) {
        width -= _scrollBar->extent().width;
    }
    if (_border) {
        width -= 2 * _border->dimension;
    }
    width -= 2 * static_cast<int>(_padding * _layoutScale);
    return std::max(width, 0);
}

int ListBox::getItemHeight(const Item &item) const {
    if (!_protoItem) {
        return 0;
    }
    if (!_protoMatchContent) {
        return _protoItem->extent().height;
    }

    float fontHeight = _protoItem->text().font->height() * _protoItem->scale();
    int textHeight = static_cast<int>(item._textLines.size() * fontHeight + 0.5f);
    if (static_cast<int>(fontHeight + 0.5f) <= 15) {
        ++textHeight;
    }
    return textHeight + 2 * _protoItem->border().dimension;
}

int ListBox::getItemTextWidth() const {
    if (!_protoItem)
        return 0;

    int width = _protoItem->extent().width -
                2 * _protoItem->border().dimension;
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
    return _renderItemIconsForButtonProto && static_cast<bool>(std::dynamic_pointer_cast<Button>(_protoItem));
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
    _protoItem->setBorderColorOverride(kInvalidItemBorderColor);
    _protoItem->setUseBorderColorOverride(item.invalid);
    _protoItem->render(screenSize, offset, pass);
    _protoItem->setUseBorderColorOverride(false);
    _protoItem->setExtent(originalExtent);

    if (!item.iconFrame && !item.iconTexture)
        return;

    glm::ivec2 iconPosition(offset.x + originalExtent.left, offset.y + originalExtent.top);
    glm::ivec2 iconSize(itemIconWidth, originalExtent.height);

    if (item.iconFrame) {
        glm::vec3 frameColor(_protoItem->isSelected() ? _protoItem->hilight().color : _protoItem->border().color);
        if (item.invalid) {
            frameColor = kInvalidItemBorderColor;
        }
        pass.drawImage(*item.iconFrame, iconPosition, iconSize, glm::vec4(frameColor, 1.0f));
    }
    if (item.iconTexture) {
        pass.drawIcon(*item.iconTexture, iconPosition, iconSize);
    }
    if (!item.iconText.empty() && _protoItem->text().font) {
        glm::vec3 position(0.0f);
        position.x = static_cast<float>(iconPosition.x + iconSize.x);
        position.y = static_cast<float>(iconPosition.y + iconSize.y - 0.5f * _protoItem->text().font->height() * _protoItem->scale());
        _protoItem->text().font->render(item.iconText, position, _protoItem->text().color, TextGravity::LeftCenter, _protoItem->scale());
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
