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

#include "reone/game/gui/chargen/iconselection.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/textures.h"

#include <cmath>

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr int kIconCellSize = 40;
static constexpr int kIconSize = 32;
static constexpr int kArrowSize = 32;
static constexpr int kK1VisibleRows = 5;
static constexpr int kK1ScrollBarGap = 4;
static constexpr int kTSLVisibleRows = 7;
static constexpr char kK1CellFill[] = "lbl_indent";
static constexpr char kK1Arrow[] = "lbl_skarr";
static constexpr char kK1CellBorderCorner[] = "border2d";
static constexpr char kK1CellBorderEdge[] = "border1d";
static constexpr int kK1CellBorderDimension = 8;
static constexpr glm::vec3 kK1BorderGreen {0.278431f, 0.921569f, 0.105882f};
static constexpr glm::vec3 kK1BorderGreenDim = 0.7f * kK1BorderGreen;
static constexpr glm::vec3 kK1BorderGold {0.980392f, 1.0f, 0.0f};
static constexpr glm::vec3 kK1BorderRed {0.698039f, 0.0f, 0.0f};
static constexpr glm::vec3 kTSLLockedBorderColor {0.698039f, 0.0f, 0.0f};
static constexpr glm::vec3 kTSLSelectableBorderColor {0.05098f, 0.34902f, 0.270588f};
static constexpr glm::vec3 kTSLOwnedBorderColor {0.101961f, 0.698039f, 0.54902f};
static constexpr glm::vec3 kTSLSelectedBorderColor {1.0f};
static constexpr char kTSLArrow[] = "uibit_abi_arrow";

static int scalePixels(int pixels, float scale) {
    return std::max(1, static_cast<int>(std::lround(pixels * scale)));
}

std::shared_ptr<IconChain> addIconSelectionChain(
    IGUI &gui,
    Game &game,
    ServicesView &services,
    std::string tag,
    ListBox &sourceList,
    const glm::vec3 &hilightColor,
    IconSelectionCallbacks callbacks) {

    auto chainControl = std::shared_ptr<Control>(gui.newControl(ControlType::IconChain, std::move(tag)));
    auto chain = std::static_pointer_cast<IconChain>(chainControl);
    chain->setExtent(sourceList.extent());
    chain->setPadding(sourceList.padding());
    chain->setBorder(sourceList.border());
    if (!game.isTSL()) {
        chain->setHilight(sourceList.border());
        chain->setHilightColor(hilightColor);
    }
    chain->setTintBorderFill(game.isTSL());
    float layoutScale = gui.scale();
    auto scrollBar = sourceList.scrollBarOrNull();
    if (scrollBar) {
        // Restretch the bar to the grid panel's plain GUI scale, then slim
        // its width to the shared density unit: the grid cells stay at full
        // scale, but a full-width bar reads heavier than every other bar
        // once lists and prose draw at their reduced scales.
        scrollBar->stretch(layoutScale, layoutScale);
        auto scrollBarExtent = scrollBar->extent();
        scrollBarExtent.width = scalePixels(
            scrollBar->authoredExtent().width, layoutScale * gui.listScale());
        scrollBar->setExtent(std::move(scrollBarExtent));
        ListBox::insetScrollBar(
            *scrollBar,
            chain->extent(),
            chain->border().dimension,
            /* leftSide = */ false,
            layoutScale);
    }
    int cellSize = scalePixels(kIconCellSize, layoutScale);
    chain->setCellSize(cellSize);
    auto &listExtent = sourceList.extent();
    auto &protoExtent = sourceList.protoItem().extent();
    int contentInsetY = protoExtent.top - listExtent.top;
    int originY = contentInsetY;
    int rowStep = protoExtent.height;
    if (!game.isTSL()) {
        int contentHeight = listExtent.height - 2 * contentInsetY;
        int remainingHeight = contentHeight - kK1VisibleRows * cellSize;
        int gapCount = kK1VisibleRows + 1;
        int rowGap = (remainingHeight + gapCount / 2) / gapCount;
        originY += rowGap;
        rowStep = cellSize + rowGap;
    } else {
        int fillInsetY = sourceList.border().dimension;
        int fillHeight = listExtent.height - 2 * fillInsetY;
        int rowRange = fillHeight - cellSize;
        int gapCount = kTSLVisibleRows - 1;
        std::vector<int> rowOffsets;
        rowOffsets.reserve(kTSLVisibleRows);
        for (int row = 0; row < kTSLVisibleRows; ++row) {
            rowOffsets.push_back(
                fillInsetY + (row * rowRange + gapCount / 2) / gapCount);
        }
        chain->setRowOffsets(std::move(rowOffsets));
    }
    int cellOriginX = protoExtent.left - listExtent.left;
    int cellRangeX = protoExtent.width - cellSize;
    if (!game.isTSL() && scrollBar) {
        // K1 authors the proto row beneath the scroll bar. Keep the square
        // cells and their icons intact, but distribute the columns only over
        // the unobstructed part of the panel.
        int scrollBarLeft = scrollBar->extent().left - listExtent.left;
        int contentRight = scrollBarLeft - scalePixels(kK1ScrollBarGap, layoutScale);
        cellRangeX = std::max(0, contentRight - cellOriginX - cellSize);
    }
    chain->setCellOrigin(cellOriginX, originY);
    chain->setCellStep(
        cellRangeX / (kIconSelectionColumnCount - 1),
        rowStep);

    IconChain::CellStyle cellStyle;
    if (!game.isTSL()) {
        cellStyle.backgroundTexture = services.resource.textures.get(kK1CellFill, TextureUsage::GUI);
        cellStyle.linkTexture = services.resource.textures.get(kK1Arrow, TextureUsage::GUI);
        int arrowSize = scalePixels(kArrowSize, layoutScale);
        cellStyle.linkSize = {arrowSize, arrowSize};
        cellStyle.itemBorder = std::make_shared<Control::Border>();
        cellStyle.itemBorder->corner = services.resource.textures.get(kK1CellBorderCorner, TextureUsage::GUI);
        cellStyle.itemBorder->edge = services.resource.textures.get(kK1CellBorderEdge, TextureUsage::GUI);
        cellStyle.itemBorder->dimension = scalePixels(kK1CellBorderDimension, layoutScale);
        cellStyle.borderColors = std::make_shared<IconChain::CellStyle::BorderColors>();
        cellStyle.borderColors->owned = kK1BorderGreenDim;
        cellStyle.borderColors->selected = kK1BorderGreen;
        cellStyle.focusedBorderColors = std::make_shared<IconChain::CellStyle::FocusedBorderColors>();
        cellStyle.focusedBorderColors->locked = kK1BorderRed;
        cellStyle.focusedBorderColors->selectable = kK1BorderGold;
        cellStyle.focusedBorderColors->owned = kK1BorderGreen;
        cellStyle.focusedBorderColors->selected = kK1BorderGreen;
        cellStyle.onlyDrawItemBorderWhenBright = true;
    } else {
        cellStyle.linkTexture = services.resource.textures.get(kTSLArrow, TextureUsage::GUI);
        int arrowSize = scalePixels(kArrowSize, layoutScale);
        cellStyle.linkSize = {arrowSize, arrowSize};
        cellStyle.borderColors = std::make_shared<IconChain::CellStyle::BorderColors>();
        cellStyle.borderColors->locked = kTSLLockedBorderColor;
        cellStyle.borderColors->selectable = kTSLSelectableBorderColor;
        cellStyle.borderColors->owned = kTSLOwnedBorderColor;
        cellStyle.borderColors->selected = kTSLSelectedBorderColor;
        cellStyle.focusedBorderColors = std::make_shared<IconChain::CellStyle::FocusedBorderColors>();
        cellStyle.focusedBorderColors->locked = kTSLLockedBorderColor;
        cellStyle.focusedBorderColors->selectable = kTSLSelectableBorderColor;
        cellStyle.focusedBorderColors->owned = kTSLOwnedBorderColor;
        cellStyle.focusedBorderColors->selected = kTSLSelectedBorderColor;
    }
    cellStyle.iconSize = scalePixels(kIconSize, layoutScale);
    cellStyle.dimLockedBackground = !game.isTSL();
    cellStyle.drawItemBorderFill = true;
    cellStyle.drawItemBorderBeforeIcon = game.isTSL();
    chain->setCellStyle(std::move(cellStyle));
    chain->setScrollBar(std::move(scrollBar));
    chain->setOnItemFocus(std::move(callbacks.onItemFocus));
    chain->setOnItemFocusCleared(std::move(callbacks.onItemFocusCleared));
    chain->setOnItemDoubleClick(std::move(callbacks.onItemDoubleClick));
    gui.addControlToBack(chain, IGUI::ControlCoordinates::Screen);
    return chain;
}

void styleChargenTitles(
    Game &game,
    Label &remainingLabel,
    ListBox &descriptionBox) {

    // K1 places the remaining-points panel to the left of the description.
    // Treating its right edge as the description's boundary collapses the
    // description width to zero. Only TSL authors these controls as aligned
    // right-column panels.
    if (!game.isTSL()) {
        return;
    }

    // Align the description box's right edge with the remaining-selections
    // bar's, keeping the left edge.
    auto descExtent = descriptionBox.extent();
    const auto &remainingExtent = remainingLabel.extent();
    descExtent.width = remainingExtent.left + remainingExtent.width - descExtent.left;
    descriptionBox.setExtent(descExtent);
}

} // namespace game

} // namespace reone
