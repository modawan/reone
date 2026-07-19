/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/gui/statussummary.h"

#include "reone/game/game.h"

using namespace reone::gui;

namespace reone {

namespace game {

static constexpr std::array<const char *, kStatusSummaryCategoryCount> kIconTags {{
    "LBL_JOURNAL",
    "LBL_CREDITS",
    "LBL_XP",
    "LBL_STEALTH",
    "LBL_DARKSIDE",
    "LBL_LIGHTSIDE",
    "LBL_RECEIVED",
    "LBL_LOST",
}};

static constexpr std::array<const char *, kStatusSummaryCategoryCount> kDescriptionTags {{
    "LBL_JOURNAL_DESC",
    "LBL_CREDITS_DESC",
    "LBL_XP_DESC",
    "LBL_STEALTH_DESC",
    "LBL_DARKSIDE_DESC",
    "LBL_LIGHTSIDE_DESC",
    "LBL_RECEIVED_DESC",
    "LBL_LOST_DESC",
}};

static constexpr std::array<const char *, 10> kUnusedTSLRowTags {{
    "LBL_INFLUENCE_LOST",
    "LBL_INFLUENCE_LOST_DESC",
    "LBL_INFLUENCE_RECV",
    "LBL_INFLUENCE_RECV_DESC",
    "LBL_MAX_FP_GAINED",
    "LBL_MAX_FP_GAINED_DESC",
    "LBL_MAX_FP_LOST",
    "LBL_MAX_FP_LOST_DESC",
    "LBL_NETSHIFT",
    "LBL_NETSHIFT_DESC",
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
    _ok = findControl<Button>("BTN_OK");
    for (size_t i = 0; i < _rows.size(); ++i) {
        auto &row = _rows[i];
        row.icon = findControl<Label>(kIconTags[i]);
        row.description = findControl<Label>(kDescriptionTags[i]);
        if (row.icon) {
            row.iconExtent = row.icon->extent();
        }
        if (row.description) {
            row.descriptionExtent = row.description->extent();
            row.authoredText = row.description->text().text;
        }
    }
    for (const auto *tag : kUnusedTSLRowTags) {
        auto control = findControl<Control>(tag);
        if (control) {
            _unusedTSLRows.push_back(std::move(control));
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
            row.description->setTextMessage("");
            row.description->setExtent(row.descriptionExtent);
        }
    }
    for (auto &control : _unusedTSLRows) {
        control->setVisible(false);
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

void StatusSummary::layoutDisplayedBatch() {
    const auto &displayed = _accumulator.displayed();
    if (!displayed) {
        return;
    }

    auto active = displayed->activeCategories();
    for (size_t slot = 0; slot < active.size(); ++slot) {
        size_t category = static_cast<size_t>(active[slot]);
        auto &row = _rows[category];
        const auto &target = _rows[slot];

        if (row.icon) {
            auto extent = row.iconExtent;
            extent.top = target.iconExtent.top;
            row.icon->setExtent(std::move(extent));
            row.icon->setVisible(true);
        }
        if (row.description) {
            auto extent = row.descriptionExtent;
            extent.top = target.descriptionExtent.top;
            row.description->setExtent(std::move(extent));
            row.description->setTextMessage(row.authoredText);
            row.description->setVisible(true);
        }
    }

    auto okExtent = _okExtent;
    auto rootExtent = _rootExtent;
    if (active.size() < _rows.size() && _rows[active.size()].icon) {
        okExtent.top = _rows[active.size()].iconExtent.top;
        int authoredBottomMargin = _rootExtent.height - (_okExtent.top + _okExtent.height);
        rootExtent.height = okExtent.top + okExtent.height + authoredBottomMargin;
    }
    _ok->setExtent(std::move(okExtent));
    _ok->setVisible(true);
    _gui->rootControl().setExtent(std::move(rootExtent));
}

} // namespace game

} // namespace reone
