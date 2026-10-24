/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <array>
#include <functional>

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"

#include "../gui.h"
#include "../statussummary.h"

namespace reone {

namespace game {

/** Modal runtime wrapper for statussummary.gui/statussummary_p.gui. */
class StatusSummary : public GameGUI {
public:
    StatusSummary(Game &game, ServicesView &services, StatusSummaryAccumulator &accumulator);

    /** Snapshot and show the current pending batch. Called from HUD::update. */
    bool presentPending();
    void reset();

    bool isVisible() const { return _visible; }
    /** Presenting a shown row may flash that row's indicator on the main interface. */
    void setOnStatusFlash(std::function<void(StatusSummaryCategory)> flash) { _flash = std::move(flash); }

    bool handle(const input::Event &event) override;

private:
    friend class TestStatusSummary;

    struct RowControls {
        std::shared_ptr<gui::Label> icon;
        std::shared_ptr<gui::Label> description;
        gui::Control::Extent iconExtent;
        gui::Control::Extent descriptionExtent;
        std::string authoredText;
    };

    StatusSummaryAccumulator &_accumulator;
    std::shared_ptr<gui::Button> _ok;
    std::array<RowControls, kStatusSummaryCategoryCount> _rows;
    gui::Control::Extent _rootExtent;
    gui::Control::Extent _okExtent;
    bool _loaded {false};
    bool _visible {false};
    std::function<void(StatusSummaryCategory)> _flash;

    void onGUILoaded() override;
    void acknowledge();
    void clearPresentation();
    void layoutDisplayedBatch();
    std::string descriptionText(
        StatusSummaryCategory category,
        const StatusSummaryEntry &entry,
        const std::string &authoredText) const;
};

} // namespace game

} // namespace reone
