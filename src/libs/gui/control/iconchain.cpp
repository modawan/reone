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

static constexpr glm::vec3 kDarkItemColor {0.45f};
static constexpr glm::vec3 kSelectedItemColor {1.0f};
static constexpr float kFocusedBorderPulseCyclesPerSecond = 1.0f;
static constexpr float kFocusedBorderPulseMinFactor = 0.55f;

void IconChain::clearItems() {
    _items.clear();
    _links.clear();
    _rowOffset = 0;
    clearFocusedItem();
}

void IconChain::addItem(Item item) {
    _items.push_back(std::move(item));
}

void IconChain::clearLinks() {
    _links.clear();
}

void IconChain::addLink(Link link) {
    _links.push_back(std::move(link));
}

void IconChain::setItemSelected(const std::string &tag, bool selected) {
    for (auto &item : _items) {
        if (item.tag == tag) {
            item.selected = selected;
            return;
        }
    }
}

void IconChain::setItemState(const std::string &tag, State state) {
    for (auto &item : _items) {
        if (item.tag == tag) {
            item.state = state;
            return;
        }
    }
}

void IconChain::focusItem(const std::string &tag) {
    auto maybeItem = std::find_if(
        _items.begin(), _items.end(),
        [&tag](const Item &item) { return item.tag == tag; });
    if (maybeItem == _items.end()) {
        return;
    }

    _focusedItemIndex = static_cast<int>(std::distance(_items.begin(), maybeItem));
    if (_onItemFocus) {
        _onItemFocus(maybeItem->tag);
    }
}

const IconChain::Item *IconChain::focusedItem() const {
    if (_focusedItemIndex < 0 || _focusedItemIndex >= static_cast<int>(_items.size())) {
        return nullptr;
    }
    return &_items[_focusedItemIndex];
}

bool IconChain::handleMouseMotion(int x, int y) {
    return false;
}

bool IconChain::handleMouseWheel(int x, int y) {
    int maxRowOffset = std::max(0, getMaxRow() - getVisibleRowCount() + 1);
    if (y < 0 && _rowOffset < maxRowOffset) {
        ++_rowOffset;
        clearFocusedItem();
        return true;
    }
    if (y > 0 && _rowOffset > 0) {
        --_rowOffset;
        clearFocusedItem();
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
    if (_onItemFocus) {
        _onItemFocus(_items[itemIdx].tag);
    }
    if (clicks > 1 && _onItemDoubleClick) {
        _onItemDoubleClick(_items[itemIdx].tag);
    }
    return true;
}

void IconChain::update(float dt) {
    Control::update(dt);
    _focusedBorderPulsePhase += dt * kFocusedBorderPulseCyclesPerSecond;
    _focusedBorderPulsePhase -= std::floor(_focusedBorderPulsePhase);
}

void IconChain::render(const glm::ivec2 &screenSize,
                       const glm::ivec2 &offset,
                       IRenderPass &pass) {
    if (!_visible) {
        return;
    }

    Control::render(screenSize, offset, pass);

    for (auto &item : _items) {
        if (!hasValidPosition(item)) {
            continue;
        }

        Extent itemExtent(getItemExtent(item));
        if (!isItemVisible(itemExtent)) {
            continue;
        }

        if (_cellStyle.backgroundTexture) {
            pass.drawImage(
                *_cellStyle.backgroundTexture,
                {offset.x + itemExtent.left, offset.y + itemExtent.top},
                {itemExtent.width, itemExtent.height},
                getCellBackgroundColor(item));
        }
    }

    for (auto &link : _links) {
        renderLink(link, offset, pass);
    }

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
        if (item.iconTexture) {
            Extent iconExtent(getItemIconExtent(itemExtent));
            pass.drawImage(
                *item.iconTexture,
                {offset.x + iconExtent.left, offset.y + iconExtent.top},
                {iconExtent.width, iconExtent.height},
                getItemIconColor(item));
        }

        renderItemBorder(item, focused, itemExtent, offset, pass);
        renderFocusedBorder(item, focused, itemExtent, offset, pass);
    }
}

void IconChain::setSelected(bool selected) {
    Control::setSelected(selected);
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

void IconChain::setCellOrigin(int x, int y) {
    _cellOrigin.x = x;
    _cellOrigin.y = y;
}

void IconChain::setCellStep(int x, int y) {
    _cellStep.x = x;
    _cellStep.y = y;
}

void IconChain::setRowOffsets(std::vector<int> offsets) {
    _rowOffsets = std::move(offsets);
}

void IconChain::setCellStyle(CellStyle style) {
    _cellStyle = std::move(style);
}

bool IconChain::hasValidPosition(const Item &item) const {
    if (_cellSize <= 0 || item.row < _rowOffset || item.column < 0) {
        return false;
    }
    if (!_rowOffsets.empty() &&
        item.row - _rowOffset >= static_cast<int>(_rowOffsets.size())) {
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
    if (!_rowOffsets.empty()) {
        return static_cast<int>(_rowOffsets.size());
    }

    int rowStep = getCellStepY();
    if (_cellSize <= 0 || rowStep <= 0) {
        return 0;
    }
    int availableHeight = _extent.height - getCellOriginY();
    if (availableHeight < _cellSize) {
        return 0;
    }
    return 1 + (availableHeight - _cellSize) / rowStep;
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

const IconChain::Item *IconChain::findItem(const std::string &tag) const {
    auto maybeItem = std::find_if(
        _items.begin(), _items.end(),
        [&tag](auto &item) { return item.tag == tag; });
    return maybeItem != _items.end() ? &*maybeItem : nullptr;
}

void IconChain::clearFocusedItem() {
    if (_focusedItemIndex == -1) {
        return;
    }

    _focusedItemIndex = -1;
    if (_onItemFocusCleared) {
        _onItemFocusCleared();
    }
}

Control::Extent IconChain::getItemExtent(const Item &item) const {
    Extent extent;
    extent.left = _extent.left + getCellOriginX() + item.column * getCellStepX();
    int visibleRow = item.row - _rowOffset;
    extent.top = _extent.top + (_rowOffsets.empty()
                                     ? getCellOriginY() + visibleRow * getCellStepY()
                                     : _rowOffsets[visibleRow]);
    extent.width = _cellSize;
    extent.height = _cellSize;
    return extent;
}

Control::Extent IconChain::getItemIconExtent(const Extent &itemExtent) const {
    if (_cellStyle.iconSize <= 0 || _cellStyle.iconSize >= _cellSize) {
        return itemExtent;
    }

    Extent extent(itemExtent);
    extent.width = _cellStyle.iconSize;
    extent.height = _cellStyle.iconSize;
    extent.left += (_cellSize - extent.width) / 2;
    extent.top += (_cellSize - extent.height) / 2;
    return extent;
}

glm::vec3 IconChain::getItemBorderColor(const Item &item, bool focused) const {
    if (_cellStyle.borderColors) {
        if (item.selected) {
            return _cellStyle.borderColors->selected;
        }
        switch (item.state) {
        case State::Owned:
            return _cellStyle.borderColors->owned;
        case State::Selectable:
            return _cellStyle.borderColors->selectable;
        case State::Locked:
            return _cellStyle.borderColors->locked;
        }
    }
    if (item.selected) {
        return kSelectedItemColor;
    }
    if (focused && _hilight) {
        return _hilight->color;
    }
    if (item.state == State::Locked) {
        return kDarkItemColor;
    }
    if (item.state == State::Selectable && _hilight) {
        return _hilight->color;
    }
    return _border ? _border->color : glm::vec3(1.0f);
}

glm::vec4 IconChain::getItemIconColor(const Item &item) const {
    if (!isItemBright(item)) {
        return glm::vec4(kDarkItemColor, 1.0f);
    }
    return glm::vec4(1.0f);
}

float IconChain::getFocusedBorderPulseFactor() const {
    float wave = 0.5f - 0.5f * std::cos(glm::two_pi<float>() * _focusedBorderPulsePhase);
    return glm::mix(kFocusedBorderPulseMinFactor, 1.0f, wave);
}

void IconChain::renderLink(
    const Link &link,
    const glm::ivec2 &offset,
    IRenderPass &pass) const {

    if (!_cellStyle.linkTexture) {
        return;
    }

    auto source = findItem(link.sourceTag);
    auto target = findItem(link.targetTag);
    if (!source || !target ||
        source->row != target->row ||
        target->column != source->column + 1 ||
        !hasValidPosition(*source) ||
        !hasValidPosition(*target)) {
        return;
    }

    Extent sourceExtent(getItemExtent(*source));
    Extent targetExtent(getItemExtent(*target));
    if (!isItemVisible(sourceExtent) || !isItemVisible(targetExtent)) {
        return;
    }

    glm::ivec2 linkSize = _cellStyle.linkSize;
    if (linkSize.x <= 0) {
        linkSize.x = _cellStyle.linkTexture->width();
    }
    if (linkSize.y <= 0) {
        linkSize.y = _cellStyle.linkTexture->height();
    }

    int sourceRight = sourceExtent.left + sourceExtent.width;
    int gapWidth = targetExtent.left - sourceRight;
    int linkLeft = sourceRight + (gapWidth - linkSize.x) / 2;
    int linkTop = sourceExtent.top + (sourceExtent.height - linkSize.y) / 2;
    pass.drawImage(
        *_cellStyle.linkTexture,
        {offset.x + linkLeft, offset.y + linkTop},
        linkSize);
}

std::optional<glm::vec3> IconChain::getFocusedBorderColor(const Item &item, bool focused) const {
    if (!focused || !_cellStyle.focusedBorderColors) {
        return std::nullopt;
    }

    if (item.selected) {
        return _cellStyle.focusedBorderColors->selected;
    }

    switch (item.state) {
    case State::Owned:
        return _cellStyle.focusedBorderColors->owned;
    case State::Selectable:
        return _cellStyle.focusedBorderColors->selectable;
    case State::Locked:
        return _cellStyle.focusedBorderColors->locked;
    }

    return std::nullopt;
}

bool IconChain::isItemBright(const Item &item) const {
    return item.state == State::Owned || item.selected;
}

glm::vec4 IconChain::getCellBackgroundColor(const Item &item) const {
    if (_cellStyle.dimLockedBackground && item.state == State::Locked && !isItemBright(item)) {
        return glm::vec4(kDarkItemColor, 1.0f);
    }
    return glm::vec4(1.0f);
}

int IconChain::getCellStepX() const {
    return _cellStep.x > 0 ? _cellStep.x : _cellSize + _cellSpacing.x;
}

int IconChain::getCellStepY() const {
    return _cellStep.y > 0 ? _cellStep.y : _cellSize + _cellSpacing.y;
}

int IconChain::getCellOriginX() const {
    return _cellOrigin.x >= 0 ? _cellOrigin.x : _padding;
}

int IconChain::getCellOriginY() const {
    return _cellOrigin.y >= 0 ? _cellOrigin.y : _padding;
}

void IconChain::renderItemBorder(
    const Item &item,
    bool focused,
    const Extent &extent,
    const glm::ivec2 &offset,
    IRenderPass &pass) {

    if (_cellStyle.onlyDrawItemBorderWhenBright && !isItemBright(item)) {
        return;
    }

    const std::shared_ptr<Border> &border = _cellStyle.itemBorder
                                               ? _cellStyle.itemBorder
                                               : (focused && _hilight) ? _hilight : _border;
    if (!border) {
        return;
    }

    Extent originalExtent(_extent);
    _extent = extent;
    setBorderColorOverride(getItemBorderColor(item, focused));
    setUseBorderColorOverride(true);
    if (_cellStyle.drawItemBorderFill || !border->fill) {
        renderBorder(*border, offset, {extent.width, extent.height}, pass);
    } else {
        Border borderWithoutFill(*border);
        borderWithoutFill.fill.reset();
        renderBorder(borderWithoutFill, offset, {extent.width, extent.height}, pass);
    }
    setUseBorderColorOverride(false);
    _extent = originalExtent;
}

void IconChain::renderFocusedBorder(
    const Item &item,
    bool focused,
    const Extent &extent,
    const glm::ivec2 &offset,
    IRenderPass &pass) {

    auto maybeColor = getFocusedBorderColor(item, focused);
    if (!maybeColor) {
        return;
    }

    const std::shared_ptr<Border> &border = _cellStyle.itemBorder ? _cellStyle.itemBorder : _border;
    if (!border) {
        return;
    }

    Extent originalExtent(_extent);
    _extent = extent;
    setBorderColorOverride(*maybeColor * getFocusedBorderPulseFactor());
    setUseBorderColorOverride(true);
    if (_cellStyle.drawItemBorderFill || !border->fill) {
        renderBorder(*border, offset, {extent.width, extent.height}, pass);
    } else {
        Border borderWithoutFill(*border);
        borderWithoutFill.fill.reset();
        renderBorder(borderWithoutFill, offset, {extent.width, extent.height}, pass);
    }
    setUseBorderColorOverride(false);
    _extent = originalExtent;
}

} // namespace gui

} // namespace reone
