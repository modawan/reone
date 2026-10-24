/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/gui/statussummary.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string_view>
#include <vector>

#include "reone/game/game.h"
#include "reone/graphics/font.h"

using namespace reone::gui;

namespace reone {

namespace game {

/** The tutorial window that a summary reporting a journal update brings. */
static constexpr int kJournalSummaryTutorial = 14;

// The panel's layout, in authored units: KotOR's on a 640x480 canvas, TSL's on
// an 800x600 one.
static constexpr float kK1CanvasWidth = 640.0f;
static constexpr float kK1CanvasHeight = 480.0f;
static constexpr float kTSLCanvasWidth = 800.0f;
static constexpr float kTSLCanvasHeight = 600.0f;
/** The top of the first row. */
static constexpr float kK1FirstRowTop = 10.0f;
static constexpr float kTSLFirstRowTop = 18.0f;
/** The narrowest description. */
static constexpr float kK1DescriptionWidth = 150.0f;
static constexpr float kTSLDescriptionWidth = 225.0f;
/** A row's height, and how far its description sits above its icon. */
static constexpr float kRowHeight = 37.0f;
static constexpr float kDescriptionRaise = 1.0f;
/** TSL's icon size and one-row description height. */
static constexpr float kTSLIconSize = 32.0f;
static constexpr float kTSLDescriptionHeight = 31.0f;
/** A row whose text wraps widens by a step while it is no wider than this. */
static constexpr float kMaxWideningRowWidth = 439.0f;
static constexpr float kK1RowWidthStep = 20.0f;
static constexpr float kTSLRowWidthStep = 30.0f;
/** TSL's received-items list: its narrowest width, and the height up to which it takes one row. */
static constexpr float kTSLItemsListWidth = 450.0f;
static constexpr float kTSLItemsListOneRowHeight = 36.0f;
/** KotOR puts the button this far above the next row's place. */
static constexpr float kK1ButtonRaise = 7.0f;
/** The panel's margin below the last row (KotOR) or below the button (TSL), and right of the widest row. */
static constexpr float kK1PanelBottomMargin = 25.0f;
static constexpr float kTSLPanelBottomMargin = 20.0f;
static constexpr float kK1PanelRightMargin = 10.0f;
static constexpr float kTSLPanelRightMargin = 30.0f;

// The rows that report an amount or a list read it from custom token 0.
static constexpr int kCreditsGainedStrRef = 42437;
static constexpr int kCreditsLostStrRef = 42627;
static constexpr int kCreditsMixedStrRef = 47933;
static constexpr int kPlotXPStrRef = 42438;
static constexpr int kStealthXPStrRef = 42439;
static constexpr int kNetDarkShiftStrRef = 47934;
static constexpr int kNetLightShiftStrRef = 47935;
static constexpr int kItemsReceivedStrRef = 42442;
static constexpr int kInfluenceGainedStrRef = 48607;
static constexpr int kInfluenceLostStrRef = 48608;

/** The party NPC names of the influence rows, by NPC index. */
static constexpr std::array<int, kStatusSummaryInfluenceMemberCount> kInfluenceMemberNames {{
    48391,
    102045,
    49456,
    48392,
    102046,
    27766,
    102048,
    102049,
    22095,
    48384,
    102047,
    48393,
}};

static constexpr std::array<const char *, kStatusSummaryCategoryCount> kIconTags {{
    "LBL_JOURNAL",
    "LBL_CREDITS",
    "LBL_XP",
    "LBL_STEALTH",
    "LBL_DARKSIDE",
    "LBL_LIGHTSIDE",
    "LBL_NETSHIFT",
    "LBL_RECEIVED",
    "LBL_LOST",
    "LBL_INFLUENCE_RECV",
    "LBL_INFLUENCE_LOST",
    "LBL_MAX_FP_GAINED",
    "LBL_MAX_FP_LOST",
}};

static constexpr std::array<const char *, kStatusSummaryCategoryCount> kDescriptionTags {{
    "LBL_JOURNAL_DESC",
    "LBL_CREDITS_DESC",
    "LBL_XP_DESC",
    "LBL_STEALTH_DESC",
    "LBL_DARKSIDE_DESC",
    "LBL_LIGHTSIDE_DESC",
    "LBL_NETSHIFT_DESC",
    "LBL_RECEIVED_DESC",
    "LBL_LOST_DESC",
    "LBL_INFLUENCE_RECV_DESC",
    "LBL_INFLUENCE_LOST_DESC",
    "LBL_MAX_FP_GAINED_DESC",
    "LBL_MAX_FP_LOST_DESC",
}};

StatusSummary::StatusSummary(
    Game &game,
    ServicesView &services,
    StatusSummaryAccumulator &accumulator) :
    GameGUI(game, services),
    _accumulator(accumulator) {

    _resRef = guiResRef("statussummary");
}

void StatusSummary::onGUILoaded() {
    centerRootInCanvas(_game.isTSL() ? 800 : 640, _game.isTSL() ? 600 : 480);

    _ok = findControl<Button>("BTN_OK");
    for (size_t i = 0; i < _rows.size(); ++i) {
        auto &row = _rows[i];
        row.icon = findControl<Label>(kIconTags[i]);
        row.description = findControl<Label>(kDescriptionTags[i]);
        if (row.icon) {
            row.icon->setSharpenBorderFillAlpha(true);
            row.iconExtent = row.icon->extent();
        }
        if (row.description) {
            row.descriptionExtent = row.description->extent();
            row.authoredText = row.description->text().text;
        }
    }
    _rootExtent = _gui->rootControl().extent();
    if (_ok) {
        _okExtent = _ok->extent();
        _ok->setOnClick([this]() {
            acknowledge();
        });
    }
    _loaded = true;
    clearPresentation();
}

bool StatusSummary::presentPending() {
    if (!_loaded || !_ok || _visible) {
        return false;
    }
    for (auto category : _accumulator.pending().activeCategories()) {
        const auto &row = _rows[static_cast<size_t>(category)];
        if (!row.icon || !row.description) {
            return false;
        }
    }
    if (!_accumulator.beginPresentation()) {
        return false;
    }
    layoutDisplayedBatch();
    _visible = true;
    // A summary that reports a journal update asks for its tutorial window.
    const auto &displayed = _accumulator.displayed();
    if (displayed && displayed->entry(StatusSummaryCategory::Journal).active) {
        _game.requestTutorialWindow(kJournalSummaryTutorial);
    }
    return true;
}

bool StatusSummary::handle(const input::Event &event) {
    if (!_visible) {
        return false;
    }

    // Status Summary is modal. The GUI receives the event first, then the
    // whole event is consumed even when it landed outside an active control.
    GameGUI::handle(event);
    return true;
}

void StatusSummary::acknowledge() {
    if (!_visible) {
        return;
    }
    _accumulator.acknowledge();
    clearPresentation();
}

void StatusSummary::reset() {
    clearPresentation();
}

void StatusSummary::clearPresentation() {
    for (auto &row : _rows) {
        if (row.icon) {
            row.icon->setVisible(false);
            row.icon->setExtent(row.iconExtent);
        }
        if (row.description) {
            row.description->setVisible(false);
            row.description->setTextMessage(row.authoredText);
            row.description->setExtent(row.descriptionExtent);
        }
    }
    if (_ok) {
        _ok->setVisible(false);
        _ok->setExtent(_okExtent);
    }
    if (_gui) {
        _gui->clearSelection();
        _gui->rootControl().setExtent(_rootExtent);
    }
    _visible = false;
}

/** The widest line of a description's text, unwrapped; a label without a font has none. */
static int idealTextWidth(const Label &description) {
    const auto &text = description.text();
    if (!text.font) {
        return 0;
    }
    float widest = 0.0f;
    size_t begin = 0;
    while (begin <= text.text.size()) {
        size_t end = text.text.find('\n', begin);
        if (end == std::string::npos) {
            end = text.text.size();
        }
        widest = std::max(widest, text.font->measure(std::string_view(text.text).substr(begin, end - begin), description.scale()));
        begin = end + 1;
    }
    return static_cast<int>(std::ceil(widest));
}

static int textHeight(const Label &description) {
    const auto &text = description.text();
    const int lineHeight = text.font ? static_cast<int>(text.font->height() * description.scale()) : 0;
    return static_cast<int>(description.textLines().size()) * lineHeight;
}

void StatusSummary::layoutDisplayedBatch() {
    const auto &displayed = _accumulator.displayed();
    if (!displayed) {
        return;
    }

    // The panel is laid out from its top, one shown row after another, in
    // authored units drawn at the GUI's scale. A description is as wide as
    // the widest row above it, and a row whose text still wraps widens step
    // by step. The panel then fits the rows and its button and is centred on
    // the screen.
    const bool tsl = _game.isTSL();
    const float scale = _gui->scale();
    auto px = [scale](float authored) { return static_cast<int>(authored * scale); };

    int y = px(tsl ? kTSLFirstRowTop : kK1FirstRowTop);
    int width = px(tsl ? kTSLDescriptionWidth : kK1DescriptionWidth);
    std::vector<std::reference_wrapper<Control>> placed;
    for (auto category : displayed->activeCategories()) {
        auto &row = _rows[static_cast<size_t>(category)];
        const auto &entry = displayed->entry(category);

        auto iconExtent = row.iconExtent;
        iconExtent.top = y;
        if (tsl) {
            iconExtent.width = px(kTSLIconSize);
            iconExtent.height = px(kTSLIconSize);
        }
        row.icon->setExtent(std::move(iconExtent));
        if (category == StatusSummaryCategory::NetShift) {
            // The net shift shows the icon of the side it leans to.
            const auto &side = entry.amount > 0
                                   ? _rows[static_cast<size_t>(StatusSummaryCategory::LightSideShift)]
                                   : _rows[static_cast<size_t>(StatusSummaryCategory::DarkSideShift)];
            row.icon->setBorderFill(side.icon->border().fill);
        }
        row.icon->setVisible(true);
        placed.push_back(*row.icon);

        auto &description = *row.description;
        description.setTextMessage(descriptionText(category, entry, row.authoredText));
        const bool hasText = !description.text().text.empty();
        const bool itemsReceived = category == StatusSummaryCategory::ItemsReceived;
        auto extent = row.descriptionExtent;
        extent.top = y - px(kDescriptionRaise);
        int rowWidth = width;
        if (tsl) {
            // TSL starts from the text's own width; the received items are as
            // tall as their lines, and at least as wide as their list.
            extent.height = itemsReceived ? textHeight(description) : px(kTSLDescriptionHeight);
            extent.width = idealTextWidth(description);
            rowWidth = std::max(extent.width, width);
            if (itemsReceived && hasText) {
                rowWidth = std::max(rowWidth, px(kTSLItemsListWidth));
                extent.width = rowWidth;
            }
        } else {
            extent.width = width;
        }
        description.setExtent(extent);
        if (hasText && !(tsl && itemsReceived)) {
            while (rowWidth <= px(kMaxWideningRowWidth) && description.textLines().size() > 1) {
                rowWidth += px(tsl ? kTSLRowWidthStep : kK1RowWidthStep);
                extent.width = rowWidth;
                description.setExtent(extent);
            }
        }
        description.setVisible(true);
        placed.push_back(description);

        if (_flash && displayed->flashes(category, tsl)) {
            _flash(category);
        }

        if (tsl && itemsReceived) {
            const int height = textHeight(description);
            y += height <= px(kTSLItemsListOneRowHeight) ? px(kRowHeight) : height;
        } else {
            y += px(kRowHeight);
        }
        width = rowWidth;
    }

    auto okExtent = _okExtent;
    okExtent.top = tsl ? y : y - px(kK1ButtonRaise);
    auto rootExtent = _rootExtent;
    rootExtent.height = tsl ? y + okExtent.height + px(kTSLPanelBottomMargin) : y + px(kK1PanelBottomMargin);
    rootExtent.width = _rows.front().descriptionExtent.left + width + px(tsl ? kTSLPanelRightMargin : kK1PanelRightMargin);
    rootExtent.left = (px(tsl ? kTSLCanvasWidth : kK1CanvasWidth) - rootExtent.width) / 2;
    rootExtent.top = (px(tsl ? kTSLCanvasHeight : kK1CanvasHeight) - rootExtent.height) / 2;
    okExtent.left = (rootExtent.width - okExtent.width) / 2;
    _ok->setExtent(std::move(okExtent));
    _ok->setVisible(true);
    placed.push_back(*_ok);

    // The rows and the button are placed from the panel's corner, which the
    // layout drew where the authored panel stood.
    const int dx = rootExtent.left - _rootExtent.left;
    const int dy = rootExtent.top - _rootExtent.top;
    for (Control &control : placed) {
        auto extent = control.extent();
        extent.left += dx;
        extent.top += dy;
        control.setExtent(std::move(extent));
    }
    _gui->rootControl().setExtent(std::move(rootExtent));
}

std::string StatusSummary::descriptionText(
    StatusSummaryCategory category,
    const StatusSummaryEntry &entry,
    const std::string &authoredText) const {

    switch (category) {
    case StatusSummaryCategory::Credits: {
        // A loss shows the amount lost; gains and losses together carry a
        // leading note.
        auto text = entry.amount < 0
                        ? _game.getInterfaceText(kCreditsLostStrRef, {{0, std::to_string(-entry.amount)}})
                        : _game.getInterfaceText(kCreditsGainedStrRef, {{0, std::to_string(entry.amount)}});
        return entry.mixed ? _game.getInterfaceText(kCreditsMixedStrRef) + text : text;
    }
    case StatusSummaryCategory::PlotXP:
        return _game.getInterfaceText(kPlotXPStrRef, {{0, std::to_string(entry.amount)}});
    case StatusSummaryCategory::StealthXP:
        return _game.getInterfaceText(kStealthXPStrRef, {{0, std::to_string(entry.amount)}});
    case StatusSummaryCategory::NetShift:
        return _game.getInterfaceText(entry.amount > 0 ? kNetLightShiftStrRef : kNetDarkShiftStrRef);
    case StatusSummaryCategory::ItemsReceived: {
        // TSL names the received items, one per line, with their actions hidden.
        if (!_game.isTSL()) {
            return authoredText;
        }
        std::string names;
        for (const auto &name : entry.items) {
            names = _game.substituteLogTokens(names + name + "\n");
        }
        return _game.getInterfaceText(kItemsReceivedStrRef, {{0, names}});
    }
    case StatusSummaryCategory::InfluenceGained:
    case StatusSummaryCategory::InfluenceLost: {
        // The party NPCs whose influence moved, by NPC index, comma separated.
        std::string names;
        for (int npc : entry.npcs) {
            auto name = _game.getInterfaceText(kInfluenceMemberNames[static_cast<size_t>(npc)]);
            names = names.empty() ? name : names + ", " + name;
        }
        return _game.getInterfaceText(
            category == StatusSummaryCategory::InfluenceGained ? kInfluenceGainedStrRef : kInfluenceLostStrRef,
            {{0, names}});
    }
    default:
        return authoredText;
    }
}

} // namespace game

} // namespace reone
