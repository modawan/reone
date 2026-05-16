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

#include "reone/gui/control/iconchain.h"

#include "reone/scene/render/pass.h"

using namespace reone::scene;

namespace reone {

namespace gui {

static constexpr glm::vec3 kLockedItemColor {0.45f};
static constexpr glm::vec3 kSelectedItemColor {1.0f};

void IconChain::clearItems() {
    _items.clear();
    _rowOffset = 0;
    _focusedItemIndex = -1;
}

void IconChain::addItem(Item item) {
    _items.push_back(std::move(item));
}

void IconChain::setItemSelected(const std::string &tag, bool selected) {
    for (auto &item : _items) {
        if (item.tag == tag) {
            item.selected = selected;
            return;
        }
    }
}

bool IconChain::handleMouseMotion(int x, int y) {
    int itemIdx = getItemIndex(x, y);
    if (itemIdx == _focusedItemIndex) {
        return false;
    }

    _focusedItemIndex = itemIdx;
    if (_focusedItemIndex != -1 && _onItemFocus) {
        _onItemFocus(_items[_focusedItemIndex].tag);
    }
    return false;
}

bool IconChain::handleMouseWheel(int x, int y) {
    int maxRowOffset = std::max(0, getMaxRow() - getVisibleRowCount() + 1);
    if (y < 0 && _rowOffset < maxRowOffset) {
        ++_rowOffset;
        _focusedItemIndex = -1;
        return true;
    }
    if (y > 0 && _rowOffset > 0) {
        --_rowOffset;
        _focusedItemIndex = -1;
        return true;
    }
    return false;
}

bool IconChain::handleClick(int x, int y, int clicks) {
    int itemIdx = getItemIndex(x, y);
    if (itemIdx == -1) {
        return false;
    }

    _focusedItemIndex = itemIdx;
    if (_onItemClick) {
        _onItemClick(_items[itemIdx].tag);
    }
    return true;
}

void IconChain::render(const glm::ivec2 &screenSize,
                       const glm::ivec2 &offset,
                       IRenderPass &pass) {
    if (!_visible) {
        return;
    }

    Control::render(screenSize, offset, pass);

    for (size_t i = 0; i < _items.size(); ++i) {
        const Item &item = _items[i];
        if (!hasValidPosition(item)) {
            continue;
        }

        Extent itemExtent(getItemExtent(item));
        if (!isItemVisible(itemExtent)) {
            continue;
        }

        bool focused = static_cast<int>(i) == _focusedItemIndex;
        renderItemBorder(item, focused, itemExtent, offset, pass);

        if (item.iconTexture) {
            pass.drawImage(
                *item.iconTexture,
                {offset.x + itemExtent.left, offset.y + itemExtent.top},
                {itemExtent.width, itemExtent.height},
                getItemIconColor(item));
        }
    }
}

void IconChain::setSelected(bool selected) {
    Control::setSelected(selected);
    if (!selected) {
        _focusedItemIndex = -1;
    }
}

void IconChain::setColumnCount(int count) {
    _columnCount = count > 0 ? count : 0;
}

void IconChain::setCellSize(int size) {
    if (size > 0) {
        _cellSize = size;
    }
}

void IconChain::setCellSpacing(int x, int y) {
    _cellSpacing.x = x;
    _cellSpacing.y = y;
}

bool IconChain::hasValidPosition(const Item &item) const {
    if (_cellSize <= 0 || item.row < _rowOffset || item.column < 0) {
        return false;
    }
    return _columnCount == 0 || item.column < _columnCount;
}

bool IconChain::isItemVisible(const Extent &extent) const {
    int right = extent.left + extent.width;
    int bottom = extent.top + extent.height;
    int controlRight = _extent.left + _extent.width;
    int controlBottom = _extent.top + _extent.height;
    return extent.left >= _extent.left &&
           extent.top >= _extent.top &&
           right <= controlRight &&
           bottom <= controlBottom;
}

int IconChain::getVisibleRowCount() const {
    if (_cellSize <= 0) {
        return 0;
    }
    int availableHeight = _extent.height - 2 * _padding;
    return std::max(1, (availableHeight + _cellSpacing.y) / (_cellSize + _cellSpacing.y));
}

int IconChain::getMaxRow() const {
    int maxRow = -1;
    for (auto &item : _items) {
        if (item.column >= 0 && (_columnCount == 0 || item.column < _columnCount)) {
            maxRow = std::max(maxRow, item.row);
        }
    }
    return maxRow;
}

int IconChain::getItemIndex(int x, int y) const {
    for (size_t i = 0; i < _items.size(); ++i) {
        const Item &item = _items[i];
        if (!hasValidPosition(item)) {
            continue;
        }

        Extent itemExtent(getItemExtent(item));
        if (isItemVisible(itemExtent) && itemExtent.contains(x, y)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Control::Extent IconChain::getItemExtent(const Item &item) const {
    Extent extent;
    extent.left = _extent.left + _padding + item.column * (_cellSize + _cellSpacing.x);
    extent.top = _extent.top + _padding + (item.row - _rowOffset) * (_cellSize + _cellSpacing.y);
    extent.width = _cellSize;
    extent.height = _cellSize;
    return extent;
}

glm::vec3 IconChain::getItemBorderColor(const Item &item, bool focused) const {
    if (item.selected) {
        return kSelectedItemColor;
    }
    if (focused && _hilight) {
        return _hilight->color;
    }
    if (item.state == State::Locked) {
        return kLockedItemColor;
    }
    if (item.state == State::Selectable && _hilight) {
        return _hilight->color;
    }
    return _border ? _border->color : glm::vec3(1.0f);
}

glm::vec4 IconChain::getItemIconColor(const Item &item) const {
    if (item.state == State::Locked) {
        return glm::vec4(kLockedItemColor, 1.0f);
    }
    return glm::vec4(1.0f);
}

void IconChain::renderItemBorder(
    const Item &item,
    bool focused,
    const Extent &extent,
    const glm::ivec2 &offset,
    IRenderPass &pass) {

    const std::shared_ptr<Border> &border = (focused && _hilight) ? _hilight : _border;
    if (!border) {
        return;
    }

    Extent originalExtent(_extent);
    _extent = extent;
    setBorderColorOverride(getItemBorderColor(item, focused));
    setUseBorderColorOverride(true);
    renderBorder(*border, offset, {extent.width, extent.height}, pass);
    setUseBorderColorOverride(false);
    _extent = originalExtent;
}

} // namespace gui

} // namespace reone
